/* ctr_gamez.h  MVC2_PROF_GAMEZ: the game's frame timed by its routines (ctr_gamez_wrap.c wraps them inside each
   program, ctr_gamez.c keeps the times) */

#ifndef CTR_GAMEZ_H
#define CTR_GAMEZ_H

enum {
    GZ_SYSTEM_MAIN, GZ_SYS_TRANS, GZ_SYS_TRANS2, GZ_LOAD_MAIN, GZ_SOUND_VSYNC, GZ_KEY_REPEAT, GZ_VMS_CHECK,
    GZ_COL_SEND, GZ_GAME_MAIN, GZ_ALL_WORK_MOVE, GZ_PL_WORK_MOVE, GZ_SCR_MOVE, GZ_STAGE_FRAME, GZ_PL_HIT_WORK_SET,
    GZ_BG_MOVE, GZ_FOG_SET, GZ_HIT_CK0, GZ_VS_SYS_MOVE, GZ_FLASH_MOVE, GZ_DISP_PRIO_CLR, GZ_PL_WORK_DISP,
    GZ_GAME_DISP, GZ_SPRITE_DISP, GZ_SPRITE_TEX_SET, GZ_LZ_UNPACK,
    GZ_LISTS = 16,                      /* the work lists, by number */
    GZ_MOVE_LIST = 32,                  /* work_list_move of each list */
    GZ_DISP_LIST = GZ_MOVE_LIST + GZ_LISTS,
    GZ_COUNT = GZ_DISP_LIST + GZ_LISTS
};

void gamez_in(int zone);
void gamez_out(void);

#endif
