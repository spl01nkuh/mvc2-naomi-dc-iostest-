/* Native definitions of tables already reconstructed in src/spr_tbl.src. */
#ifndef MVC2_NATIVE64_GAME_TABLES_H
#define MVC2_NATIVE64_GAME_TABLES_H

#include "types.h"

#ifndef HOST_NATIVE64
#error Native game tables are only for the experimental HOST_NATIVE64 path
#endif

enum { MVC2_NATIVE_FIGHTER_COUNT = 59 };
extern Sint16 spr_cnt_tbl[MVC2_NATIVE_FIGHTER_COUNT];
extern Sint16 spr_set_cnt_tbl[MVC2_NATIVE_FIGHTER_COUNT];

#endif
