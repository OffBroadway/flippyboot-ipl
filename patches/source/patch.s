#define _LANGUAGE_ASSEMBLY
#include "asm.h"
#include "patch_asm.h"

// TODO: install a pre_main function for init
// TODO: replace bs2start with a funciton overload

patch_inst vNTSC_11(_change_default_menu) 0x81310b38 li r0, 0x6
// patch_inst vNTSC_11(_disable_main_menu_text) 0x81314d24 nop
// patch_inst vNTSC_11(_disable_main_menu_cube_a) 0x8130df44 nop
// patch_inst vNTSC_11(_disable_main_menu_cube_b) 0x8130dfac nop
// patch_inst vNTSC_11(_disable_menu_transition_cube) 0x8130de04 nop
// patch_inst vNTSC_11(_disable_menu_transition_outer) 0x8130ddc0 nop
patch_inst vNTSC_11(_disable_menu_transition_text_a) 0x8130de88 nop
patch_inst vNTSC_11(_disable_menu_transition_text_b) 0x8130de70 nop

// patch_inst vNTSC_11(_disable_options_menu_transition) 0x81327c4c nop
patch_inst vNTSC_11(_disable_options_menu_title_text) 0x81327c10 nop
patch_inst vNTSC_11(_disable_options_menu_sound_text) 0x81327c24 nop
patch_inst vNTSC_11(_disable_options_menu_offset_text) 0x81327c38 nop
patch_inst vNTSC_11(_force_options_menu_draw) 0x81327c44 nop
patch_inst vNTSC_11(_custom_options_menu_draw) 0x81327c4c bl custom_options_menu

// 81313724 text

// patch_inst vNTSC_11(_disable_card_menu_exit) 0x8131612c b _addr_813161e8
// patch_inst vNTSC_11(_disable_card_menu_text_a) 0x8131b328 nop
// patch_inst vNTSC_11(_disable_card_menu_text_b) 0x8131b3bc nop
// patch_inst vNTSC_11(_disable_card_menu_text_c) 0x8131b45c nop
// patch_inst vNTSC_11(_disable_card_menu_text_d) 0x8131b4f0 nop
// patch_inst vNTSC_11(_disable_memory_card_detect) 0x813010ec nop


patch_inst_ntsc "_stub_dvdwait" 0x00000000 0x8130108c 0x81301440 0x81301444 nop
patch_inst_pal  "_stub_dvdwait" 0x8130108c 0x8130108c 0x813011f8 nop

patch_inst_ntsc "_replace_bs2tick" 0x81300a70 0x81300968 0x81300d08 0x81300d0c b bs2tick
patch_inst_pal  "_replace_bs2tick" 0x81300968 0x81300968 0x81300ac0 b bs2tick

patch_inst_ntsc "_replace_bs2start" 0x813023e0 0x813021e8 0x81302590 0x813025a8 bl bs2start
patch_inst_pal  "_replace_bs2start" 0x813021e8 0x813021e8 0x8130235c bl bs2start

patch_inst_ntsc "_replace_report" 0x8133491c 0x8135a344 0x81300520 0x81300520 b usb_OSReport
patch_inst_pal  "_replace_report" 0x8135d924 0x8135a264 0x81300520 b usb_OSReport

patch_inst_ntsc "_patch_cube_init" 0x8130e9b4 0x8130ebac 0x8130ef20 0x8130ef38 bl pre_cube_init
patch_inst_pal  "_patch_cube_init" 0x8130f2c8 0x8130ead8 0x8130f408 bl pre_cube_init

patch_inst_pal "_fix_video_mode_init" 0x81300520 0x81300520 0x81300610 bl get_tvmode

patch_inst_global "_patch_pre_main" 0x81300090 bl pre_main
