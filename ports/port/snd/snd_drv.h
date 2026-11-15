/*
 * snd_drv.h  The sound CPU's driver (Sega AM2's AICA driver, 1.76) as C, in place of the interpreter
 */

#ifndef SND_DRV_H
#define SND_DRV_H

void snd_drv_set(int mode);         /* MVC2_SNDNATIVE: 1 the FIQ, 2 it checked against the interpreter, 3 the FIQ
                                       and the main loop; 4 the FIQ and each pass of the main loop checked */
int snd_drv_fiq(void);              /* the timer's FIQ, if due: 1 if run here, 0 to leave it to the interpreter */
int snd_drv_main(int cycles, int rate);  /* the main loop for about that long, at rate cycles a sample; or -1 to
                                            leave it to the interpreter */

#endif
