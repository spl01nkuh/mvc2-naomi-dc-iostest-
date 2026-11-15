/* ef_char_set: set an effect's animation, its group and number as bytes, as pl_char.c defines it; and
   the frame an effect's animation copies into its work. */
#ifndef EF_CHAR_H
#define EF_CHAR_H

/* an effect's animation frame: the first eight bytes of a player's */
typedef struct EF_CHAR_DATA {
	Sint32	loop_ofs;	/* 000: the frame's codes; after a last frame, where the animation goes on */
	Sint32	spr_no;		/* 004 */
} EF_CHAR_DATA;

/* what an effect's frame leaves in cg_prm */
typedef struct EF_CHAR_WORK {
	EF_CHAR_DATA data;	/* 000: cg_prm to spr_no */
	Sint32	prio_l;		/* 008: cg_prio to cg_flag1, which the frame clears */
	Sint32	se_l;		/* 00c: cg_se to cg_flag2 */
	Sint32	rival_l;	/* 010: cg_rival and the two bytes after it */
} EF_CHAR_WORK;

#define EF_CG( w )	( *( EF_CHAR_WORK * )&( w )->cg_prm )

extern int ef_char_set( PLAYER_WORK *ef_w, Uint8 group, Uint8 no );

#endif
