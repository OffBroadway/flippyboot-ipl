#include "dolphin_dvd.h"

#define assert(...)
// #include <assert.h>
#include <malloc.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <gcutil.h>
#include <ogc/system.h>
#include "picolibc.h" // for strcasecmp

#include "reloc.h"
#include "flippy_sync.h"
#include "dvd_threaded.h"
#include "bnr_offsets.h"

#include "dolphin_os.h"

#define DVD_FS_DUMP 1

#define GET_OFFSET(o) ((u32)((o[0] << 16) | (o[1] << 8) | o[2]))

#define ENTRY_IS_DIR(i) (entry_table[i].filetype == T_DIR)
#define FILE_POSITION(i) (entry_table[i].addr)
#define FILE_LENGTH(i) (entry_table[i].len)

typedef struct {
    u32 offset;
    u32 length;
} bnr_info_t;

static bnr_info_t get_banner_offset_slow(DiskHeader *header, uint32_t fd) {
    u32 size = OSRoundUp32B(header->FSTSize);
    u32 offset = header->FSTOffset;
    u8 *fst = (void*)0x81700000;

    // read FST
    DI_Read(fst, size, offset, fd);

    FSTEntry *entry_table = (FSTEntry*)fst;
    u32 total_entries = entry_table[0].len;
    char *string_table = (char*)&(entry_table[total_entries]);
    if ((u32)string_table < 0x81700000 || (u32)string_table > 0x81800000) {
        OSReport("ERROR: String table is out of bounds: %08x\n", (u32)string_table);
        return (bnr_info_t) {
            .offset = 0,
            .length = 0,
        };
    }

#ifdef PRINT_READDIR_FILES
    OSReport("FST contains %u\n", total_entries);
    OSYieldThread();
#endif

    FSTEntry* p = entry_table;
    for (u32 i = 1; i < total_entries; ++i) { //Start @ 1 to skip FST header
        u32 string_offset = GET_OFFSET(p[i].offset);
        char *string = (char*)((u32)string_table + string_offset);
        // OSReport("String table = %08x, String offset = %08x\n", (u32)string_table, string_offset);
        // OSReport("FST (0x%08x) entry: %s\n", FILE_POSITION(i), string);
        if (entry_table[i].filetype == T_FILE && strcasecmp(string, "opening.bnr") == 0) {
#ifdef PRINT_READDIR_FILES
            OSReport("FST (0x%08x) entry: %s\n", FILE_POSITION(i), string);
#endif

            OSYieldThread(); // allow rescheduling
            return (bnr_info_t) {
                .offset = FILE_POSITION(i),
                .length = FILE_LENGTH(i),
            };
        }
    }

    OSYieldThread(); // allow rescheduling
    return (bnr_info_t) {
        .offset = 0,
        .length = 0,
    };
}

// Get the BNR offset on the disc
dolphin_game_into_t get_game_info(char *game_path) {
    __attribute__((aligned(32))) static u32 small_buf[8]; // for BNR reads

    const uint8_t flags = IPC_FILE_FLAG_DISABLECACHE | IPC_FILE_FLAG_DISABLESPEEDEMU;
    int ret = dvd_custom_open(game_path, T_FILE, flags);
    if (ret != 0) {
        OSReport("ERROR: Failed to open %s\n", game_path);
        return (dolphin_game_into_t) { .valid = false };
    }

    // OSReport("DEBUG: file opened\n");

    file_status_t *status = dvd_custom_status();
    if (status->result != 0) {
        OSReport("ERROR: Failed to get status for %s\n", game_path);

        dvd_custom_close(status->fd);
        return (dolphin_game_into_t) { .valid = false };
    }

    __attribute__((aligned(32))) static DiskHeader header;
    DI_Read(&header, sizeof(DiskHeader), 0, status->fd); //Read in the disc header

    // OSReport("DEBUG: disk header loaded\n");

    u32 fast_bnr_offset = get_banner_offset_fast(&header);
    // OSReport("DEBUG: Fast BNR offset: %08x\n", fast_bnr_offset);
    if (fast_bnr_offset != 0) {
        DI_Read(small_buf, 32, fast_bnr_offset, status->fd); //Read in the banner data

        u32 magic = small_buf[0];
        if (magic == BANNER_MAGIC_1 || magic == BANNER_MAGIC_2) {
            dolphin_game_into_t info;
            info.valid = true;
            info.bnr_type = magic == BANNER_MAGIC_2; // BANNER_MULTI_LANG
            info.bnr_offset = fast_bnr_offset;
            memcpy(info.game_id, &header, 6);
            info.disc_num = header.DiscID;
            info.disc_ver = header.Version;

            dvd_custom_close(status->fd);
            return info;
        }
    }

    // OSReport("DEBUG: loading FST from disk\n");

    if (header.FSTSize > 0x100000) {
        OSReport("ERROR: FST size is too large: %08x\n", header.FSTSize);
        dvd_custom_close(status->fd);
        return (dolphin_game_into_t) { .valid = false };
    }

    // If we didn't find the banner in the fast location, try the FST
    bnr_info_t bnr_info = get_banner_offset_slow(&header, status->fd);
    if (bnr_info.offset != 0) {
        DI_Read(small_buf, 32, bnr_info.offset, status->fd); //Read in the banner data

        u32 magic = small_buf[0];
        if (magic == BANNER_MAGIC_1 || magic == BANNER_MAGIC_2) {
            dolphin_game_into_t info;
            info.valid = true;
            info.bnr_type = magic == BANNER_MAGIC_2; // BANNER_MULTI_LANG
            info.bnr_offset = bnr_info.offset;
            memcpy(info.game_id, &header, 6);
            info.disc_num = header.DiscID;
            info.disc_ver = header.Version;

            dvd_custom_close(status->fd);
            return info;
        }
    }

    // OSReport("DEBUG: FST was loaded\n");

    // invalid file
    dvd_custom_close(status->fd);
    return (dolphin_game_into_t) { .valid = false };
}
