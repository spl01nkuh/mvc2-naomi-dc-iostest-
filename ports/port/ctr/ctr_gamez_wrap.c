/* ctr_gamez_wrap.c  MVC2_PROF_GAMEZ: linked into each program's relocatable, where ld -r --wrap (CMakeLists) sends
   the calls from other modules to these, so that ctr_gamez.c times the routines */

#include "ctr_gamez.h"

#define WRAP(name, zone)                                                                                               \
    void __real_##name(void);                                                                                          \
    void __wrap_##name(void) {                                                                                         \
        gamez_in(zone);                                                                                                \
        __real_##name();                                                                                               \
        gamez_out();                                                                                                   \
    }

WRAP(System_Main, GZ_SYSTEM_MAIN)
WRAP(SysTrans, GZ_SYS_TRANS)
WRAP(SysTrans2, GZ_SYS_TRANS2)
WRAP(load_main, GZ_LOAD_MAIN)
WRAP(sound_vsync, GZ_SOUND_VSYNC)
WRAP(vms_check, GZ_VMS_CHECK)
WRAP(col_send, GZ_COL_SEND)
WRAP(game_main, GZ_GAME_MAIN)
WRAP(all_work_move, GZ_ALL_WORK_MOVE)
WRAP(pl_work_move, GZ_PL_WORK_MOVE)
WRAP(scr_move, GZ_SCR_MOVE)
WRAP(stage_frame, GZ_STAGE_FRAME)
WRAP(pl_hit_work_set, GZ_PL_HIT_WORK_SET)
WRAP(bg_move, GZ_BG_MOVE)
WRAP(fog_set, GZ_FOG_SET)
WRAP(hit_ck0, GZ_HIT_CK0)
WRAP(vs_sys_move, GZ_VS_SYS_MOVE)
WRAP(flash_move, GZ_FLASH_MOVE)
WRAP(disp_prio_clr, GZ_DISP_PRIO_CLR)
WRAP(pl_work_disp, GZ_PL_WORK_DISP)
WRAP(game_disp, GZ_GAME_DISP)

void __real_key_repeat(void* sw);
void __wrap_key_repeat(void* sw) {
    gamez_in(GZ_KEY_REPEAT);
    __real_key_repeat(sw);
    gamez_out();
}

int __real_sprite_disp(void* w);
int __wrap_sprite_disp(void* w) {
    int n;
    gamez_in(GZ_SPRITE_DISP);
    n = __real_sprite_disp(w);
    gamez_out();
    return n;
}

int __real_sprite_tex_set(void* w);
int __wrap_sprite_tex_set(void* w) {
    int n;
    gamez_in(GZ_SPRITE_TEX_SET);
    n = __real_sprite_tex_set(w);
    gamez_out();
    return n;
}

int __real_lz_unpack(signed char* src, int size, unsigned char* dst);
int __wrap_lz_unpack(signed char* src, int size, unsigned char* dst) {
    int n;
    gamez_in(GZ_LZ_UNPACK);
    n = __real_lz_unpack(src, size, dst);
    gamez_out();
    return n;
}

void __real_work_list_move(unsigned char list);
void __wrap_work_list_move(unsigned char list) {
    gamez_in(GZ_MOVE_LIST + (list & (GZ_LISTS - 1)));
    __real_work_list_move(list);
    gamez_out();
}

void __real_work_list_disp(unsigned char list);
void __wrap_work_list_disp(unsigned char list) {
    gamez_in(GZ_DISP_LIST + (list & (GZ_LISTS - 1)));
    __real_work_list_disp(list);
    gamez_out();
}
