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

#include "dolphin_arq.h"
#include "flippy_sync.h"
#include "dvd_threaded.h"
#include "gc_dvd.h"
#include "games.h"

#include "video.h"
#include "dol.h"
#include "boot.h"

#define CUBE_TEX_WIDTH 84
#define CUBE_TEX_HEIGHT 84

#define STATE_WAIT_LOAD  0x0f // delay after animation
#define STATE_START_GAME 0x10 // play full animation and start game
#define STATE_NO_DISC    0x12 // play full animation before menu
#define STATE_COVER_OPEN 0x13 // force direct to menu

// __attribute_data__ u32 prog_entrypoint;
// __attribute_data__ u32 prog_dst;
// __attribute_data__ u32 prog_src;
// __attribute_data__ u32 prog_len;

__attribute_data__ u32 cube_color = 0;
__attribute_data__ u32 start_passthrough_game = 0;

__attribute_data__ u8 *cube_text_tex = NULL;
__attribute_data__ u32 force_progressive = 0;
__attribute_data__ u32 force_swiss_boot = 0;

// __attribute_data__ static cubeboot_state local_state;
// __attribute_data__ static cubeboot_state *global_state = (cubeboot_state*)0x81700000;

// used if we are switching to 60Hz on a PAL IPL
__attribute_data__ static int fix_pal_ntsc = 0;

// used for optional delays
__attribute_data__ u32 preboot_delay_ms = 0;
__attribute_data__ u32 postboot_delay_ms = 0;
__attribute_data__ u64 completed_time = 0;

// used to start game
__attribute_reloc__ u32 (*PADSync)();
__attribute_reloc__ void (*__OSStopAudioSystem)();
// __attribute_reloc__ void (*run)(register void* entry_point, register u32 clear_start, register u32 clear_size);

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

// start
__attribute_data__ gm_file_entry_t boot_entry;
__attribute_data__ gm_file_entry_t second_boot_entry;

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
        OSReport("PTR = %08x\n", (u32)cube_text_tex);
        OSReport("ORIG_PTR_PARTS = %08x, %08x\n", (u32)gc_text_tex, gc_text_tex->offset);

        if (cube_text_tex != NULL) {
            s32 desired_offset = gc_text_tex->offset;
            if ((u32)gc_text_tex > (u32)cube_text_tex) {
                desired_offset = -1 * (s32)((u32)gc_text_tex - (u32)cube_text_tex);
            } else {
                desired_offset = (s32)((u32)cube_text_tex - (u32)gc_text_tex);
            }

            OSReport("DESIRED = %d\n", desired_offset);

            // change the texture format
            gc_text_tex->format = GX_TF_RGBA8;
            gc_text_tex->offset = desired_offset;
        }
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
    DI_InitQueue();
    dolphin_ARAMInit();
    orig_thread_init();
    if (!start_passthrough_game) {
        gm_init_heap();
        gm_init_thread();
        gm_start_thread("/");
    }
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
    const int fps = rmode->viTVMode >> 2 == VI_NTSC ? 60 : 50;
    const int total_frames = preboot_delay_ms / fps;
    for (int i = 0; i < total_frames; i++) {
        VIWaitForRetrace();
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
    frame_count++;
    if (!completed_time && cube_state->cube_anim_done) {
        OSReport("FINISHED (%d)\n", frame_count);
        completed_time = gettime();
    }

    if (start_passthrough_game) {
        if (postboot_delay_ms) {
            u64 elapsed = diff_msec(completed_time, gettime());
            if (completed_time > 0 && elapsed > postboot_delay_ms) {
                return STATE_START_GAME;
            } else {
                return STATE_WAIT_LOAD;
            }
        }
        return STATE_START_GAME;
    }

    // this helps the start menu show correctly
    if (*main_menu_id >= 3) {
        return STATE_START_GAME;
    }

#ifdef TEST_SKIP_ANIMATION
    return STATE_COVER_OPEN;
#endif

    // TODO: allow the user to decide if they want to logo to play
    return STATE_NO_DISC;
}

__attribute_used__ void bs2start() {
    OSReport("DONE\n");
    if (!start_passthrough_game) {
         gm_deinit_thread();
    }

    // memcpy(global_state, &local_state, sizeof(cubeboot_state));
    // global_state->boot_code = 0xCAFEBEEF;

    // no IPL code should be running after this point

    while (!PADSync());
    OSDisableInterrupts();
    __OSStopAudioSystem();

    u32 start_addr = 0x80100000;
    u32 end_addr = 0x81600000;
    u32 len = end_addr - start_addr;

    memset((void*)start_addr, 0, len); // cleanup
    DCFlushRange((void*)start_addr, len);
    ICInvalidateRange((void*)start_addr, len);

    // read boot info into lowmem
    struct dolphin_lowmem *lowmem = (struct dolphin_lowmem*)0x80000000;
    lowmem->a_boot_magic = 0x0D15EA5E;
    lowmem->a_version = 0x00000001;
    lowmem->b_physical_memory_size = 0x01800000;

    // Passthrough mode
    if (start_passthrough_game) {
        dvd_custom_bypass_enter();
        udelay(10 * 1000);

        int ret = dvd_read_id();
        int err = dvd_get_error();
        if (ret != 0 || err != 0) {
            custom_OSReport("Failed to read disc ID\n");
            dvd_custom_bypass_exit();
            udelay(10 * 1000);

            load_stub(); // exit to loader again
            u32 *sig = (u32*)0x80001804;
            if ((*sig++ == 0x53545542 || *sig++ == 0x53545542) && *sig == 0x48415858) {
                static void (*reload)(void) = (void(*)(void))0x80001800;
                run(reload);
            }
        }

        chainload_boot_game(NULL, true);
    }

    char *boot_path = boot_entry.path;
    if (boot_entry.type == GM_FILE_TYPE_PROGRAM) {
        custom_OSReport("Booting DOL\n");
        load_stub();

        dol_info_t info = load_dol(boot_path, false);
        run(info.entrypoint);
    } else {
        custom_OSReport("Booting ISO\n");

        prepare_game_lowmem(boot_path);
        char *game_code = lowmem->b_disk_info.game_code;
        bool is_tonyhawk_proskater4 = (game_code[0] == 'G' && game_code[1] == 'T' && game_code[2] == '4');
        bool is_xeno_crisis = (game_code[0] == 'G' && game_code[1] == 'C' && game_code[2] == 'R');
        bool use_swiss = (is_tonyhawk_proskater4 || force_swiss_boot) && !is_xeno_crisis;

        if (use_swiss) {
            custom_OSReport("Booting ISO (swiss chainload)\n");

            dol_info_t info = load_dol("/swiss-gc.dol", true);

            char *argz = (void*)info.max_addr + 32;
            int argz_len = 0;

            char autoload_arg[256];
            strcpy(autoload_arg, "Autoload=fldrv:");
            strcat(autoload_arg, boot_path);

            const char *arg_list[] = {
                "swiss-novideo.dol", // this causes Swiss to black the screen
                autoload_arg,
                "AutoBoot=Yes",
                "IGRType=Reboot",
                "BS2Boot=No",
                "Prefer Clean Boot=No",
                NULL
            };

            int arg_count = 0;
            while(arg_list[arg_count] != NULL) {
                const char *arg = arg_list[arg_count];
                int arg_len = strlen(arg) + 1;
                memcpy(argz + argz_len, arg, arg_len);
                argz_len += arg_len;
                arg_count++;
            }

            struct __argv *args = (void*)(info.entrypoint + 8);
            args->argvMagic = ARGV_MAGIC;
            args->commandLine = argz;
            args->length = argz_len;

            DCFlushRange(argz, argz_len);
            DCFlushRange(args, sizeof(struct __argv));

            run(info.entrypoint);
        } else {
            custom_OSReport("Booting ISO (custom apploader)\n");
            chainload_boot_game(&boot_entry, false);
        }
    }

    __builtin_unreachable();
}

// unused
void _start() {}
