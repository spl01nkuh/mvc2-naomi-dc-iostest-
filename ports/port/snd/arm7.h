/*
 * arm7.h  The AICA's sound CPU: an ARM7 (ARM instructions, no Thumb), interpreted
 */

#ifndef ARM7_H
#define ARM7_H

#include <stdint.h>

/* its memory, from aica.c: the sound RAM at 0, the AICA's registers at 0x800000 */
uint32_t aica_arm_read32(uint32_t addr);
uint32_t aica_arm_read16(uint32_t addr);
uint32_t aica_arm_read8(uint32_t addr);
void aica_arm_write32(uint32_t addr, uint32_t v);
void aica_arm_write16(uint32_t addr, uint32_t v);
void aica_arm_write8(uint32_t addr, uint32_t v);
extern uint8_t* aica_ram;
extern uint32_t aica_ram_mask;

void arm7_reset(void);
int arm7_run(int cycles);           /* runs about that many cycles; gives back how many it ran */
int arm7_run_fiq(int cycles);       /* the FIQ's handler at once, if it is due */
void arm7_stop(void);               /* from a write it makes: the run ends after that instruction */
void arm7_set_fiq(int on);          /* the AICA's interrupt line */
uint32_t arm7_pc(void);
uint64_t arm7_cycles(void);         /* run so far, for measuring */
void arm7_fast(int on);             /* loops that scan a table run many turns at once, exactly */
void arm7_small(int on);            /* the rarer instructions through one shared decoder: less code; 2: all but
                                       the sound driver's commonest */
void arm7_track(int on);            /* writes drop the decoded words they cover, so no fetch reads its word: the same result */
void arm7_fiq_fast(int on);         /* the sound driver's FIQ handler run natively, while its words are its own: the same result */
void arm7_dec8(int on);             /* the decode table at 8 bytes an instruction, not 16: the same result */
void arm7_translate(int on);
void arm7_translate2(int mode);     /* MVC2_ARMJIT2: the translator that keeps the guest's registers in the host's */        /* whether runs may use translated code, where the host has it */
int arm7_run_interpreted(int cycles);   /* arm7_run without it: for another thread than the sound's */
void arm7_code_written(uint32_t addr, uint32_t size);   /* the RAM written from outside: its code checked before it runs */
uint32_t arm7_code_changes(void);   /* counts those writes to the code's 32 KB */

/* for snd_drv.c, which runs the driver's routines as C */
typedef struct {
    uint32_t r[16], cpsr, spsr, b8[2][5], b13[6], b14[6], bs[6];
    int fiq_line;
} ARM7_REGS;
void arm7_get_regs(ARM7_REGS* s);
void arm7_set_regs(const ARM7_REGS* s);
int arm7_fiq_line(void);
int arm7_fiq_due(void);             /* the FIQ would be taken now, from a mode other than FIQ */
/* the FIQ's handler run elsewhere up to its last instruction, at `at` after `used` cycles: the CPU put as the
   handler leaves it there (r8 the saved status, r10-r12 its bases, its stack if it set it, r14 the return), and
   the rest run as arm7_run_fiq(cycles) would. Gives back the cycles in all */
int arm7_fiq_finish(uint32_t at, int used, int cycles, uint32_t r10, uint32_t r11, uint32_t r12, int sp_set,
                    uint32_t sp);
void arm7_fiq_return(uint32_t r10, uint32_t r11, uint32_t r12, int sp_set, uint32_t sp);  /* the same, and its
                                                                                           return made at once */

#endif
