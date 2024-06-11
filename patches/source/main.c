#include <math.h>
#include <stddef.h>

#include "picolibc.h"
#include "structs.h"
#include "attr.h"
#include "util.h"
#include "os.h"

#include "usbgecko.h"
#include "state.h"
#include "time.h"

#include "reloc.h"
#include "menu.h"

#include "gc_dvd.h"
#include "filebrowser.h"

#include "video.h"
#include "dol.h"

#define CUBE_TEX_WIDTH 84
#define CUBE_TEX_HEIGHT 84

#define STATE_WAIT_LOAD  0x0f // delay after animation
#define STATE_START_GAME 0x10 // play full animation and start game
#define STATE_NO_DISC    0x12 // play full animation before menu
#define STATE_COVER_OPEN 0x13 // force direct to menu

#define TEST_ONLY_force_boot_menu 1
#define TEST_ONLY_skip_animation 1

__attribute_data__ u32 prog_entrypoint;
__attribute_data__ u32 prog_dst;
__attribute_data__ u32 prog_src;
__attribute_data__ u32 prog_len;

__attribute_data__ u32 cube_color = 0;
__attribute_data__ u32 start_game = 0;

__attribute_data__ u8 cube_text_tex[0xdc00];
__attribute_data__ u8 cube_text_loaded;
__attribute_data__ u32 force_progressive;

// __attribute_data__ static cubeboot_state local_state;
// __attribute_data__ static cubeboot_state *global_state = (cubeboot_state*)0x81700000;

// used if we are switching to 60Hz on a PAL IPL
__attribute_data__ static int fix_pal_ntsc = 0;

// used for optional delays
__attribute_data__ u32 preboot_delay_ms;
__attribute_data__ u32 postboot_delay_ms;
__attribute_data__ u64 completed_time;

// used to start game
__attribute_reloc__ u32 (*PADSync)();
__attribute_reloc__ void (*__OSStopAudioSystem)();
__attribute_reloc__ void (*run)(register void* entry_point, register u32 clear_start, register u32 clear_size);

// for setup
__attribute_reloc__ void (*orig_thread_init)();
__attribute_reloc__ void (*menu_init)();
__attribute_reloc__ void (*main)();

__attribute_reloc__ model *bg_outer_model;
__attribute_reloc__ model *bg_inner_model;
__attribute_reloc__ model *gc_text_model;
__attribute_reloc__ model *logo_model;
__attribute_reloc__ model *cube_model;

// locals
__attribute_data__ static GXColorS10 color_cube;
__attribute_data__ static GXColorS10 color_cube_low;
__attribute_data__ static GXColorS10 color_bg_inner;
__attribute_data__ static GXColorS10 color_bg_outer_0;
__attribute_data__ static GXColorS10 color_bg_outer_1;

// for filebrowser
// __attribute_data__ u32 stop_loading_games = 0;

__attribute_used__ void mod_cube_colors() {
    if (cube_color == 0) {
        OSReport("Using default colors\n");
        return;
    }

    rgb_color target_color;
    target_color.color = (cube_color << 8) | 0xFF;

    // TODO: the HSL calculations do not render good results for darker inputs, I still need to tune SAT/LUM scaling
    // tough colors: 252850 A18594 763C28

    u32 target_hsl = GRRLIB_RGBToHSL(target_color.color);
    u32 target_hue = H(target_hsl);
    u32 target_sat = S(target_hsl);
    u32 target_lum = L(target_hsl);
    float sat_mult = (float)target_sat / 40.0; //* 1.5;
    if (sat_mult > 2.0) sat_mult = sat_mult * 0.5;
    if (sat_mult > 1.5) sat_mult = sat_mult * 0.5;
    // sat_mult = 0.35; // temp for light colors
    // sat_mult = 1.0;
    float lum_mult = (float)target_lum / 135.0; //* 0.75;
    if (lum_mult < 0.75) lum_mult = lum_mult * 1.5;
    OSReport("SAT MULT = %f\n", sat_mult);
    OSReport("LUM MULT = %f\n", lum_mult);
    OSReport("TARGET_HI: %02x%02x%02x = (%d, %d, %d)\n", target_color.parts.r, target_color.parts.g, target_color.parts.b, H(target_hsl), S(target_hsl), L(target_hsl));

    u8 low_hue = (u8)round((float)H(target_hsl) * 1.02857143);
    u8 low_sat = (u8)round((float)S(target_hsl) * 1.09482759);
    u8 low_lum = (u8)round((float)L(target_hsl) * 0.296296296);
    u32 low_hsl = HSLA(low_hue, low_sat, low_lum, A(target_hsl));
    rgb_color target_low;
    target_low.color = GRRLIB_HSLToRGB(low_hsl);
    OSReport("TARGET_LO: %02x%02x%02x = (%d, %d, %d)\n", target_low.parts.r, target_low.parts.g, target_low.parts.b, H(low_hsl), S(low_hsl), L(low_hsl));

    u8 bg_inner_hue = (u8)round((float)H(target_hsl) * 1.00574712);
    u8 bg_inner_sat = (u8)round((float)S(target_hsl) * 0.95867768);
    u8 bg_inner_lum = (u8)round((float)L(target_hsl) * 0.9);
    u32 bg_inner_hsl = HSLA(bg_inner_hue, bg_inner_sat, bg_inner_lum, bg_inner_model->data->mat[0].tev_color[0]->a);
    rgb_color bg_inner;
    bg_inner.color = GRRLIB_HSLToRGB(bg_inner_hsl);

    u8 bg_outer_hue_0 = (u8)round((float)H(target_hsl) * 1.02941176);
    u8 bg_outer_sat_0 = 0xFF;
    u8 bg_outer_lum_0 = (u8)round((float)L(target_hsl) * 1.31111111);
    u32 bg_outer_hsl_0 = HSLA(bg_outer_hue_0, bg_outer_sat_0, bg_outer_lum_0, bg_outer_model->data->mat[0].tev_color[0]->a);
    rgb_color bg_outer_0;
    bg_outer_0.color = GRRLIB_HSLToRGB(bg_outer_hsl_0);

    u8 bg_outer_hue_1 = (u8)round((float)H(target_hsl) * 1.07428571);
    u8 bg_outer_sat_1 = (u8)round((float)S(target_hsl) * 0.61206896);
    u8 bg_outer_lum_1 = (u8)round((float)L(target_hsl) * 0.92592592);
    u32 bg_outer_hsl_1 = HSLA(bg_outer_hue_1, bg_outer_sat_1, bg_outer_lum_1, bg_outer_model->data->mat[1].tev_color[0]->a);
    rgb_color bg_outer_1;
    bg_outer_1.color = GRRLIB_HSLToRGB(bg_outer_hsl_1);

    // logo

    DUMP_COLOR(logo_model->data->mat[1].tev_color[0]);
    DUMP_COLOR(logo_model->data->mat[1].tev_color[1]);
    DUMP_COLOR(logo_model->data->mat[2].tev_color[2]);

    tex_data *base = logo_model->data->tex->dat;
    for (int i = 0; i < 8; i++) {
        tex_data *p = base + i;
        if (p->width != 84) break; // early exit
        u16 wd = p->width;
        u16 ht = p->height;
        void *img_ptr = (void*)((u8*)base + p->offset + (i * 0x20));
        OSReport("FOUND TEX: %dx%d @ %p\n", wd, ht, img_ptr);

        // change hue of cube textures
        for (int y = 0; y < ht; y++) {
            for (int x = 0; x < wd; x++) {
                u32 color = GRRLIB_GetPixelFromtexImg(x, y, img_ptr, wd);

                // hsl
                {
                    u32 hsl = GRRLIB_RGBToHSL(color);
                    u32 sat = round((float)L(hsl) * sat_mult);
                    if (sat > 0xFF) sat = 0xFF;
                    u32 lum = round((float)L(hsl) * lum_mult);
                    if (lum > 0xFF) lum = 0xFF;
                    color = GRRLIB_HSLToRGB(HSLA(target_hue, (u8)sat, (u8)lum, A(hsl)));
                }

                GRRLIB_SetPixelTotexImg(x, y, img_ptr, wd, color);
            }
        }

        uint32_t buffer_size = (wd * ht) << 2;
        DCFlushRange(img_ptr, buffer_size);
    }

    // copy over colors

    copy_color(target_color, &cube_state->color);
    copy_color10(target_color, &color_cube);
    copy_color10(target_low, &color_cube_low);

    copy_color10(bg_inner, &color_bg_inner);
    copy_color10(bg_outer_0, &color_bg_outer_0);
    copy_color10(bg_outer_1, &color_bg_outer_1);

    cube_model->data->mat[0].tev_color[0] = &color_cube;
    cube_model->data->mat[0].tev_color[1] = &color_cube_low;

    logo_model->data->mat[0].tev_color[0] = &color_cube_low; // TODO: use different shades
    logo_model->data->mat[0].tev_color[1] = &color_cube_low; // TODO: <-
    logo_model->data->mat[1].tev_color[0] = &color_cube_low; // TODO: <-
    logo_model->data->mat[1].tev_color[1] = &color_cube_low; // TODO: <-
    logo_model->data->mat[2].tev_color[0] = &color_cube_low; // TODO: <-
    logo_model->data->mat[2].tev_color[1] = &color_cube_low; // TODO: <-

    bg_inner_model->data->mat[0].tev_color[0] = &color_bg_inner;
    bg_inner_model->data->mat[1].tev_color[0] = &color_bg_inner;

    bg_outer_model->data->mat[0].tev_color[0] = &color_bg_outer_0;
    bg_outer_model->data->mat[1].tev_color[0] = &color_bg_outer_1;

    return;
}

void DumpHex(const void* data, size_t size) {
	char ascii[17];
	size_t i, j;
	ascii[16] = '\0';
	for (i = 0; i < size; ++i) {
		OSReport("%02X ", ((unsigned char*)data)[i]);
		if (((unsigned char*)data)[i] >= ' ' && ((unsigned char*)data)[i] <= '~') {
			ascii[i % 16] = ((unsigned char*)data)[i];
		} else {
			ascii[i % 16] = '.';
		}
		if ((i+1) % 8 == 0 || i+1 == size) {
			OSReport(" ");
			if ((i+1) % 16 == 0) {
				OSReport("|  %s \n", ascii);
			} else if (i+1 == size) {
				ascii[(i+1) % 16] = '\0';
				if ((i+1) % 16 <= 8) {
					OSReport(" ");
				}
				for (j = (i+1) % 16; j < 16; ++j) {
					OSReport("   ");
				}
				OSReport("|  %s \n", ascii);
			}
		}
	}
}


__attribute_used__ void mod_cube_text() {
        tex_data *gc_text_tex = gc_text_model->data->tex->dat;

        u16 wd = gc_text_tex->width;
        u16 ht = gc_text_tex->height;
        void *img_ptr = (void*)((u8*)gc_text_tex + gc_text_tex->offset);
        u32 img_size = wd * ht;

#ifndef DEBUG
        (void)img_ptr;
        (void)img_size;
#endif

        OSReport("CUBE TEXT TEX: %dx%d[%d] (type=%d) @ %p\n", wd, ht, img_size, gc_text_tex->format, img_ptr);
        memset(img_ptr, 0x80, 0x3700);
        OSReport("PTR = %08x\n", (u32)cube_text_tex);
        DumpHex(&cube_text_tex[0], 0x100);

        // {
        //     OSReport("ZEROOOO\n");
        //     memset(cube_text_tex, 0xFF, 0xdc00);
        // }

        OSReport("ORIG_PTR_PARTS = %08x, %08x\n", (u32)gc_text_tex, gc_text_tex->offset);

        // if (cube_text_loaded) {
            u32 cube_text_tex_ptr = (u32)&cube_text_tex[0];
            s32 desired_offset = gc_text_tex->offset;
            if ((u32)gc_text_tex > (u32)cube_text_tex_ptr) {
                desired_offset = -1 * (s32)((u32)gc_text_tex - (u32)cube_text_tex_ptr);
            } else {
                desired_offset = (s32)((u32)cube_text_tex_ptr - (u32)gc_text_tex);
            }

            OSReport("DESIRED = %d\n", desired_offset);
            DCFlushRange(cube_text_tex, 0xdc00);

            // change the texture format
            gc_text_tex->format = GX_TF_RGBA8;
            gc_text_tex->offset = desired_offset;
            DCFlushRange(gc_text_tex, sizeof(tex_data));
        // }

        // memset(gc_text_tex, 0, sizeof(tex_data));
        // DCFlushRange(gc_text_tex, sizeof(tex_data));
}


__attribute_used__ void mod_cube_anim() {
    if (fix_pal_ntsc) {
        cube_state->cube_side_frames = 10;
        cube_state->cube_corner_frames = 16;
        cube_state->fall_anim_frames = 5;
        cube_state->fall_delay_frames = 16;
        cube_state->up_anim_frames = 18;
        cube_state->up_delay_frames = 7;
        cube_state->move_anim_frames = 10;
        cube_state->move_delay_frames = 20;
        cube_state->done_delay_frames = 40;
        cube_state->logo_hold_frames = 60;
        cube_state->logo_delay_frames = 5;
        cube_state->audio_cue_frames_b = 7;
        cube_state->audio_cue_frames_c = 6;
    }
}

__attribute_used__ void pre_thread_init() {
    orig_thread_init();
    start_file_enum();
}

__attribute_used__ void pre_menu_init(int unk) {
    menu_init(unk);

    // change default menu
    *prev_menu_id = MENU_GAMESELECT_TRANSITION_ID;
    *cur_menu_id = MENU_GAMESELECT_ID;

    custom_gameselect_init();

    mod_cube_colors();
    mod_cube_text();
    mod_cube_anim();

    // delay before boot animation (to wait for GCVideo)
    if (preboot_delay_ms) {
        udelay(preboot_delay_ms * 1000);
    }
}

__attribute_used__ void pre_main() {
    OSReport("RUNNING BEFORE MAIN\n");

    OSReport("efbHeight = %u\n", rmode->efbHeight);
    OSReport("xfbHeight = %u\n", rmode->xfbHeight);

    if (force_progressive) {
        OSReport("Patching video mode to Progressive Scan\n");
        fix_pal_ntsc = rmode->viTVMode >> 2 != VI_NTSC;
        if (fix_pal_ntsc) {
            rmode->fbWidth = 592;
            rmode->efbHeight = 226;
            rmode->xfbHeight = 448;
            rmode->viXOrigin = 40;
            rmode->viYOrigin = 16;
            rmode->viWidth = 640;
            rmode->viHeight = 448;
        }

        // sample points arranged in increasing Y order
        u8  sample_pattern[12][2] = {
            {6,6},{6,6},{6,6},  // pix 0, 3 sample points, 1/12 units, 4 bits each
            {6,6},{6,6},{6,6},  // pix 1
            {6,6},{6,6},{6,6},  // pix 2
            {6,6},{6,6},{6,6}   // pix 3
        };
        memcpy(&rmode->sample_pattern[0][0], &sample_pattern[0][0], (12*2));

        rmode->viTVMode = VI_TVMODE_NTSC_PROG;
        rmode->xfbMode = VI_XFBMODE_SF;
        // rmode->aa = FALSE; // breaks the IPL??
        // rmode->field_rendering = TRUE;

        rmode->vfilter[0] = 0;
        rmode->vfilter[1] = 0;
        rmode->vfilter[2] = 21;
        rmode->vfilter[3] = 22;
        rmode->vfilter[4] = 21;
        rmode->vfilter[5] = 0;
        rmode->vfilter[6] = 0;
    }

    // // can't boot dol
    // if (!start_game) {
    //     cube_color = 0x4A412A; // or 0x0000FF
    // }

    // OSReport("LOADCMD %x, %x, %x, %x\n", prog_entrypoint, prog_dst, prog_src, prog_len);
    // memmove((void*)prog_dst, (void*)prog_src, prog_len);

    main();

    __builtin_unreachable();
}

__attribute_used__ u32 get_tvmode() {
    return rmode->viTVMode;
}

__attribute_data__ int frame_count = 0;
__attribute_used__ u32 bs2tick() {
    // // TODO: move this check to PADRead in main loop
    // if (pad_status->pad.button != local_state.last_buttons) {
    //     for (int i = 0; i < MAX_BUTTONS; i++) {
    //         u16 bitmask = 1 << i;
    //         u16 pressed = (pad_status->pad.button & bitmask) >> i;

    //         // button changed state
    //         if (local_state.held_buttons[i].status != pressed) {
    //             if (pressed) {
    //                 local_state.held_buttons[i].timestamp = gettime();
    //             } else {
    //                 local_state.held_buttons[i].timestamp = 0;
    //             }
    //         }

    //         local_state.held_buttons[i].status = pressed;
    //     }
    // }
    // local_state.last_buttons = pad_status->pad.button;

    frame_count++;
    if (!completed_time && cube_state->cube_anim_done) {
        OSReport("FINISHED (%d)\n", frame_count);
        DumpHex(&cube_text_tex[0], 0x100);
        completed_time = gettime();
    }

    // this helps the start menu show correctly
    if (*main_menu_id >= 3) {
        return STATE_START_GAME;
    }

    // if (start_game && !TEST_ONLY_force_boot_menu) {
    //     if (postboot_delay_ms) {
    //         u64 elapsed = diff_msec(completed_time, gettime());
    //         if (completed_time > 0 && elapsed > postboot_delay_ms) {
    //             return STATE_START_GAME;
    //         } else {
    //             return STATE_WAIT_LOAD;
    //         }
    //     }
    //     return STATE_START_GAME;
    // }

#ifdef TEST_SKIP_ANIMATION
    if (TEST_ONLY_skip_animation) {
        return STATE_COVER_OPEN;
    }
#endif

    // TODO: allow the user to decide if they want to logo to play
    return STATE_WAIT_LOAD;
    // return STATE_COVER_OPEN;
    // return STATE_NO_DISC;
}

__attribute__((aligned(32))) static u8 apploader_buf[0x20];
void* LoadGame_Apploader() {
    // variables
    int err;
    void *buffer = &apploader_buf[0];
    void (*app_init)(void (*report)(const char *fmt, ...));
    int (*app_main)(void **dst, int *size, int *offset);
    void *(*app_final)();
    void (*app_entry)(void(**init)(void (*report)(const char *fmt, ...)), int (**main)(void**,int*,int*), void *(**final)());

    // // disable interrupts
    // u32 msr;
    // msr = GetMSR();
    // msr &= ~0x8000;
    // msr |= 0x2002;
    // SetMSR(msr);

    // start disc drive & read apploader
    err = dvd_read(buffer,0x20,0x2440);
    if (err) {
        OSReport("Could not load apploader header\n");
        while(1);
    }
    err = dvd_read((void*)0x81200000,((*(unsigned long*)((u32)buffer+0x14)) + 31) &~31,0x2460);
    if (err) {
        OSReport("Could not load apploader data\n");
        while(1);
    }

    // run apploader
    app_entry = (void (*)(void(**)(void(*)(const char*,...)),int(**)(void**,int*,int*),void*(**)()))(*(unsigned long*)((u32)buffer + 0x10));
    app_entry(&app_init,&app_main,&app_final);
    app_init((void(*)(const char*,...))&custom_OSReport);
    for (;;)
    {
        void *dst = 0;
        int len = 0,offset = 0;
        int res = app_main(&dst,&len,&offset);
		custom_OSReport("res = %d\n", res);
        if (!res) break;
        err = dvd_read(dst,len,offset);
        if (err) {
            custom_OSReport("Apploader read failed\n");
            while(1);
        }
        DCFlushRange(dst,len);
    }
	custom_OSReport("GOT ENTRY\n");

    void* entrypoint = app_final();
    custom_OSReport("THIS ENTRY, %p\n", entrypoint);
    return entrypoint;
}

extern char boot_path[];

__attribute__((aligned(32))) static DOLHEADER dol_hdr;
__attribute_used__ void bs2start() {
    OSReport("DONE\n");

    // iwrite32((u32)&stop_loading_games, 1);
    // sync_after_write(&stop_loading_games, 4);

    OSReport("Waiting for file enum\n");
    OSLockMutex(game_enum_mutex);
    OSYieldThread();
    udelay(100 * 1000); // probably overkill??
    OSYieldThread();

    // memcpy(global_state, &local_state, sizeof(cubeboot_state));
    // global_state->boot_code = 0xCAFEBEEF;

    // if (prog_entrypoint == 0) {
    //     OSReport("HALT: No program\n");
    //     while(1); // block forever
    // }

    // // TODO: remove this after testing on hardware
    OSReport("we are about to look at %08x\n", (u32)boot_path);
    // udelay(100 * 1000);

    OSReport("we are about to open %s\n", boot_path);
    // udelay(100 * 1000);

    int ret = dvd_custom_open(IPC_DEVICE_SD, boot_path, FILE_ENTRY_TYPE_FILE, 0);
    OSReport("OPEN ret: %08x\n", ret);
    (void)ret;

    while (!PADSync());
    OSDisableInterrupts();
    __OSStopAudioSystem();

    u32 start_addr = 0x80100000;
    u32 end_addr = 0x81300000;
    u32 len = end_addr - start_addr;

    memset((void*)start_addr, 0, len); // cleanup
    DCFlushRange((void*)start_addr, len);
    ICInvalidateRange((void*)start_addr, len);

    // read boot info into lowmem
    struct dolphin_lowmem *lowmem = (struct dolphin_lowmem*)0x80000000;
    lowmem->a_boot_magic = 0x0D15EA5E;
    lowmem->a_version = 0x00000001;
    lowmem->b_physical_memory_size = 0x01800000;

    // only load the apploader if the boot path is not a .dol file
    extern int strncmpci(const char * str1, const char * str2, size_t num);
    if (strncmpci(boot_path + strlen(boot_path) - 4, ".dol", 4) == 0 || strncmpci(boot_path + strlen(boot_path) - 8, ".dol+cli", 8) == 0) {
        custom_OSReport("Booting DOL\n");

        // file_status_t file_status;
        // dvd_custom_status(IPC_DEVICE_SD, &file_status);
        // u64 dol_size = file_status.fsize;
        // custom_OSReport("DOL size: %x\n", dol_size);

        DOLHEADER *hdr = &dol_hdr;
        dvd_read(hdr, sizeof(DOLHEADER), 0);

        // Inspect text sections to see if what we found lies in here
        for (int i = 0; i < MAXTEXTSECTION; i++) {
            if (hdr->textAddress[i] && hdr->textLength[i]) {
                dvd_read((void*)hdr->textAddress[i], hdr->textLength[i], hdr->textOffset[i]);
            }
        }

        // Inspect data sections (shouldn't really need to unless someone was sneaky..)
        for (int i = 0; i < MAXDATASECTION; i++) {
            if (hdr->dataAddress[i] && hdr->dataLength[i]) {
                dvd_read((void*)hdr->dataAddress[i], hdr->dataLength[i], hdr->dataOffset[i]);
            }
        }

        custom_OSReport("Copy done...\n");

        // Clear BSS
        // TODO: check if this overlaps with IPL
        memset((void*)hdr->bssAddress, 0, hdr->bssLength);

        prog_entrypoint = hdr->entryPoint;
    } else {
        dvd_read(&lowmem->b_disk_info, 0x20, 0);

        prog_entrypoint = (u32)LoadGame_Apploader();

        struct gcm_disk_header_info *bi2 = lowmem->a_bi2;

        OSReport("BI2: %08x\n", bi2);
        OSReport("Country: %x\n", bi2->country_code);

        // game id
        OSReport("Game ID: %c%c%c%c\n", lowmem->b_disk_info.game_code[0], lowmem->b_disk_info.game_code[1], lowmem->b_disk_info.game_code[2], lowmem->b_disk_info.game_code[3]);

        if (bi2->country_code == COUNTRY_EUR) {
            OSReport("PAL game detected\n");
            ogc__VIInit(VI_TVMODE_PAL_INT);

            // set video mode PAL
            u32 mode = rmode->viTVMode >> 2;
            if (mode == VI_MPAL) {
                lowmem->tv_mode = 5;
            } else {
                lowmem->tv_mode = 1;
            }
        } else {
            OSReport("NTSC game detected\n");
            ogc__VIInit(VI_TVMODE_NTSC_INT);

            lowmem->tv_mode = 0;
        }
    }

    custom_OSReport("booting... (%08x)\n", prog_entrypoint);

    // TODO: copy a DCZeroRange routine to 0x81200000 instead (like sidestep)
    void (*entry)(void) = (void(*)(void))prog_entrypoint;
    run(entry, 0x81300000, 0x20000);

    __builtin_unreachable();
}

// unused
void _start() {}
