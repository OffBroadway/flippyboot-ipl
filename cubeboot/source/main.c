#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include <ogcsys.h>
#include <gccore.h>
#include <unistd.h>

#include <asndlib.h>
#include <ogc/lwp_threads.h>

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <malloc.h>

#include "crc32.h"

#include "ipl.h"
#include "patches_elf.h"
#include "elf.h"

#include "boot/sidestep.h"
#include "sd.h"

#include "print.h"
#include "helpers.h"
#include "halt.h"

#include "pcg_basic.h"

#include "config.h"
#include "loader.h"

#define is_fallback_enabled() 0

extern void udelay(int us);

static u32 prog_entrypoint, prog_dst, prog_src, prog_len;
static u32 *bs2done = (u32*)0x81700000;

#define BS2_BASE_ADDR 0x81300000
static void (*bs2entry)(void) = (void(*)(void))BS2_BASE_ADDR;

static char stringBuffer[255];
u8 current_dol_buf[512 * 1024];
u32 current_dol_len;

extern const void _start;
extern const void _edata;
extern const void _end;

// // text logo replacment
// void *gc_text_tex_data_ptr;
// extern void render_logo();

u32 can_load_dol = 0;

GXRModeObj *rmode;
void *xfb;

// u8 xfb[0xb4000];

void __SYS_PreInit() {
    if (*bs2done == 0xCAFEBEEF) return;

    SYS_SetArenaHi((void*)BS2_BASE_ADDR);

    current_dol_len = &_edata - &_start;
    memcpy(current_dol_buf, &_start, current_dol_len);
}

int main() {
#ifdef VIDEO_ENABLE
	VIDEO_Init();
	rmode = VIDEO_GetPreferredMode(NULL);
	xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode)); // heap
#ifdef CONSOLE_ENABLE
	console_init(xfb,20,20,rmode->fbWidth,rmode->xfbHeight,rmode->fbWidth*VI_DISPLAY_PIX_SZ);
#endif
	VIDEO_Configure(rmode);
	VIDEO_SetNextFramebuffer(xfb);
    VIDEO_ClearFrameBuffer(rmode, xfb, COLOR_BLACK);
	VIDEO_SetBlack(FALSE);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if(rmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();
    // // debug above
#endif

#ifdef DOLPHIN_PRINT_ENABLE
    InitializeUART();
#endif

#ifdef GECKO_PRINT_ENABLE
    // enable printf
    CON_EnableGecko(EXI_CHANNEL_1, FALSE);
#endif

    iprintf("XFB = %08x [max=%x]\n", (u32)xfb, VIDEO_GetFrameBufferSize(&TVPal576ProgScale));

    // setup config device
    if (mount_available_device() != SD_OK) {
        iprintf("Could not find an inserted SD card\n");
        // VIDEO_WaitVSync();
        // ppchalt();
    }

    // check if we have a bootable dol
    if (check_load_program()) {
        can_load_dol = true;
    }

#if 0
    void *config_buf;
    if (load_file_dynamic("/cubeboot.ini", &config_buf) != SD_OK) {
        prog_halt("Could not find config file\n");
        return 1;
    }

    int size = get_file_size("/cubeboot.ini");
    iprintf("config_size = %d\n", size);

    char b[255];
    memset(b, 0, sizeof(b));
    strncpy(b, config_buf, size);
    iprintf("config: \n%s\n", b);

    free(config_buf);
#endif

#if 0
    if(is_fallback_enabled()) {
        void *dol_raw_buf = NULL;
        if (load_file_dynamic("/fallback.dol", &dol_raw_buf) != SD_OK) {
            prog_halt("Could not load fallback file\n");
            return 1;
        }

        DOLHEADER *hdr = (DOLHEADER *)dol_raw_buf;

        u32 fallback_start_addr = 0x80003100;
        u32 max = 0x80003000;

        // Inspect text sections to see if what we found lies in here
        for (int i = 0; i < MAXTEXTSECTION; i++) {
            if (hdr->textAddress[i] && hdr->textLength[i]) {
                u32 dst = (u32)current_dol_buf + hdr->textAddress[i] - fallback_start_addr;
                iprintf("Text section %08x\n", (u32)dst);
                memcpy((void*)dst, ((unsigned char*)dol_raw_buf) + hdr->textOffset[i], hdr->textLength[i]);
                u32 _max = hdr->textAddress[i] + hdr->textLength[i];
                if (_max > max) max = _max;
            }
        }

        // Inspect data sections (shouldn't really need to unless someone was sneaky..)
        for (int i = 0; i < MAXDATASECTION; i++) {
            if (hdr->dataAddress[i] && hdr->dataLength[i]) {
                u32 dst = (u32)current_dol_buf + hdr->dataAddress[i] - fallback_start_addr;
                iprintf("Data section %08x\n", (u32)dst);
                memcpy((void*)dst, ((unsigned char*)dol_raw_buf) + hdr->dataOffset[i], hdr->dataLength[i]);
                u32 _max = hdr->dataAddress[i] + hdr->dataLength[i];
                if (_max > max) max = _max;
            }
        }

        free(dol_raw_buf);

        current_dol_len = (u32)dol_raw_buf + max - fallback_start_addr;
    }
#endif

#if 0
    if(is_fallback_enabled()) {
        int fallback_size = get_file_size("/fallback.bin");
        iprintf("fallback size = %d\n", fallback_size);

        if (load_file_buffer("/fallback.bin", current_dol_buf) != SD_OK) {
            prog_halt("Could not load fallback file\n");
            return 1;
        }

        current_dol_len = fallback_size;
    }
#endif

    u32 random_color = generate_random_color();

    iprintf("Checkup, done=%08x\n", *bs2done);
    if (*bs2done == 0xCAFEBEEF) {
        iprintf("He's alive! The doc's alive! He's in the old west, but he's alive!!\n");

#ifdef VIDEO_ENABLE
        VIDEO_WaitVSync();
#endif

        // load program
        load_program();
    }

    // load ipl
    load_ipl();

//// elf world

    Elf32_Ehdr* ehdr;
    Elf32_Shdr* shdr;
    unsigned char* image;

    void *addr = (void*)patches_elf;
    ehdr = (Elf32_Ehdr *)addr;

    // get section string table
    Elf32_Shdr* shstr = &((Elf32_Shdr *)(addr + ehdr->e_shoff))[ehdr->e_shstrndx];
    char* stringdata = (char*)(addr + shstr->sh_offset);

    // get symbol string table
    Elf32_Shdr* symshdr = SHN_UNDEF;
    char* symstringdata = SHN_UNDEF;
    for (int i = 0; i < ehdr->e_shnum; ++i) {
        shdr = (Elf32_Shdr *)(addr + ehdr->e_shoff + (i * sizeof(Elf32_Shdr)));
        if (shdr->sh_type == SHT_SYMTAB) {
            symshdr = shdr;
        }
        if (shdr->sh_type == SHT_STRTAB && strcmp(stringdata + shdr->sh_name, ".strtab") == 0) {
            symstringdata = (char*)(addr + shdr->sh_offset);
        }
    }

    // get symbols
    Elf32_Sym* syment = (Elf32_Sym*) (addr + symshdr->sh_offset);

    // setup local vars
    char *patch_prefix = ".patch.";
    uint32_t patch_prefix_len = strlen(patch_prefix);
    char patch_region_suffix[128];
    sprintf(patch_region_suffix, "%s_func", current_bios->patch_suffix);

    char *reloc_prefix = ".reloc";
    u32 reloc_start = 0;
    u32 reloc_end = 0;
    char *reloc_region = current_bios->reloc_prefix;

    // Patch each appropriate section
    for (int i = 0; i < ehdr->e_shnum; ++i) {
        shdr = (Elf32_Shdr *)(addr + ehdr->e_shoff + (i * sizeof(Elf32_Shdr)));

        const char *sh_name = stringdata + shdr->sh_name;
        if ((!(shdr->sh_flags & SHF_ALLOC) && strncmp(patch_prefix, sh_name, patch_prefix_len) != 0) || shdr->sh_addr == 0 || shdr->sh_size == 0) {
            iprintf("Skipping ALLOC %s!!\n", stringdata + shdr->sh_name);
            continue;
        }

        shdr->sh_addr &= 0x3FFFFFFF;
        shdr->sh_addr |= 0x80000000;
        // shdr->sh_size &= 0xfffffffc;

        if (shdr->sh_type == SHT_NOBITS && strncmp(patch_prefix, stringdata + shdr->sh_name, patch_prefix_len) != 0) {
            iprintf("Skipping NOBITS %s @ %08x!!\n", stringdata + shdr->sh_name, shdr->sh_addr);
            memset((void*)shdr->sh_addr, 0, shdr->sh_size);
        } else {
            // check if this is a patch section
            uint32_t sh_size = 0;
            if (strncmp(patch_prefix, sh_name, patch_prefix_len) == 0) {
                // check if this patch is for the current IPL
                if (!ensdwith(sh_name, patch_region_suffix)) {
                    // iprintf("SKIP PATCH %s != %s\n", sh_name, patch_region_suffix);
                    continue;
                }

                // create symbol name for section size
                uint32_t sh_name_len = strlen(sh_name);
 
                strcpy(stringBuffer, sh_name);
                stringBuffer[sh_name_len - 5] = '\x00';
                strcat(&stringBuffer[0], "_size");
                char* current_symname = stringBuffer + patch_prefix_len;

                // find symbol by name
                for (int i = 0; i < (symshdr->sh_size / sizeof(Elf32_Sym)); ++i) {
                    if (syment[i].st_name == SHN_UNDEF) {
                        continue;
                    }

                    char *symname = symstringdata + syment[i].st_name;
                    if (strcmp(symname, current_symname) == 0) {
                        sh_size = syment[i].st_value;
                    }
                }
            } else if (strcmp(reloc_prefix, sh_name) == 0) {
                reloc_start = shdr->sh_addr;
                reloc_end = shdr->sh_addr + shdr->sh_size;
            }

            // set section size from header if it is not provided as a symbol
            if (sh_size == 0) sh_size = shdr->sh_size;

            image = (unsigned char*)addr + shdr->sh_offset;
#ifdef PRINT_PATCHES
            iprintf("patching ptr=%x size=%04x orig=%08x val=%08x [%s]\n", shdr->sh_addr, sh_size, *(u32*)shdr->sh_addr, *(u32*)image, sh_name);
#endif
            memcpy((void*)shdr->sh_addr, (const void*)image, sh_size);
        }
    }

    // Copy symbol relocations by region
    iprintf(".reloc section [0x%08x - 0x%08x]\n", reloc_start, reloc_end);
    for (int i = 0; i < (symshdr->sh_size / sizeof(Elf32_Sym)); ++i) {
        if (syment[i].st_name == SHN_UNDEF) {
            continue;
        }

        char *current_symname = symstringdata + syment[i].st_name;
        if (syment[i].st_value >= reloc_start && syment[i].st_value < reloc_end) {
            sprintf(stringBuffer, "%s_%s", reloc_region, current_symname);
            // iprintf("reloc: Looking for symbol named %s\n", stringBuffer);
            u32 val = get_symbol_value(symshdr, syment, symstringdata, stringBuffer);
            
            // if (strcmp(current_symname, "OSReport") == 0) {
            //     iprintf("OVERRIDE OSReport with iprintf\n");
            //     val = (u32)&iprintf;
            // }

            if (val != 0) {
#ifdef PRINT_RELOCS
                iprintf("Found reloc %s = %x, val = %08x\n", current_symname, syment[i].st_value, val);
#endif
                *(u32*)syment[i].st_value = val;
            } else {
                iprintf("ERROR broken reloc %s = %x\n", current_symname, syment[i].st_value);
            }
        }
    }

    // load current program
    prog_entrypoint = (u32)&_start;
    prog_src = (u32)current_dol_buf;
    prog_dst = (u32)&_start; // (u32*)0x80600000;
    prog_len = current_dol_len;

    iprintf("Current program start = %08x\n", prog_entrypoint);

    // Copy program metadata into place
    set_patch_value(symshdr, syment, symstringdata, "prog_entrypoint", prog_entrypoint);
    set_patch_value(symshdr, syment, symstringdata, "prog_src", prog_src);
    set_patch_value(symshdr, syment, symstringdata, "prog_dst", prog_dst);
    set_patch_value(symshdr, syment, symstringdata, "prog_len", prog_len);

    set_patch_value(symshdr, syment, symstringdata, "start_game", can_load_dol);
    set_patch_value(symshdr, syment, symstringdata, "cube_color", random_color);

    // while(1);

    unmount_current_device();

#ifdef VIDEO_ENABLE
    VIDEO_WaitVSync();
#endif

    /*** Shutdown libOGC ***/
    GX_AbortFrame();
    ASND_End();
    u32 bi2Addr = *(volatile u32*)0x800000F4;
    u32 osctxphys = *(volatile u32*)0x800000C0;
    u32 osctxvirt = *(volatile u32*)0x800000D4;
    SYS_ResetSystem(SYS_SHUTDOWN, 0, 0);
    *(volatile u32*)0x800000F4 = bi2Addr;
    *(volatile u32*)0x800000C0 = osctxphys;
    *(volatile u32*)0x800000D4 = osctxvirt;

    /*** Shutdown all threads and exit to this method ***/
    iprintf("IPL BOOTING\n");

// #ifdef VIDEO_ENABLE
//     if (is_dolphin()) {
//         udelay(3 * 1000 * 1000);
//     }
// #endif

    __lwp_thread_stopmultitasking(bs2entry);

    __builtin_unreachable();
}