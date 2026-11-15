/* arm7_jit.h  The sound CPU's translator to the host's own ARM code, between arm7.c and arm7_jit.c */

#ifndef ARM7_JIT_H
#define ARM7_JIT_H

#include <stdint.h>

#define ARM7_JIT_WORDS 8192         /* the code it translates: the sound RAM's first 32 KB */

/* arm7.c's state, for the translated code */
typedef struct {
    uint32_t* r;
    uint32_t* cpsr;
    int* event;
    uint8_t** ram;
    uint32_t* ram_mask;
} ARM7_JIT_STATE;

int arm7_jit_init(ARM7_JIT_STATE* state);       /* 0: no memory to run code from */
void arm7_jit_reset(void);
void arm7_jit_check(void);                      /* the guest's code may have been written: blocks checked again */
int arm7_jit_covers(uint32_t addr);             /* a word some block was made from */
int arm7_jit_run(uint32_t pc, int done, int cycles);    /* the cycles done after a block, or -1 to interpret */
extern uint32_t arm7_jit_range[2];              /* the lowest translated byte, and the span to the highest */

/* arm7_jit2.c: the guest's registers kept in the host's. mode: its kinds (1 ALU and branches, 2 loads, 4 stores,
   8 LDM and STM), 16 to check each block against the interpreter */
int arm7_jit2_init(ARM7_JIT_STATE* state, int mode);
void arm7_jit2_reset(void);
void arm7_jit2_check(void);
int arm7_jit2_covers(uint32_t addr);
int arm7_jit2_goes_on(uint32_t p, uint32_t to);     /* the block arm7_jit2_run was last asked about */
int arm7_jit2_run(uint32_t pc, int fiq, int done, int cycles, uint32_t* end);  /* end: only whether it would run */
int arm7_jit2_slow(uint32_t site);
extern uint32_t arm7_jit2_range[2];
extern const uint32_t arm7_jit2_tables;    /* bytes, taken from the heap while it is on */

#ifdef MVC2_PROF
/* counted in profiling builds, for the sound's report */
typedef struct {
    uint64_t jit_cycles;                        /* guest cycles run translated (arm7.c) */
    uint32_t entries, refused;                  /* blocks entered from C; entries left to the interpreter */
    uint32_t made, words, made_ticks;           /* blocks translated, their host words, the time it took */
    uint32_t flushes, checks, rechecked, remade, resets;
    uint32_t steps;                             /* instructions the translated code left to the interpreter */
    uint32_t held;                              /* blocks kept now */
    uint32_t hot_held, cold_held, guest_held;   /* arm7_jit2's: their host words, hot and cold, and guest words */
    uint32_t links, checked, wrong;             /* exits pointed at their blocks; blocks checked, and wrong */
    uint32_t compactions;                       /* the hot blocks made again in order */
} ARM7_JIT_STATS;
extern ARM7_JIT_STATS arm7_jit_stats;
void arm7_jit_dump(void (*out)(const void* p, uint32_t size));
#define JIT_STAT(x) (x)
#else
#define JIT_STAT(x) ((void)0)
#endif

/* arm7.c's, called from the translated code */
int arm7_jit_step(uint32_t op, uint32_t pc);
uint32_t arm7_jit_ldr(uint32_t a);
uint32_t arm7_jit_ldrb(uint32_t a);
void arm7_jit_str(uint32_t a, uint32_t v);
void arm7_jit_strb(uint32_t a, uint32_t v);

/* the host's: memory to write code into and run, and making what was written runnable on this core */
void* host_code(uint32_t* size);
void host_code_flush(void* p, uint32_t size);

#endif
