/*
 * s_pl1a_host.c  s_pl1a.c for an ARM host
 *
 * pl1a_win takes the throw win when ( Uint32 )x, the gap past 160 dots, is within 128 dots: a nearer
 * opponent's negative gap wraps on the SH4 and fails, but stops at 0 on ARM and passes. The module's own
 * routine is renamed by a function-like macro, which leaves the bare name in the routine tables alone,
 * so they take the one here.
 */

#include "player.h"
#include "sh_conv.h"

void pl1a_win( PLAYER_WORK *pl_w );
#define pl1a_win( pl_w ) pl1a_win_sh( pl_w )
#include "s_pl1a.c"
#undef pl1a_win

void pl1a_win( PLAYER_WORK *pl_w )
{
	Float x ;

	switch( pl_w->type ){
	case 0:
	case 2:
	case 4:
		pl_w->routine_1 = PL_NORM ;
		pl_w->routine_2 = pl_w->routine_3 = 0 ;
		x = pl_w->target_w->pos.x - pl_w->pos.x ;
		if( !pl_w->pl_dir ) x = -x ;
		x -= pl_w->scale.x * 160.0f ;
		if( sh_u32( x ) <= pl_w->scale.x * DOT_X( 128.0f ) && vs_sys_w->winner_id == pl_w->pl_no && !vs_sys_w->time_over ){
			pl_w->pl_sp_type = pl1a_sp_throw ;
		} else {
			if( Sys->play_timer & 1 ) pl_w->pl_sp_type = pl1a_sp_win ;
			else pl_w->pl_sp_type = pl1a_sp_win_voice ;
		}
		act_st_set( pl_w, N_SPECIAL ) ;
		break ;
	case 1:
	case 3:
		pl_w->routine_1 = PL_NORM ;
		pl_w->routine_2 = pl_w->routine_3 = 0 ;
		pl_w->pl_sp_type = pl1a_sp_fade ;
		act_st_set( pl_w, N_SPECIAL ) ;
		break ;
	}
}
