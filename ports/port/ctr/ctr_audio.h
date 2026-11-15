/*
 * ctr_audio.h  The 3DS's sound out (ctr_audio.c), for main_3ds.c
 */

#ifndef CTR_AUDIO_H
#define CTR_AUDIO_H

#include <stdint.h>

void ctr_audio_init(void);      /* after start_game: NDSP, and the sound's thread on the second core */
void ctr_audio_frame(void);     /* after each naomi_run_frame: the frame's sound is made beside the next frame */
void ctr_audio_idle(void);      /* before switch_game: waits until the thread is done with the sound's state */
void ctr_audio_exit(void);      /* at the end */
void ctr_audio_dump(void);      /* the sound chip's sounding channels to the log (SELECT+Y) */
void ctr_audio_quiet(void);     /* turned down, without waiting for the sound's thread */
float ctr_audio_own_ms(void);   /* the last frame handed over: its time on core 1, less the lists' stages over it */
/* 1 while the frame handed over is being made, with core 1's list ticks when it was (ctr_lists1_busy) and the ticks
   of its own it is expected to take */
int ctr_audio_making(uint32_t* busy_at_go, uint32_t* need);

#endif
