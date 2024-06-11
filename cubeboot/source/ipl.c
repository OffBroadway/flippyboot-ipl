// ipl loading
#include <ogc/machine/processor.h>
#include <ogcsys.h>
#include <gccore.h>
#include <unistd.h>

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "sd.h"

#include "print.h"
#include "helpers.h"
#include "halt.h"

#include "descrambler.h"
#include "crc32.h"
#include "ipl.h"

#include "gc_dvd.h"

extern GXRModeObj *rmode;
extern void *xfb;

#define IPL_ROM_FONT_SJIS	0x1AFF00
#define DECRYPT_START		0x100

#define IPL_SIZE 0x200000
#define BS2_START_OFFSET 0x800
#define BS2_CODE_OFFSET (BS2_START_OFFSET + 0x20)
#define BS2_BASE_ADDR 0x81300000

#define DISABLE_SDA_CHECK

// SDA finding
#ifndef DISABLE_SDA_CHECK
#define CLEAR32_INST_CNT 1
#define LOAD32_INST_CNT 2
#define INST_SIZE 4

#define NUM_GPR 32
#define NUM_RESERVED_GPR 3
#define NUM_GPR_CLEARS (INST_SIZE * (CLEAR32_INST_CNT * (NUM_GPR - NUM_RESERVED_GPR)))
#define SDA_LOAD_OFFSET (INST_SIZE * (LOAD32_INST_CNT * 2))

#define STACK_SETUP_ADDR 0x81300098
#define SDA_LOAD_ADDR_A (STACK_SETUP_ADDR + SDA_LOAD_OFFSET)
#define SDA_LOAD_ADDR_B (SDA_LOAD_ADDR_A + NUM_GPR_CLEARS)
#endif

ATTRIBUTE_ALIGN(32) static u8 bios_buffer[IPL_SIZE];

static u32 bs2_size = IPL_SIZE - BS2_CODE_OFFSET;
static u8 *bs2 = (u8*)(BS2_BASE_ADDR);

s8 bios_index = -1;
bios_item *current_bios;

#ifdef TEST_IPL_PATH
char *bios_path = TEST_IPL_PATH;
#else
char *bios_path = "/ipl.bin";
#endif

// TODO: detect bad IPL images
// ipl_bad_ntsc_v10.bin  CRC = 6d740ae7, SHA1 = 015808f637a984acde6a06efa7546e278293c6ee
// ipl_bad2_ntsc_v10.bin CRC = 8bdabbd4, SHA1 = f1b0ef434cd74fd8fe23698e2fc911d945b45bf1
// ipl_bad_pal_v10.bin   CRC = dd8cab7c, SHA1 = 6f305c37dc1fbe332883bb8153eee26d3d325629
// ipl_unknown.bin       CRC = d235e3f9, SHA1 = 96f69a21645de73a5ba61e57951ef303d55788c5

// NOTE: these are not ipl.bin CRCs, but decoded ipl[0x100:] hashes
bios_item bios_table[] = {
    {IPL_NTSC_10,      IPL_NTSC,  "gc-ntsc-10",      "ntsc10",       "VER_NTSC_10",      CRC(0xa8325e47), SDA(0x81465320)},
    {IPL_NTSC_11,      IPL_NTSC,  "gc-ntsc-11",      "ntsc11",       "VER_NTSC_11",      CRC(0xf1ebeb95), SDA(0x81489120)},
    {IPL_NTSC_12_001,  IPL_NTSC,  "gc-ntsc-12_001",  "ntsc12_001",   "VER_NTSC_12_001",  CRC(0xc4c5a12a), SDA(0x8148b1c0)},
    {IPL_NTSC_12_101,  IPL_NTSC,  "gc-ntsc-12_101",  "ntsc12_101",   "VER_NTSC_12_101",  CRC(0xbf225e4d), SDA(0x8148b640)},
    {IPL_PAL_10,       IPL_PAL,   "gc-pal-10",       "pal10",        "VER_PAL_10",       CRC(0x5c3445d0), SDA(0x814b4fc0)},
    {IPL_PAL_11,       IPL_PAL,   "gc-pal-11",       "pal11",        "VER_PAL_11",       CRC(0x05196b74), SDA(0x81483de0)},
    {IPL_PAL_12,       IPL_PAL,   "gc-pal-12",       "pal12",        "VER_PAL_12",       CRC(0x1082fbc9), SDA(0x814b7280)},
};

extern void __SYS_ReadROM(void *buf,u32 len,u32 offset);

extern u64 gettime(void);
extern u32 diff_msec(s64 start,s64 end);

static bool valid = false;

void load_ipl() {
    if (is_dolphin()) {
        __SYS_ReadROM(bs2, bs2_size, BS2_CODE_OFFSET);
        iprintf("TEST IPL D, %08x\n", *(u32*)bs2);
    } else {
        iprintf("TEST IPL X\n");
        __SYS_ReadROM(bios_buffer, IPL_SIZE, 0);

        iprintf("TEST IPL A, %08x\n", *(u32*)bios_buffer);
        iprintf("TEST IPL C, %08x\n", *(u32*)(bios_buffer + DECRYPT_START));
        Descrambler(bios_buffer + DECRYPT_START, IPL_ROM_FONT_SJIS - DECRYPT_START);
        memcpy(bs2, bios_buffer + BS2_CODE_OFFSET, bs2_size);
        iprintf("TEST IPL D, %08x\n", *(u32*)bs2);
    }

#ifdef TEST_IPL_PATH
    int ret = dvd_custom_open(IPC_DEVICE_SD, TEST_IPL_PATH, FILE_ENTRY_TYPE_FILE, IPC_FILE_FLAG_DISABLECACHE | IPC_FILE_FLAG_DISABLEFASTSEEK);
    iprintf("OPEN ret: %08x\n", ret);

    dvd_read(bios_buffer, IPL_SIZE, 0);
    iprintf("TEST IPL A, %08x\n", *(u32*)bios_buffer);
    iprintf("TEST IPL C, %08x\n", *(u32*)(bios_buffer + DECRYPT_START));
    Descrambler(bios_buffer + DECRYPT_START, IPL_ROM_FONT_SJIS - DECRYPT_START);
    memcpy(bs2, bios_buffer + BS2_CODE_OFFSET, bs2_size);
    iprintf("TEST IPL D, %08x\n", *(u32*)bs2);
#endif

    u32 crc = csp_crc32_memory(bs2, bs2_size);
    iprintf("Read BS2 crc=%08x\n", crc);

    valid = false;
    for(int i = 0; i < sizeof(bios_table) / sizeof(bios_table[0]); i++) {
        if(bios_table[i].crc == crc) {
            bios_index = i;
            valid = true;
            break;
        }
    }

    iprintf("BS2 is valid? = %d\n", valid);

#ifdef FORCE_IPL_LOAD
    // TEST ONLY
    valid = false;
#endif

    if (!valid) {
        // #include "fatfs/ff.h"

        // DIR dir;
        // FRESULT lastRet = f_opendir(&dir, "/bios-sfn");
        // if (lastRet != FR_OK)
        // {
        //     prog_halt("badA");
        // }

        // int count = 0;
        // FILINFO fno;
        // while ((lastRet = f_readdir(&dir, &fno)) == FR_OK && count < 10) {
        //     iprintf("Found %s\n", fno.fname);
        //     count++;
        // }

        // f_closedir(&dir);

        int size = get_file_size(bios_path);
        if (size == SD_FAIL) {
            char err_buf[255];
            sprintf(err_buf, "Failed to find %s\n", bios_path);
            prog_halt(err_buf);
            return;
        }

        if (size != IPL_SIZE) {
            char err_buf[255];
            sprintf(err_buf, "File %s is the wrong size %x\n", bios_path, size);
            prog_halt(err_buf);
            return;
        }

        if (load_file_buffer(bios_path, bios_buffer)) {
            char err_buf[255];
            sprintf(err_buf, "Failed to load %s\n", bios_path);
            prog_halt(err_buf);
            return;
        }

        Descrambler(bios_buffer + DECRYPT_START, IPL_ROM_FONT_SJIS - DECRYPT_START);
        memcpy(bs2, bios_buffer + BS2_CODE_OFFSET, bs2_size);
    } else {
        goto ipl_loaded;
    }

    crc = csp_crc32_memory(bs2, bs2_size);
    iprintf("Read IPL crc=%08x\n", crc);

    valid = FALSE;
    for(int i = 0; i < sizeof(bios_table) / sizeof(bios_table[0]); i++) {
        if(bios_table[i].crc == crc) {
            bios_index = i;
            valid = TRUE;
            break;
        }
    }

    if (!valid) {
        prog_halt("Bad IPL image\n");
    }

ipl_loaded:
    current_bios = &bios_table[bios_index];
    iprintf("IPL %s loaded...\n", current_bios->name);

    // UNTESTED
    if (current_bios->type == IPL_PAL && VIDEO_GetCurrentTvMode() == VI_NTSC) {
        iprintf("Switching to VI to PAL\n");
        if (current_bios->version == IPL_PAL_11) {
            rmode = &TVPal528IntDf;
        } else {
            rmode = &TVMpal480IntDf;
        }
        VIDEO_Configure(rmode);
        VIDEO_SetNextFramebuffer(xfb);
        VIDEO_ClearFrameBuffer(rmode, xfb, COLOR_BLACK);
        VIDEO_SetBlack(FALSE);
        VIDEO_Flush();
        VIDEO_WaitVSync();
        if(rmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();
    }
}

// #ifndef DISABLE_SDA_CHECK
//     u32 *sda_load = (u32*)SDA_LOAD_ADDR_A;
//     if (*(u32*)STACK_SETUP_ADDR == 0x38000000) {
//         sda_load = (u32*)SDA_LOAD_ADDR_B;
//     }
//     u32 sda_high = (sda_load[0] & 0xFFFF) << 16;
//     u32 sda_low = sda_load[1] & 0xFFFF;
//     u32 sda = sda_high | sda_low;
//     iprintf("Read BS2 sda=%08x\n", sda);
// #endif
