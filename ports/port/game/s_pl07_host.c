/*
 * s_pl07_host.c  s_pl07.c for an ARM host
 *
 * nml_p_chase tests ( Uint32 )( dx + 1.0f ) > 2, which a step of more than one dot back passes on the SH4
 * (the negative wraps) and fails on ARM. The module's own routine is renamed by a function-like macro,
 * which leaves the bare name in the throw's table alone, so the table takes the one here.
 */

#include "player.h"
#include "sh_conv.h"

static void nml_p_chase( PLAYER_WORK *pl_w, PLAYER_WORK *em );
#define nml_p_chase( pl_w, em ) nml_p_chase_sh( pl_w, em )
#include "s_pl07.c"
#undef nml_p_chase

static void nml_p_chase( PLAYER_WORK *pl_w, PLAYER_WORK *em )
{
	VEC3 pos ;
	Float dx, dy ;

	scr_excep_set( pl_w, 6, 6 ) ;
	pos = pl_w->pos ;
	em->pos.y = em->ground_y ;
	catch_em_pos_set1( pl_w, em ) ;
	dx = ( pl_w->pos.x - pos.x ) / 2.0f ;
	dy = ( pl_w->pos.y - pos.y ) / 2.0f ;
	if( sh_u32( dx + 1.0f ) > 2 || sh_u32( dy + 1.0f ) > 2 ){
		pl_w->pos.x = pos.x + dx ;
		pl_w->pos.y = pos.y + dy ;
		pl_w->cg_ctr = 8 ;
	}
}
