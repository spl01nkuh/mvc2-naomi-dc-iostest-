/* ctr_render.h  The 3DS's renderer: the frame's lists drawn with citro3d */

#ifndef CTR_RENDER_H
#define CTR_RENDER_H

#include <stdint.h>

int ctr_render_init(int full, int stretch);   /* full: 640x480 drawn, halved (else 320x240); 0: no GPU */
void ctr_render_set_stretch(int stretch);     /* between frames: the game across the screen or between the pillars,
                                                 from the next frame built */
int ctr_render_reserve(void);  /* before the heap is shared out: the memory for drawing beside the game */
const char* ctr_render_mode(void);
void ctr_render_frame(void);
void ctr_render_restart(int after);  /* host_restart_hook's: before the program's memory is put back (0), after (1) */
void ctr_render_exit(void);
void ctr_render_note(void);     /* the bottom screen's text shown without a frame drawn */
void ctr_render_alive(void);    /* each vertical blank while a screen holds the game */
extern unsigned ctr_presented;
int ctr_render_read(uint32_t* frame, int screen);  /* 640x480 ARGB, or the top screen's 400x240; for test runs */

/* system ticks and counts */
enum { CTR_TICKS_SYNC, CTR_TICKS_LISTS, CTR_TICKS_TEX, CTR_TICKS_SORT, CTR_TICKS_TRANS, CTR_TICKS_DRAW, CTR_TICKS_END, CTR_TICKS };
enum { CTR_COUNT_MADE, CTR_COUNT_TEXELS, CTR_COUNT_FREED, CTR_COUNT_TRIS, CTR_COUNT_DRAWS, CTR_COUNT_KEPT, CTR_COUNT_GROUPS, CTR_COUNT_TL_RUNS,
       CTR_COUNT_GROUPS_ONCE, CTR_COUNT_TL_INDICES, CTR_COUNT_TL_VERTS, CTR_COUNT_EARLY_AGAIN, CTR_COUNT_MESHES,
       CTR_COUNT_MESH_BYTES, CTR_COUNT_MESH_FULL, CTR_COUNT_DROPPED, CTR_COUNT_MOVED,
       CTR_COUNT_STAGES1, CTR_COUNT_HELD, CTR_COUNTS };
/* the sums since the last call, of the frames drawn to the end; it doesn't wait for the drawing thread */
void ctr_render_stats(uint64_t ticks[CTR_TICKS], uint32_t counts[CTR_COUNTS]);
/* on the game's thread: the GPU's mesh pool, in bytes */
void ctr_render_meshes(uint32_t* size, uint32_t* used);
/* MVC2_LISTS_CORE1: core 1's list ticks so far, the stage under way's to now; called by the sound's thread when the
   frame handed over is made */
uint32_t ctr_lists1_busy(void);
void ctr_lists1_sound_done(void);
/* profile builds: the memory blocks (all, in use) and flushes' ticks (4 KB least and most, 64 KB) */
void ctr_flush_probe(uint32_t out[5]);

#endif