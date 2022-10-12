// dol loading

#include <malloc.h>

#include "sd.h"
#include "helpers.h"

#include "crc32.h"
#include "print.h"
#include "halt.h"

#include "config.h"

#include "boot/sidestep.h"

#define AUTOLOAD_ARG_INDEX 1
#define AUTOBOOT_ARG_INDEX 2

char *swiss_paths[] = {
    "/BOOT.DOL",
    "/BOOT2.DOL",
    "/IGR.DOL", // used by swiss-gc
    "/IPL.DOL", // used by iplboot
    "/AUTOEXEC.DOL", // used by ActionReplay
};

extern const void _start;
extern const void _edata;

u8 *dol_buf;
u32 *bs2done = (u32*)0x81700000;

bool check_load_program() {
    // check if we can even load files
    if (!is_device_mounted()) return false;

    bool found_file = false;
    for (int f = 0; f < (sizeof(swiss_paths) / sizeof(char *)); f++) {
        char *path = swiss_paths[f];
        int size = get_file_size(path);
        if (size == SD_FAIL) {
            iprintf("Failed to open file: %s\n", path);
            continue;
        } else {
            found_file = true;
            break;
        }
    }

    return found_file;
}

void load_program() {
    // load program
    for (int f = 0; f < (sizeof(swiss_paths) / sizeof(char *)); f++) {
        char *path = swiss_paths[f];

        iprintf("Reading %s\n", path);

        int size = get_file_size(path);
        if (size == SD_FAIL) {
            iprintf("Failed to open file: %s\n", path);
            continue;
        }

        dol_buf = memalign(32, size);
        if (!dol_buf) {
            dol_buf = (u8*)0x81300000;
        }

        if (load_file_buffer(path, dol_buf) != SD_OK) {
            iprintf("Failed to DOL read file: %s\n", path);
            dol_buf = NULL;
        }

        iprintf("Loaded DOL into %p\n", dol_buf);
        break;
    }

    if (dol_buf == NULL) {
        prog_halt("No program loaded!\n");
        return;
    }

    iprintf("Program loaded...\n");
    *bs2done = 0x0;

    iprintf("BOOTING\n");
#ifdef VIDEO_ENABLE
    VIDEO_WaitVSync();
#endif

    // No stack - we need it all
    AR_Init(NULL, 0);    

    static char *argv[] = {
        "boot.dol",
        NULL,
        NULL,
#ifdef GECKO_PRINT_ENABLE
        "Enable Debug=Yes",
#endif
        "BS2Boot=No",
        NULL,
    };

    // temp
    u32 force_autoboot = 1;

    if (force_autoboot) {
        argv[AUTOLOAD_ARG_INDEX] = "Autoload=gcldr:/XD.iso";
        argv[AUTOBOOT_ARG_INDEX] = "AutoBoot=Yes";
    } else {
        argv[AUTOLOAD_ARG_INDEX] = "Autoload=";
        argv[AUTOBOOT_ARG_INDEX] = "AutoBoot=No";
    }

    size_t argc = memlen(argv);
    DOLtoARAM(dol_buf, argc, argv);
}
