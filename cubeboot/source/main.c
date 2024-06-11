#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include <ogcsys.h>
#include <gccore.h>
#include <unistd.h>

#include <asndlib.h>
#include <ogc/lwp_threads.h>
#include <ogc/lwp_watchdog.h>

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

#include "state.h"
#include "settings.h"
#include "logo.h"
#include "pngu/pngu.h"

#include "config.h"
#include "loader.h"
#include "gcm.h"
#include "bnr.h"

#include "gc_dvd.h"

typedef struct {
    struct gcm_disk_header header;
    BNR banner;
    // u8 icon_rgb5[64*64*2];
    char path[128];
} game_asset;

// static u32 prog_entrypoint, prog_dst, prog_src, prog_len;

#define BS2_BASE_ADDR 0x81300000
static void (*bs2entry)(void) = (void(*)(void))BS2_BASE_ADDR;

static char stringBuffer[255];
// ATTRIBUTE_ALIGN(32) u8 current_dol_buf[750 * 1024];
// u32 current_dol_len;

extern const void _start;
extern const void _edata;
extern const void _end;

u32 can_load_dol = 0;

void *xfb;
GXRModeObj *rmode;

void __SYS_PreInit() {
    // if (state->boot_code == 0xCAFEBEEF) return;

    SYS_SetArenaHi((void*)BS2_BASE_ADDR);

    // current_dol_len = &_edata - &_start;
    // memcpy(current_dol_buf, &_start, current_dol_len);
}

void DumpHex(const void* data, size_t size) {
	char ascii[17];
	size_t i, j;
	ascii[16] = '\0';
	for (i = 0; i < size; ++i) {
		iprintf("%02X ", ((unsigned char*)data)[i]);
		if (((unsigned char*)data)[i] >= ' ' && ((unsigned char*)data)[i] <= '~') {
			ascii[i % 16] = ((unsigned char*)data)[i];
		} else {
			ascii[i % 16] = '.';
		}
		if ((i+1) % 8 == 0 || i+1 == size) {
			iprintf(" ");
			if ((i+1) % 16 == 0) {
				iprintf("|  %s \n", ascii);
			} else if (i+1 == size) {
				ascii[(i+1) % 16] = '\0';
				if ((i+1) % 16 <= 8) {
					iprintf(" ");
				}
				for (j = (i+1) % 16; j < 16; ++j) {
					iprintf("   ");
				}
				iprintf("|  %s \n", ascii);
			}
		}
	}
}

int main() {
    u64 startts, endts;

    startts = ticks_to_millisecs(gettime());


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

#ifdef VIDEO_ENABLE
	VIDEO_Init();
    // rmode = VIDEO_GetPreferredMode(NULL);
    // rmode = &TVNtsc480IntDf;
    u32 tvmode = VIDEO_GetCurrentTvMode();
    switch (tvmode) {
        case VI_NTSC:
            rmode = &TVNtsc480IntDf;
            break;
        case VI_PAL:
            rmode = &TVPal528IntDf; // libogc uses TVPal576IntDfScale
            break;
        case VI_MPAL:
            rmode = &TVMpal480IntDf;
            break;
        case VI_EURGB60:
            rmode = &TVEurgb60Hz480IntDf;
            break;
    }
    xfb = MEM_K0_TO_K1(ROUND_UP_1K(get_symbol_value(symshdr, syment, symstringdata, "_patches_end"))); // above patches, below stack
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

#ifdef VIDEO_ENABLE
    iprintf("XFB = %08x [max=%x]\n", (u32)xfb, VIDEO_GetFrameBufferSize(&TVPal576ProgScale));
#endif
    // iprintf("current_dol_len = %d\n", current_dol_len);

#if 0
    // setup config device
    if (mount_available_device() != SD_OK) {
        iprintf("Could not find an inserted SD card\n");
    }

    if (is_device_mounted()) {
        load_settings();
    }

    // check if we have a bootable dol
    if (check_load_program()) {
        can_load_dol = true;
    }
#elif 0
    // dvd setup
    // DVDInit();

    // fake settings
    {
        memset(&settings, 0, sizeof(settings));

        // cube color
        // settings.cube_color = 0x660089;
        settings.cube_color = 0; // default

        // cube logo
        settings.cube_logo = NULL;

        // default program
        settings.default_program = NULL;

        // fallback enable
        settings.fallback_enabled = 0;

        // progressive enable
        // GXRModeObj *temp = VIDEO_GetPreferredMode(NULL);
        // settings.progressive_enabled = (temp->viTVMode & 3) == VI_PROGRESSIVE ? 1 : 0;
        // iprintf("Detected progressive: %d\n", settings.progressive_enabled);
        // settings.progressive_enabled = 1;
        settings.progressive_enabled = 0;

        // preboot delay
        settings.preboot_delay_ms = 0;

        // postboot delay
        settings.postboot_delay_ms = 0;
    }
#else

    iprintf("Loading settings\n");
    load_settings();

#endif

#if 0
    iprintf("Checkup, done=%08x\n", state->boot_code);
    if (state->boot_code == 0xCAFEBEEF) {
        iprintf("He's alive! The doc's alive! He's in the old west, but he's alive!!\n");

#ifdef VIDEO_ENABLE
        VIDEO_WaitVSync();
#endif

        // check for a held button
        int held_max_index = -1;
        u64 held_current_max = 0;
        for (int i = 0; i < MAX_BUTTONS; i++) {
            if (state->held_buttons[i].status == 0) continue;

            u64 ts = state->held_buttons[i].timestamp;
            u32 ms = ticks_to_millisecs(ts);

            if (ms > held_current_max) {
                held_max_index = i;
                held_current_max = ms;
            }

            iprintf("HELD: %s\n", buttons_names[i]);
        }

        // only boot when held > 300ms
        if (held_current_max < 300) {
            held_max_index = -1;
        }

        if (held_max_index != -1) {
            char *button_name = buttons_names[held_max_index];
            iprintf("MAX HELD: %s\n", button_name);
            char buf[64];

            char *filename = settings.boot_buttons[held_max_index];
            if (filename == NULL) {
                filename = &buf[0];
                strcpy(filename, button_name);
                strcat(filename, ".dol");
            }

            if (*button_name == '_') {
                filename = NULL;
            }

            boot_program(filename);
        } else {
            boot_program(NULL);
        }
    }
#endif

//// fun stuff

    // load ipl
    load_ipl();

    // disable progressive on unsupported IPLs
    if (current_bios->version == IPL_NTSC_10) {
        settings.progressive_enabled = FALSE;
    }

//// elf world pt2

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

    bool failed_patching = false;

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
                failed_patching = true;
            }
        }
    }

    if (failed_patching) {
        prog_halt("Failed BIOS Patching relocation\n");
    }

    // local vars
    u8 *image_data = NULL;
    if (settings.cube_logo != NULL) {
        u32 addr = get_symbol_value(symshdr, syment, symstringdata, "cube_text_tex");
        load_logo_texture(settings.cube_logo, (void*)addr);
        extern void DumpHex(const void* data, size_t size);
        DumpHex((void*)addr, 0x100);

        iprintf("img can be found\n");

        // enable image
        set_patch_value(symshdr, syment, symstringdata, "cube_text_loaded", 1);
    }

    // // load current program
    // prog_entrypoint = (u32)&_start;
    // prog_src = (u32)current_dol_buf;
    // prog_dst = (u32)&_start; // (u32*)0x80600000;
    // prog_len = current_dol_len;

    // iprintf("Current program start = %08x\n", prog_entrypoint);

    // // Copy program metadata into place
    // set_patch_value(symshdr, syment, symstringdata, "prog_entrypoint", prog_entrypoint);
    // set_patch_value(symshdr, syment, symstringdata, "prog_src", prog_src);
    // set_patch_value(symshdr, syment, symstringdata, "prog_dst", prog_dst);
    // set_patch_value(symshdr, syment, symstringdata, "prog_len", prog_len);

    // Copy settings into place
    set_patch_value(symshdr, syment, symstringdata, "start_game", can_load_dol);
    set_patch_value(symshdr, syment, symstringdata, "cube_color", settings.cube_color);
    set_patch_value(symshdr, syment, symstringdata, "force_progressive", settings.progressive_enabled);

    set_patch_value(symshdr, syment, symstringdata, "preboot_delay_ms", settings.preboot_delay_ms);
    set_patch_value(symshdr, syment, symstringdata, "postboot_delay_ms", settings.postboot_delay_ms);

    // unmount_current_device();

#ifdef VIDEO_ENABLE
    VIDEO_WaitVSync();
#endif

    // while(1);

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

#ifdef DOLPHIN_DELAY_ENABLE
    // fix for dolphin cache
    udelay(3 * 1000 * 1000);
#endif

    iprintf("DONE\n");

    endts = ticks_to_millisecs(gettime());

    u64 runtime = endts - startts;
    iprintf("Runtime = %llu\n", runtime);

    __lwp_thread_stopmultitasking(bs2entry);

    __builtin_unreachable();
}
