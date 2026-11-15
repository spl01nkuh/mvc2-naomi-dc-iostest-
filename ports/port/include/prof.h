/* prof.h  Zone timers for profiling builds (-DMVC2_PROF); nothing at all otherwise */

#ifndef PROF_H
#define PROF_H

#include <stdint.h>

/* each zone is added to by one thread only */
enum {
    PZ_FRAME,                                   /* a whole frame, timed by prof_frame */
    PZ_GAME, PZ_PRESENT, PZ_LISTS,
    PZ_LIST_SLICES, PZ_LIST_BUCKET, PZ_LIST_ADD, PZ_LIST_SORT,  /* the lists' build, in its steps */
    PZ_TEXTURES, PZ_DRAWS, PZ_GPU_WAIT, PZ_VBLANK, PZ_ROM_MISS, PZ_ROM_POLL, PZ_SND_PREPARE, PZ_SND_SYNC,
    PZ_SND_FRAME = 16, PZ_SND_ARM, PZ_SND_FIQ, PZ_SND_IO, PZ_SND_CHANNELS, PZ_SND_MIX, PZ_SND_DEFER,
    PZ_SND_OUT, PZ_SND_IDLE,
    PZ_SND_SYNC_MID = 25,                       /* the game's waits inside its frame; PZ_SND_SYNC is the frame's end */
    PZ_TEX_ALLOC, PZ_TEX_FLUSH,                 /* inside PZ_TEXTURES' calls, and outside them */
    PZ_ROM_THREAD = 28,
    PZ_LIST_TRANS,                              /* the translucent triangles' adds */
    PZ_NL_OBJ, PZ_NL_SPRITE, PZ_NL_STRIP, PZ_NL_TEX,   /* inside PZ_GAME: the library's models, sprites, strips, texture loads */
    PZ_HANDOFF,                                 /* the frame's textures and state taken from the game, PZ_TEXTURES in it */
    PZ_PIPE_WAIT,                               /* the game's thread waiting for the drawing thread */
    PZ_RENDER_IDLE,                             /* the drawing thread waiting for a frame */
    PZ_VERT_FLUSH,                              /* inside PZ_DRAWS: the vertices out of the cache */
    PZ_TRANS_PREP, PZ_TRANS_CUTS, PZ_TRANS_GROUP, PZ_TRANS_ORDER,   /* inside PZ_LIST_SORT, in its steps */
    PZ_TEX_BIG,                                 /* inside PZ_TEXTURES: those of 128x128 texels or more */
    PZ_DRAW_MODELS,                             /* inside PZ_DRAWS: the GPU's model groups, and going back from them */
    PZ_GPU_LIST,                                /* the GPU: from C3D_FrameEnd to its command list's end, the clear first */
    PZ_TL_UNIFS, PZ_TL_CORNER, PZ_TL_DEPTH, PZ_TL_APPLY, PZ_TL_WRITE, PZ_TL_FRAG, PZ_TL_C3D, PZ_TL_BACK,  /* inside PZ_DRAW_MODELS, with MVC2_PROF_TL=1 */
    PZ_NLG_PRE, PZ_NLG_FIND, PZ_NLG_PUT,       /* inside PZ_NL_OBJ, a group's parts, with MVC2_PROF_GROUPS=n */
    PZ_NLG_HERE,                               /* a group turned and lit here */
    PZ_LIST_GPREP,                             /* inside PZ_LISTS: the model groups' uniforms and depths */
    PZ_LISTS1, PZ_LISTS1_WAIT,                 /* MVC2_LISTS_CORE1: the stages on core 1, and core 0 waiting for one */
    PZ_SLICE_SCAN, PZ_SLICE_SORT, PZ_SLICE_CUT,    /* inside PZ_LIST_SLICES: the depths read, sorted, and cut */
    PZ_SCAN_GROUPS,                            /* inside PZ_SLICE_SCAN: the model groups' depths */
    PZ_ADD_TEX, PZ_TRANS_TEX,                  /* inside PZ_LIST_ADD and PZ_LIST_TRANS: the textures made there */
    PZ_LISTS1_STAGE,                           /* inside PZ_LISTS1: its stages, slices to prep, one zone each */
    PZ_DRAW_BEGIN = PZ_LISTS1_STAGE + 5,       /* inside PZ_DRAWS: before the batches, the clear and the pillars */
    PZ_CONSOLE_FLUSH, PZ_FRAME_END,            /* inside PZ_DRAWS, after the batches: the bottom screen's text out of
                                                  the cache, and C3D_FrameEnd with the frame's command words */
    PZ_CMD_FLUSH,                              /* inside PZ_DRAWS: MVC2_CMD_FLUSH=self, the commands out of the cache */
    PZ_GAME_APT,                               /* inside PZ_GAME: the host's loop, input and the frame's start, */
    PZ_GAME_INPUT, PZ_GAME_SETUP, PZ_GAME_IRQ, PZ_GAME_AUDIO,
    PZ_GAME_MATRIX, PZ_GAME_MATH,              /* and the game's calls into the library (ctr_game_zones.c) */
    PZ_GAME_BOARD, PZ_GAME_ROM,
    PZ_STATES,                                 /* inside PZ_HANDOFF: the states' textures, PZ_TEXTURES in it */
    PZ_SND_LISTS,                              /* inside PZ_SND_FRAME: PZ_LISTS1's stages that ran in it, above it */
    PZ_GX_RUN,                                 /* inside PZ_FRAME_END: libctru's queue started, */
    PZ_GX_SUBMIT,                              /* its commands handed to GSP there (items: command lists), */
    PZ_GX_LATE,                                /* and those handed over later, when one before them is done */
    PZ_GPU_FILL, PZ_GPU_FILL1, PZ_GPU_COPIES, PZ_GPU_3D,   /* MVC2_GPU_SPLIT, inside PZ_GPU_LIST: the clear's two
                                                  buffers, the copies after it, the list after them */
    PZ_GPU_OUT, PZ_GPU_AFTER,                  /* the output's transfer after the list, then until C3D_FrameBegin */
    PZ_TEX_STAGE,                              /* inside PZ_DRAWS: MVC2_TEX_VRAM's copies from the staging ring queued */
    PZ_GPU_IDLE,                               /* from C3D_FrameBegin to the frame's first job sent: C3D_FrameEnd, or
                                                  MVC2_LIST_SPLIT's first part */
    PZ_TL_C_REJECT, PZ_TL_C_WAY1, PZ_TL_C_WAY2,  /* inside PZ_TL_CORNER, counts: groups not tested, the memo's answers, */
    PZ_TL_C_AT, PZ_TL_C_BIG, PZ_TL_C_PAST,     /* the texels' areas kept, the tests too big, the triangles after that */
    PZ_PACK_BYTES, PZ_PACK_LINES,              /* inside PZ_LIST_GPREP, MVC2_PREP_PACK's counts: the records' bytes and */
    PZ_PACK_TOUCH, PZ_PACK_DISTINCT,           /* lines, and the lines read in their place without it, each and once */
    PZ_LN_DRAWS,                               /* MVC2_LINE_COUNT=1, counts: the model draws' cache lines by what */
    PZ_LN_STATES = PZ_LN_DRAWS + 24,           /* holds them, each and once, then all once and the groups; the same */
    PZ_GD_OPEN = PZ_LN_STATES + 18,            /* for the states loop. The DC's disc on the game's thread: the files
                                                  opened and closed, */
    PZ_GD_READ,                                /* and read there, or waited for and taken */
    PZ_COUNT
};

/* the places outside the game the sampler names */
enum { PP_PRESENT = 1, PP_SND_WAIT, PP_ROM_READ };

#ifdef MVC2_PROF
uint32_t prof_now(void);                    /* the system tick, 268 MHz */
extern uint32_t prof_read_ticks;            /* what a read of it takes, from the calibration */
void prof_add(int zone, uint32_t ticks);    /* a call's time */
uint32_t prof_lists1(void);                 /* PZ_LISTS1's ticks so far */
uint32_t prof_ticks(int zone);              /* a zone's ticks so far */
void prof_count(int zone, uint32_t n);      /* items counted with the zone, untimed */
void prof_frame(void);                      /* the end of a frame, on the game's thread */
void prof_start(void);                      /* first thing: the calibration, and the writer */
void prof_place(uint32_t place);            /* MVC2_PROF_FUNCS: where the game's thread is, outside the game */
void prof_perf_models(int stop, uint32_t* v);   /* MVC2_PERFZONES: core 0's events over the models (v: 3 words) */
void reg_check_frame(const uint32_t* w, uint32_t n);    /* MVC2_REG_CHECK: a frame's commands, before they are sent */
void gpu_split_init(void);                  /* ctr_gpuprobe.c: MVC2_GPU_SPLIT and MVC2_GPU_PROBE */
void gpu_split_p3d(void);                   /* the list's interrupt */
void gpu_split_begun(uint32_t wait_from);   /* C3D_FrameBegin returned, its wait begun at wait_from */
void gpu_split_take(void);                  /* before C3D_FrameEnd: the last frame's jobs into their zones */
void gpu_split_sent(void);                  /* after C3D_FrameEnd, or MVC2_LIST_SPLIT's first part */
void gpu_probe_frame(uint32_t* w, uint32_t n);  /* a frame's commands, before they are sent */
int gpu_probe_nolight(void);                /* the lighting to be unbound */
void gpu_split_events(char* out, unsigned size);  /* the interrupts stamped since the last take, for a report */
int gpu_probe_on(void);                     /* MVC2_GPU_PROBE given, which changes the frame's words before they are sent */
void ctr_xmtr_bench(void);                  /* ctr_game_zones.c: MVC2_XMTR_BENCH */
void prof_stop(void);
#define PROF_NOW() prof_now()
#define PROF_START(v) uint32_t v = prof_now()
#define PROF_STOP(zone, v) prof_add((zone), prof_now() - (v))
#define PROF_ADD(zone, ticks) prof_add((zone), (uint32_t)(ticks))
#define PROF_COUNT(zone, n) prof_count((zone), (n))
#define PROF_FRAME() prof_frame()
#ifdef MVC2_PROF_FUNCS
#define PROF_PLACE(p) prof_place(p)
#else
#define PROF_PLACE(p) ((void)0)
#endif
#else
#define PROF_NOW() 0u
#define PROF_START(v) ((void)0)
#define PROF_STOP(zone, v) ((void)0)
#define PROF_ADD(zone, ticks) ((void)0)
#define PROF_COUNT(zone, n) ((void)0)
#define PROF_FRAME() ((void)0)
#define PROF_PLACE(p) ((void)0)
#endif

#endif
