#include <stdint.h>
#include <ogc/gx.h>
#include <ogc/gx_struct.h>
#include <ogc/video_types.h>

#define TEXT_ALIGN_CENTER 0
#define TEXT_ALIGN_TOP 2

typedef struct state_t {
    s32 unk0;
    s32 unk1;
    s8 unk2;
    s8 unk3;
    s8 unk4;

    u8 cube_alpha;
    u32 cube_color;

    u16 unk5;
    s16 unk6;
    s16 cube_side_frames;
    s16 cube_corner_frames;
    s16 unk9;
    s16 unk10;
    
    u8 unk11;
    u8 unk12;
    u8 unk13;
    u8 unk14;
    u8 unk15;
    u8 unk16;

    u8 fall_anim_frames;
    u8 fall_delay_frames;
    
    f32 unk19;
    f32 unk20;
    f32 unk21;
    f32 unk22;

    u8 unk23;
    u8 up_anim_frames;
    u8 up_delay_frames;
    u8 unk26;
    f32 unk27;

    u8 unk28;
    u8 move_anim_frames;
    u8 move_delay_frames;

    f32 unk31;
    f32 unk32;
    f32 unk33;
    f32 unk34;
    f32 unk35;

    u8 done_delay_frames;
    u8 unk37;
    u8 unk38;

    u8 logo_hold_frames;
    u8 logo_delay_frames;

    u8 big_size;
    GXColor color;

    s16	unk41; // maybe audio related
    s16	audio_cue_frames_a;
    s16	audio_cue_frames_b;
    s16	audio_cue_frames_c;
} state;

typedef struct mat_t {
    u8 unk0;
    u8 unk1;
    u8 unk2;
    u8 unk3;
    u8 unk4;
    u8 unk5;
    u8 unk6;
    u8 unk7;
    
    u16 unk8[8];

    GXColor* color[2];
    void* unk9[4];
    GXColorS10* tev_color[4];

    void* unk10[8];
    void* unk11[8];
    void* unk12[8];
    void* unk13[10];

    void* unk14;
    void* unk15;
    void* unk16;
    void* unk17;
} mat;

typedef struct tex_data_t {
    u8 format;
    u8 unk0;

    u16 width;
    u16 height;

    u8 unk1;
    u8 unk2;

    u8 index;
    u8 color_space_tlut;
    u16 pal_size;
    s32 pal_offset;

    u8 unk3;
    u8 unk4;
    u8 unk5;
    u8 unk6;
    u8 unk7;
    u8 unk8;
    s8 unk9;
    s8 unk10;
    u8 unk11;
    s8 unk12;

    s16 lodbias; // used by GX_InitTexObjLOD
    s32 offset; // image data
} tex_data;

typedef struct tex_t {
    u32 magic;
    u32 size;
    u16 id;

    u16 unk0;

    tex_data *dat;
} tex;

typedef struct model_data_t {
    void* unka;
    void* unkb;
    
    void* root;
    void* unkc;
    mat* mat;

    void* unk0;
    void* unk1;
    void* unk2;
    void* unk3;
    void* unk4;
    void* unk5;
    void* unk6;
    void* unk7;
    tex* tex;

} model_data;

typedef struct model_t {
    Mtx *anim;
    Mtx unk0;
    Mtx cur;

    GXColor *color;
    model_data *data;

    void *unk1;
    void *unk2;
} model;


typedef struct text_draw_group {
	u32 type;

	u32 unk0_offset;
	u32 metadata_offset;
	u32 unk1_offset;

	u16 unk2;
	u16 unk3;
	u16 unk4;
} text_draw_group;

typedef struct text_draw_metadata {
	u32 type;

    // positions use some weird (0 -> 10240) range with ~400 being underscan/overscan
	u16 x;
	u16 y;

	u16 unk0; // maybe max_width
	u16 unk1; // maybe max_height
	u32 unk2;
	u32 unk3;

	u8 y_align;
	u8 x_align;
	s16 letter_spacing;
	s16 line_spacing;
	u16 size;
	u16 border_obj;
} text_draw_metadata;

typedef struct text_group {
	u32 type;

	u16 arr_size;
	u16 padding;
} text_group;

typedef struct text_metadata {
	u16 draw_metadata_index;
	u16 length;
	u32 text_data_offset;
} text_metadata;
