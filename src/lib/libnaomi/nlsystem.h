/* The system set-up of the NAOMI graphics library: Kamui2's configuration and vertex context, and
   what nlsystem and nlproj share. */
#ifndef LIBNAOMI_NLSYSTEM_H
#define LIBNAOMI_NLSYSTEM_H

#include "types.h"
#include "kamui.h"

/* a render pass of the system configuration */
typedef struct KMPASSINFO {
	Uint32	flags;		/* 00 */
	Uint32	buffer;		/* 04: where its vertex buffer starts */
	Uint32	size;		/* 08: bytes of vertex buffer */
	Uint32	list_type;	/* 0c: the direct list, 0 to 4 */
	Float	ratio[5];	/* 10 */
	Uint32	opb_size[5];	/* 24: each list's object pointer block size */
} KMPASSINFO;

/* what Kamui2's kmSetSystemConfiguration takes */
typedef struct KMSYSTEMCONFIGSTRUCT {
	Uint32	dwSize;		/* 00 */
	Uint32	flags;		/* 04 */
	KMSURFACEDESC	**pSurfaceDesc;	/* 08 */
	Uint32	nFrameBuffers;	/* 0c */
	Uint32	strip_height;	/* 10: the strip buffer's height */
	Uint32	texture_size;	/* 14 */
	Uint32	texture_structs;	/* 18: how many textures */
	Uint32	small_vq_structs;	/* 1c: how many of them small VQ ones */
	void	*texture_work;	/* 20: 24 bytes a texture and 76 a small VQ one */
	void	*buffer_desc;	/* 24: the vertex buffer's description, filled in */
	Uint32	vertex_banks;	/* 28 */
	Uint32	base;		/* 2c: where the vertex buffer starts */
	Uint32	vertex_size;	/* 30 */
	Uint32	passes;		/* 34 */
	KMPASSINFO	pass[8];	/* 38 */
} KMSYSTEMCONFIGSTRUCT;

/* a vertex context: the settings of a strip, and the header words kmProcessVertexRenderState
   makes of them */
typedef struct KMVERTEXCONTEXT {
	Uint32	state;		/* 00: which of the settings to process */
	Uint32	param_type;	/* 04 */
	Uint32	list_type;	/* 08: 0 opaque, 2 translucent */
	Uint32	color_type;	/* 0c: 0 packed, 1 floating */
	Uint32	uv_format;	/* 10 */
	Uint32	depth_mode;	/* 14 */
	Uint32	culling_mode;	/* 18 */
	Uint32	reserved0;	/* 1c */
	Uint32	shading;	/* 20: bit 0 gouraud, bit 1 textured */
	Uint32	select_modifier;	/* 24 */
	Uint32	zwrite_disable;	/* 28 */
	Uint32	src_blend;	/* 2c */
	Uint32	dst_blend;	/* 30 */
	Uint32	src_select;	/* 34 */
	Uint32	dst_select;	/* 38 */
	Uint32	fog_mode;	/* 3c */
	Uint32	specular;	/* 40 */
	Uint32	use_alpha;	/* 44 */
	Uint32	ignore_tex_alpha;	/* 48 */
	Uint32	clamp_uv;	/* 4c */
	Uint32	flip_uv;	/* 50 */
	Uint32	filter;		/* 54 */
	Uint32	super_sample;	/* 58 */
	Uint32	mipmap_adjust;	/* 5c: in quarters */
	Uint32	tex_shading;	/* 60 */
	Uint32	color_clamp;	/* 64 */
	Uint32	palette_bank;	/* 68 */
	KMSURFACEDESC	*texture;	/* 6c */
	Float	face_a, face_r, face_g, face_b;		/* 70 */
	Float	offset_a, offset_r, offset_g, offset_b;	/* 80 */
	Uint32	pcw;		/* 90: the parameter control word */
	Uint32	isp;		/* 94 */
	Uint32	tsp;		/* 98 */
	Uint32	tcw;		/* 9c: the texture control word */
	Uint32	modifier_inst;	/* a0: the modifier volume's instruction */
	Float	reserved1[4];	/* a4 */
	Uint32	dcalc_exact;	/* b4 */
	Uint32	reserved5;	/* b8 */
	Uint32	user_clip;	/* bc: the user clip mode */
} KMVERTEXCONTEXT;

/* the screen: its size and mode, in nl_scr */
typedef struct NLSCREEN {
	Sint16	unk_0, unk_2;
	Sint16	width;		/* 04 */
	Sint16	height;		/* 06 */
	Sint16	interlace;	/* 08 */
	Sint16	vga;		/* 0a: the VGA flag, which doubles a line */
} NLSCREEN;

extern NLSCREEN nl_scr;
/* the library's work: its near and far planes, how they scale, and the model work areas with
   their stack (nl_vertex and nl_lit hold each level's place) */
typedef struct NLSYSWORK {
	Uint8	palette_changed;	/* 00: nlcmnPalette's, by GBR */
	Uint8	palette_mode;	/* 01: 3 for 32-bit colours */
	Sint16	unk_02;
	Sint16	palettes;	/* 04: how many are loaded */
	Sint16	vertex_depth;	/* 06 */
	Sint32	unk_08[4];
	void	*palette;	/* 18: the loaded palettes */
	Float	znear;		/* 1c */
	Float	zfar;		/* 20 */
	Float	znear_inv;	/* 24 */
	Float	scale_factor;	/* 28 */
	Float	*vertex_ptr;	/* 2c */
	Float	*vertex_end_ptr;	/* 30 */
	Float	*vertex_work;	/* 34 */
	Sint32	*lit_ptr;	/* 38 */
	Sint32	*lit_end_ptr;	/* 3c */
	Sint32	*lit_work;	/* 40 */
	Sint32	unk_44;		/* 44 */
} NLSYSWORK;

extern NLSYSWORK NLSystemWork_;

#endif
