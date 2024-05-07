#include <math.h>
#include "picolibc.h"
#include "structs.h"

#include "util.h"
#include "reloc.h"
#include "attr.h"

#include <ogc/machine/processor.h>

#include "usbgecko.h"
#include "menu.h"

// TODO: this is all zeros except for one BNRDesc, so replace it with a sparse version
#include "../build/default_opening_bin.h"
#include "../../cubeboot/include/gcm.h"
#include "../../cubeboot/include/bnr.h"

// for setup
__attribute_reloc__ void (*menu_alpha_setup)();

// for custom menus
__attribute_reloc__ void (*prep_text_mode)();
__attribute_reloc__ void (*gx_draw_text)(u16 index, text_group* text, text_draw_group* text_draw, GXColor* color);
__attribute_reloc__ void (*setup_gameselect_menu)(u8 alpha_0, u8 alpha_1, u8 alpha_2);
__attribute_reloc__ GXColorS10 *(*get_save_color)(u32 color_index, s32 save_type);
__attribute_reloc__ void (*setup_gameselect_anim)();
__attribute_reloc__ void (*setup_cube_anim)();
__attribute_reloc__ model_data *save_icon;
__attribute_reloc__ model_data *save_empty;

// for audio
__attribute_reloc__ void (*Jac_PlaySe)(u32);
__attribute_reloc__ void (*Jac_StopSoundAll)();

// for model gx
__attribute_reloc__ void (*model_init)(model* m, int process);
__attribute_reloc__ void (*draw_model)(model* m);
__attribute_reloc__ void (*draw_partial)(model* m, model_part* part);
__attribute_reloc__ void (*change_model)(model* m);

// for menu elements
__attribute_reloc__ void (*draw_grid)(Mtx position, u8 alpha);
__attribute_reloc__ void (*draw_box)(u32 index, box_draw_group* header, GXColor* rasc, int inside_x, int inside_y, int inside_width, int inside_height);
__attribute_reloc__ void (*draw_start_info)(u8 alpha);
__attribute_reloc__ void (*draw_start_anim)(u8 alpha);
__attribute_reloc__ void (*draw_blob_fixed)(void *blob_ptr, void *blob_a, void *blob_b, GXColor *color);
__attribute_reloc__ void (*draw_blob_text)(u32 type, void *blob, GXColor *color, char *str, s32 len);
__attribute_reloc__ void (*draw_blob_border)(u32 type, void *blob, GXColor *color);
__attribute_reloc__ void (*draw_blob_tex)(u32 type, void *blob, GXColor *color, tex_data *dat);
__attribute_reloc__ void (*setup_tex_draw)(s32 unk0, s32 unk1, s32 unk2);

// unknown blob (from memcard menu)
__attribute_reloc__ void **ptr_menu_blob;
__attribute_data__ void *menu_blob = NULL;

// unknown blobs (from gameselect menu)
__attribute_reloc__ void *game_blob_text;
__attribute_reloc__ void **ptr_game_blob_a;
__attribute_data__ void *game_blob_a = NULL;
__attribute_reloc__ void **ptr_game_blob_b;
__attribute_data__ void *game_blob_b = NULL;

// for camera gx
__attribute_reloc__ void (*set_obj_pos)(model* m, MtxP matrix, guVector vector);
__attribute_reloc__ void (*set_obj_cam)(model* m, MtxP matrix);
__attribute_reloc__ MtxP (*get_camera_mtx)();

// helpers
__attribute_reloc__ f32 (*fast_sin)();
__attribute_reloc__ f32 (*fast_cos)();
__attribute_reloc__ void (*apply_save_rot)(s32 x, s32 y, s32 z, Mtx matrix);
__attribute_reloc__ u32 *bs2start_ready;
__attribute_reloc__ u32 *banner_pointer;
__attribute_reloc__ u32 *banner_ready;

// test only
typedef struct {
    struct gcm_disk_header header;
    BNR banner;
    // u8 icon_rgb5[64*64*2];
    char path[128];
} game_asset;

game_asset *assets;
__attribute_data__ int asset_count;
__attribute_data__ char boot_path[128];

typedef struct {
    f32 scale;
    f32 opacity;
    Mtx m;
} position_t;

static position_t icons_positions[40];
// static asset_t (*game_assets)[40] = 0x80400000; // needs ~2.5mb


// START file browser
typedef struct {
    char file_name[128]; // needs to be bumped to 128
    char game_name[64]; // needs to be bumped to 128
} game_dirent_t;

game_dirent_t current_directory[256];

// game_item_t game_items[32][8];

// line_order

// line animation list [8] depth
// get anim push / pop working



// first press -> lines 0, 1, 2, 3, 4 move up 



// Define constants for max dimensions
#define MAX_LINES 6
#define MAX_ANIMATIONS_PER_LINE 4

// Define the struct
typedef struct {
    bool is_animating;
    signed char queue_index;
    unsigned char current_frame;
    int start_pos;
    int end_pos;
} line_anim_t;

// Define the array to hold animations
line_anim_t line_animation_list[MAX_LINES][MAX_ANIMATIONS_PER_LINE];



// END file browser


__attribute__((aligned(4))) static tex_data banner_texture;

void draw_text(char *s, s16 size, u16 x, u16 y, GXColor *color) {
    static struct {
        text_group group;
        text_metadata metadata;
        char contents[255];
    } text = {
        .group = {
            .type = make_type('S','T','H','0'),
            .arr_size = 1, // arr size
        },
        .metadata = {
            .draw_metadata_index = 0,
            .text_data_offset = sizeof(text_metadata),
        },
    };

    static struct {
        text_draw_group group;
        text_draw_metadata metadata;
    } draw = {
        .group = {
            .type = make_type('G','L','H','0'),
            .metadata_offset = sizeof(text_draw_group),
        },
        .metadata = {
            .type = make_type('m','e','s','g'),
            .x = 0, // x position
            .y = 0, // y position
            .y_align = TEXT_ALIGN_CENTER,
            .x_align = TEXT_ALIGN_TOP,
            .letter_spacing = -1,
            .line_spacing = 0,
            .size = 0,
            .border_obj = 0xffff,
        }
    };

    strcpy(text.contents, s);

    draw.metadata.size = draw.metadata.line_spacing = size;
    draw.metadata.x = (x + 64) * 20;
    draw.metadata.y = (y + 64) * 10;

    gx_draw_text(0, &text.group, &draw.group, color);
}

__attribute_data__ u16 anim_step = 0;

__attribute_data__ GXColorS10 *menu_color_icon;
__attribute_data__ GXColorS10 *menu_color_icon_sel;

__attribute_data__ GXColorS10 *menu_color_empty;
__attribute_data__ GXColorS10 *menu_color_empty_sel;

__attribute_data__ model global_textured_icon = {};
__attribute_data__ model global_empty_icon = {};

// pointers
model *textured_icon = &global_textured_icon;
model *empty_icon = &global_empty_icon;

void set_empty_icon_selected() {
    empty_icon->data->mat[0].tev_color[0] = menu_color_empty_sel;
    empty_icon->data->mat[1].tev_color[0] = menu_color_empty_sel;
}

void set_empty_icon_unselected() {
    empty_icon->data->mat[0].tev_color[0] = menu_color_empty;
    empty_icon->data->mat[1].tev_color[0] = menu_color_empty;
}

void set_textured_icon_selected() {
    textured_icon->data->mat[0].tev_color[0] = menu_color_icon_sel;
    textured_icon->data->mat[2].tev_color[0] = menu_color_icon_sel;
}

void set_textured_icon_unselected() {
    textured_icon->data->mat[0].tev_color[0] = menu_color_icon;
    textured_icon->data->mat[2].tev_color[0] = menu_color_icon;
}

__attribute_used__ void custom_gameselect_init() {
    // default banner
    *banner_pointer = (u32)&default_opening_bin[0];
    *banner_ready = 1;

    // menu setup
    menu_blob = *ptr_menu_blob;
    game_blob_a = *ptr_game_blob_a;
    game_blob_b = *ptr_game_blob_b;

    // colors
    u32 color_num = SAVE_COLOR_PURPLE;
    u32 color_index = 1 << (10 + 3 + color_num);
    menu_color_icon = get_save_color(color_index, SAVE_ICON);
    menu_color_icon_sel = get_save_color(color_index, SAVE_ICON_SEL);
    menu_color_empty = get_save_color(color_index, SAVE_EMPTY);
    menu_color_empty_sel = get_save_color(color_index, SAVE_EMPTY_SEL);

    DUMP_COLOR(menu_color_icon);
    DUMP_COLOR(menu_color_icon_sel);
    DUMP_COLOR(menu_color_empty);
    DUMP_COLOR(menu_color_empty_sel);

    // empty icon
    empty_icon->data = save_empty;
    model_init(empty_icon, 0);
    set_empty_icon_unselected();

    // textured icon
    textured_icon->data = save_icon;
    model_init(textured_icon, 0);
    set_textured_icon_unselected();

    // change the texture format (disc scans)
    tex_data *textured_icon_tex = &textured_icon->data->tex->dat[1];
    textured_icon_tex->format = GX_TF_RGB5A3;
    textured_icon_tex->width = 64;
    textured_icon_tex->height = 64;

    // banner image
    banner_texture.format = GX_TF_RGB5A3;
    banner_texture.width = 96;
    banner_texture.height = 32;

    banner_texture.lodbias = 0; // used by GX_InitTexObjLOD
    banner_texture.index = 0x00;

    banner_texture.unk1 = 0x00;
    banner_texture.unk2 = 0x00;
    banner_texture.unk3 = 0x00;
    banner_texture.unk4 = 0x00;
    banner_texture.unk5 = 0x00;
    banner_texture.unk6 = 0x00;
    banner_texture.unk7 = 0x01; // used by GX_InitTexObjLOD
    banner_texture.unk8 = 0x01; // used by GX_InitTexObjLOD
    banner_texture.unk9 = 0x00;
    banner_texture.unk10 = 0x00;

    // // init anim list
    // for (int line_num = 0; line_num < 6; line_num++) {
    //     int anim_slot = 0; // TODO: increase num of slots
    //     line_anim_t *anim = &line_animation_list[line_num][anim_slot];
    //     anim->is_animating = false;
    //     anim->queue_index = -1;
    //     anim->start_pos = 0;
    //     anim->end_pos = 0;
    //     anim->current_frame = 0;
    // }
}

int selected_slot = 0;

__attribute_used__ void draw_save_icon(u32 index, u8 alpha, bool selected, bool has_texture) {
    position_t *pos = &icons_positions[index];

    f32 sc = pos->scale;
    guVector scale = {sc, sc, sc};

    model *m = NULL;
    if (has_texture) {
        m = textured_icon;
        if (selected) {
            set_textured_icon_selected();
        } else {
            set_textured_icon_unselected();
        }
    } else {
        m = empty_icon;
        if (selected) {
            set_empty_icon_selected();
        } else {
            set_empty_icon_unselected();
        }
    }

    // setup camera
    set_obj_pos(m, pos->m, scale);
    set_obj_cam(m, get_camera_mtx());
    change_model(m);

    // draw icon
    m->alpha = (u8)((f32)alpha * pos->opacity);
    if (has_texture) {
        // cube
        draw_partial(m, &m->data->parts[2]);
        draw_partial(m, &m->data->parts[10]);

        // icon
        tex_data *icon_tex = &m->data->tex->dat[1];
        // u32 target_texture_data = (u32)assets[index].icon_rgb5;
        u16 *source_texture_data = (u16*)assets[0].banner.pixelData;
        u32 target_texture_data = (u32)source_texture_data;

        s32 desired_offset = (s32)((u32)target_texture_data - (u32)icon_tex);
        icon_tex->offset = desired_offset;
        icon_tex->format = GX_TF_RGB5A3;
        icon_tex->width = 96;
        icon_tex->height = 32;

        // TODO: instead set m->data->mat[1].texmap_index[0] = 0xFFFF
        draw_partial(m, &m->data->parts[6]);
    } else {
        draw_model(m);
    }

    return;
}

__attribute_used__ void draw_info_box(GXColor *color) {
    static struct {
        box_draw_group group;
        box_draw_metadata metadata;
    } blob = {
        .group = {
            .type = make_type('G','L','H','0'),
            .metadata_offset = sizeof(box_draw_group),
        },
        .metadata = {
            .center_x = 0x1230,
            .center_y = 0x1640,
            .width = 0x20f0,
            .height = 0x560,

            .inside_center_x = 0x80,
            .inside_center_y = 0x80,
            .inside_width = 0x1ff0,
            .inside_height = 0x460,

            .boarder_index = { 0x28, 0x28, 0x28, 0x28 },
            .boarder_unk = { 0x27, 0x0, 0x0, 0x0 },

            .top_color = {},
            .bottom_color = {},
        }
    };

    GXColor top_color = {0x6e, 0x00, 0xb3, 0xc8};
    GXColor bottom_color = {0x80, 0x00, 0x57, 0xb4};
    copy_gx_color(&top_color, &blob.metadata.top_color[0]);
    copy_gx_color(&top_color, &blob.metadata.top_color[1]);
    copy_gx_color(&bottom_color, &blob.metadata.bottom_color[0]);
    copy_gx_color(&bottom_color, &blob.metadata.bottom_color[1]);

    box_draw_metadata *box = (box_draw_metadata*)((u32)&blob + blob.group.metadata_offset);
	int inside_x = box->center_x - (box->inside_width / 2);
	int inside_y = box->center_y - (box->inside_height / 2);
	draw_box(0, &blob.group, color, inside_x, inside_y, box->inside_width, box->inside_height);

    return;
}

#define WITH_SPACE 1
u32 move_frame = 0;

__attribute_used__ void update_icon_positions() {
#if defined(WITH_SPACE) && WITH_SPACE
    const int base_x = -208;
#else
    const int base_x = -196;
#endif
    const int base_y = 118;

    for (int row = 0; row < 5; row++) {
        f32 mod_pos_y = 0;
        f32 mod_opacity = 1.0;
        // if (row == 0 || row == 1 || row == 2) {
        //     u32 anim_slot = 0;
        //     line_anim_t *anim = &line_animation_list[0][anim_slot];
        //     if (anim->is_animating) {
        //         mod_pos_y = 5.0 * pow(anim->current_frame * 0.1, 1.8);
        //         mod_opacity = (60.0 - (f32)anim->current_frame) / 60.0;

        //         OSReport("current = %u, mod_pos_y = %f\n", anim->current_frame, mod_pos_y);

        //         if (mod_pos_y > anim->end_pos) {
        //             anim->is_animating = false;
        //             move_frame = 0;
        //             anim->queue_index = -1;
        //         }

        //         if (row == 0) anim->current_frame++;
        //     }
        // }
        for (int col = 0; col < 8; col++) {
            int slot_num = (row * 8) + col;
            position_t *pos = &icons_positions[slot_num];
            pos->opacity = mod_opacity;

            f32 sc = 1.3;
            if (slot_num == selected_slot) {
                sc = 2.0;
            }
            pos->scale = sc;

            f32 pos_x = base_x + (col * 56);
#if defined(WITH_SPACE) && WITH_SPACE
            if (slot_num % 8 >= 4) pos_x += 24; // card spacing
#endif
            f32 pos_y = base_y - (row * 56);

            C_MTXIdentity(pos->m);

            if (slot_num == selected_slot) {
                f32 mult = 0.7; // 1.0 is more accurate
                s32 rot_diff_x = fast_cos(anim_step * 70) * 350 * mult;
                s32 rot_diff_y = fast_cos(anim_step * 35 - 15000) * 1000 * mult;
                s32 rot_diff_z = fast_cos(anim_step * 35) * 1000 * mult;

                apply_save_rot(rot_diff_x, rot_diff_y, rot_diff_z, pos->m);

                f32 move_diff_y = fast_sin(35 * anim_step - 0x4000) * 10.0 * mult;
                f32 move_diff_z = fast_sin(70 * anim_step) * 5.0 * mult;

                pos->m[0][3] = pos_x + move_diff_y;
                pos->m[1][3] = pos_y - move_diff_z;
                pos->m[2][3] = 2.0;
            } else {
                pos->m[0][3] = pos_x;
                pos->m[1][3] = pos_y;
                pos->m[2][3] = 1.0;
            }
        }
    }

    anim_step += 0x7; // why is this the const?
}

__attribute_data__ Mtx global_gameselect_matrix;
__attribute_data__ Mtx global_gameselect_inverse;
void set_gameselect_view(Mtx matrix, Mtx inverse) {
    C_MTXCopy(matrix, global_gameselect_matrix);
    C_MTXCopy(inverse, global_gameselect_inverse);
}

void fix_gameselect_view() {
    GX_LoadPosMtxImm(global_gameselect_matrix,0);
    GX_LoadNrmMtxImm(global_gameselect_inverse,0);
    GX_SetCurrentMtx(0);
}

__attribute_data__ u32 current_gameselect_state = SUBMENU_GAMESELECT_LOADER;
__attribute_used__ void custom_gameselect_menu(u8 broken_alpha_0, u8 alpha_1, u8 broken_alpha_2) {
    // color
    u8 ui_alpha = alpha_1;
    // u8 ui_alpha = alpha_2; // correct with animation
    GXColor white = {0xFF, 0xFF, 0xFF, ui_alpha};

    // text
    draw_text("cubeboot loader", 20, 20, 4, &white);

    // icons
    for (int pass = 0; pass < 2; pass++) {
        for (int row = 0; row < 4; row++) {
            for (int col = 0; col < 8; col++) {
                int slot_num = (row * 8) + col;

                bool has_texture = (slot_num < asset_count);
                bool selected = (slot_num == selected_slot);

                if (selected && pass == 0) continue; // skip selected icon on first pass
                if (!selected && pass == 1) continue; // skip unselected icons on second pass
                draw_save_icon(slot_num, alpha_1, selected, has_texture);
            }
        }
    }

    // arrows
    fix_gameselect_view();
    setup_tex_draw(1, 0, 0);
    void (*draw_named_tex)(u32 type, void *blob, GXColor *color, s16 x, s16 y) = (void*)0x8130a36c;
    draw_named_tex(make_type('a','r','a','d'), menu_blob, &white, 0x800 - 100, 0); // TODO: y pos

    // box
    if (rmode->viTVMode >> 2 == VI_NTSC) // quirk
        draw_info_box(&white);

    if (selected_slot < asset_count) {
        // info
        draw_blob_text(make_type('t','i','t','l'), menu_blob, &white, assets[selected_slot].banner.desc->fullGameName, 0x1f);
        draw_blob_text(make_type('i','n','f','o'), menu_blob, &white, assets[selected_slot].banner.desc->description, 0x1f);

        // game source
        draw_blob_border(make_type('f','r','m','c'), menu_blob, &white);
        draw_text("ISO", 20, 125, 540, &white);

        // banner image
        setup_tex_draw(1, 0, 1);
        banner_texture.offset = (s32)((u32)(assets[selected_slot].banner.pixelData) - (u32)&banner_texture);
        draw_blob_tex(make_type('b','a','n','a'), menu_blob, &white, &banner_texture);
    }

    return;
}

__attribute_used__ void original_gameselect_menu(u8 broken_alpha_0, u8 alpha_1, u8 broken_alpha_2) {
    // menu alpha
    u8 ui_alpha = alpha_1;
    GXColor white = {0xFF, 0xFF, 0xFF, ui_alpha};

    // banner and info
    draw_start_info(ui_alpha); // TODO: fix alpha timing

    // press start anim
    draw_start_anim(ui_alpha); // TODO: fix alpha timing

    // fix camera again
    setup_gameselect_menu(0, 0, 0);

    // start string
    draw_blob_fixed(game_blob_text, game_blob_a, game_blob_b, &white);

    return;
}

static bool first_transition = true;
static bool in_submenu_transition = false;
static u8 custom_menu_transition_alpha = 0xFF;
static u8 original_menu_transition_alpha = 0;
__attribute_used__ void pre_menu_alpha_setup() {
    menu_alpha_setup(); // run original function

    if (*cur_menu_id == MENU_GAMESELECT_ID && *prev_menu_id == MENU_GAMESELECT_TRANSITION_ID) {
        OSReport("Resetting back to SUBMENU_GAMESELECT_LOADER\n");
        current_gameselect_state = SUBMENU_GAMESELECT_LOADER;

        if (first_transition) {
            Jac_PlaySe(SOUND_MENU_ENTER);
            first_transition = false;
        }
    }
}

__attribute_used__ void mod_gameselect_draw(u8 alpha_0, u8 alpha_1, u8 alpha_2) {
    // this is for the camera
    setup_gameselect_menu(0, 0, 0);
    draw_grid(global_gameselect_matrix, alpha_1);

    // TODO: use GXColor instead of alpha byte
    u8 custom_alpha_1 = custom_menu_transition_alpha;
    u8 original_alpha_1 = original_menu_transition_alpha;

    if (alpha_1 != 0xFF) {
        custom_alpha_1 = alpha_1;
    }

    if (custom_alpha_1 != 0) custom_gameselect_menu(alpha_0, custom_alpha_1, alpha_2);
    if (original_alpha_1 != 0) original_gameselect_menu(0, original_alpha_1, 0); // fix alpha_0 + alpha_2?

    return;
}

__attribute_used__ s32 handle_gameselect_inputs() {
    update_icon_positions();

    // TODO: only works with numbers that do not divide into 255
    const u8 transition_step = 14;
    if (in_submenu_transition) {
        if (custom_menu_transition_alpha != 0 && original_menu_transition_alpha != 0) {
            if ((255 - custom_menu_transition_alpha) < transition_step || (255 - original_menu_transition_alpha) < transition_step) {
                in_submenu_transition = false;

                custom_menu_transition_alpha = custom_menu_transition_alpha < 127 ? 0 : 255;
                original_menu_transition_alpha = original_menu_transition_alpha < 127 ? 0 : 255;
            }
        }
    }

    if (in_submenu_transition) {
        switch(current_gameselect_state) {
            case SUBMENU_GAMESELECT_LOADER:
                custom_menu_transition_alpha += transition_step;
                original_menu_transition_alpha -= transition_step;
                break;
            case SUBMENU_GAMESELECT_START:
                custom_menu_transition_alpha -= transition_step;
                original_menu_transition_alpha += transition_step;
                break;
            default:
        }
    }

    if (pad_status->buttons_down & PAD_TRIGGER_Z) {
        // get_free_anim_slot()
        //     // TODO: find next anim slot
        //     u32 anim_slot = 0;
        //     line_anim_t *anim = &line_animation_list[0][anim_slot];
        //     anim->is_animating = true;
        //     anim->queue_index = 0;
        //     anim->start_pos = 0;
        //     anim->end_pos = 120;
        //     anim->current_frame = 0;
    }

    if (pad_status->buttons_down & PAD_BUTTON_B) {
        if (current_gameselect_state == SUBMENU_GAMESELECT_START && !in_submenu_transition) {
            in_submenu_transition = true;
            current_gameselect_state = SUBMENU_GAMESELECT_LOADER;
            Jac_PlaySe(SOUND_SUBMENU_EXIT);
        } else if (!in_submenu_transition) {
            anim_step = 0; // anim reset
            *banner_pointer = (u32)&default_opening_bin[0]; // banner reset
            Jac_PlaySe(SOUND_MENU_EXIT);
            return MENU_GAMESELECT_ID;
        }
    }

    if (pad_status->buttons_down & PAD_BUTTON_A && current_gameselect_state == SUBMENU_GAMESELECT_LOADER) {
        if (selected_slot < asset_count && !in_submenu_transition) {
            in_submenu_transition = true;
            current_gameselect_state = SUBMENU_GAMESELECT_START;
            *banner_pointer = (u32)&assets[selected_slot].banner; // banner buf

            Jac_PlaySe(SOUND_SUBMENU_ENTER);
            setup_gameselect_anim();
            setup_cube_anim();
        }
    }

    if (pad_status->buttons_down & PAD_BUTTON_START && current_gameselect_state == SUBMENU_GAMESELECT_START) {
        Jac_StopSoundAll();
        Jac_PlaySe(SOUND_MENU_FINAL);
        strcpy(boot_path, assets[selected_slot].path);
        *bs2start_ready = 1;
    }

    if (current_gameselect_state == SUBMENU_GAMESELECT_LOADER) {
        if (pad_status->analog_down & ANALOG_RIGHT) {
            if ((selected_slot % 8) == (8 - 1)) {
                Jac_PlaySe(SOUND_CARD_ERROR);
            }
            else {
                Jac_PlaySe(SOUND_CARD_MOVE);
                selected_slot++;
            }
        }

        if (pad_status->analog_down & ANALOG_LEFT) {
            if ((selected_slot % 8) == 0) {
                Jac_PlaySe(SOUND_CARD_ERROR);
            }
            else {
                Jac_PlaySe(SOUND_CARD_MOVE);
                selected_slot--;
            }
        }

        if (pad_status->analog_down & ANALOG_DOWN) {
            if (((5 * 8) - selected_slot) <= 8) {
                Jac_PlaySe(SOUND_CARD_ERROR);
            }
            else {
                Jac_PlaySe(SOUND_CARD_MOVE);
                selected_slot += 8;
            }
        }

        if (pad_status->analog_down & ANALOG_UP) {
            if (selected_slot < 8) {
                Jac_PlaySe(SOUND_CARD_ERROR);
            }
            else {
                Jac_PlaySe(SOUND_CARD_MOVE);
                selected_slot -= 8;
            }
        }
    }

    return MENU_GAMESELECT_TRANSITION_ID;
}

void alpha_watermark(void) {
    prep_text_mode();

    GXColor yellow_alpha = {0xFF, 0xFF, 0x00, 0x80};
    draw_text("ALPHA TEST", 24, 330, 0, &yellow_alpha);
    draw_text("cubeboot rc" CONFIG_ALPHA_RC, 22, 330, 28, &yellow_alpha);
}
