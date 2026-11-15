/* ctr_code.c  Memory that code can be written into and run from, for the sound CPU's translator */

#include <3ds.h>

#include "arm7_jit.h"
#include "naomi_host.h"

#define CODE_SIZE (1024 * 1024)

static uint8_t code[CODE_SIZE] __attribute__((aligned(0x1000)));
static Handle self;
static int ready;

static Result flush_res;            /* the probe's last results, for the log */
static uint32_t icache_r0;

/* Luma's svcInvalidateEntireInstructionCache (0x94), for processes allowed svcBackdoor; it also flushes the branch
   predictor. It returns nothing: r0 is whatever the kernel left there. */
static void icache_invalidate(void) {
    register uint32_t r0 __asm__("r0") = 0xFFFFFFFFu;
    __asm__ volatile("svc 0x94" : "+r"(r0) : : "r1", "r2", "r3", "r12", "memory");
    icache_r0 = r0;
}

static Result flush(void* p, uint32_t size) {
    flush_res = svcFlushProcessDataCache(self, (u32)p, size);
    if (R_FAILED(flush_res)) {
        return flush_res;
    }
    icache_invalidate();
    return 0;
}

#ifdef MVC2_PROF
/* the same code run from the program and from the code memory, timed, for the log: whether the code memory is
   cached, and what a cache miss costs */
__attribute__((naked, noinline)) static void text_empty(void) {
    __asm__ volatile("bx lr");
}
__attribute__((naked, noinline)) static void text_loop(void) {
    __asm__ volatile("mov r0, #1024\n1:\tsubs r0, r0, #1\n\tbne 1b\n\tbx lr");
}
__attribute__((naked, noinline)) static void text_straight(void) {
    __asm__ volatile(".rept 1024\n\tmov r0, r0\n.endr\n\tbx lr");
}

static uint32_t timed(void (*f)(void), int cold) {
    u64 t;
    if (cold) {
        icache_invalidate();
    }
    t = svcGetSystemTick();
    f();
    return (uint32_t)(svcGetSystemTick() - t);
}

static void bench(void) {
    uint32_t* w = (uint32_t*)code;
    void (*f)(void) = (void (*)(void))(void*)code;
    uint32_t t[10];
    int i;
    timed(text_empty, 0);
    t[0] = timed(text_empty, 0);
    timed(text_loop, 0);
    t[1] = timed(text_loop, 0);
    t[2] = timed(text_straight, 1);
    t[3] = timed(text_straight, 0);
    w[0] = 0xE3A00B01u;                                 /* mov r0, #1024 */
    w[1] = 0xE2500001u;                                 /* subs r0, r0, #1 */
    w[2] = 0x1AFFFFFDu;                                 /* bne the subs */
    w[3] = 0xE12FFF1Eu;                                 /* bx lr */
    flush(code, 16);
    timed(f, 0);
    t[4] = timed(f, 0);
    for (i = 0; i < 16384; i++) {
        w[i] = i % 1024 == 1023 ? 0xEAFFFFFFu : 0xE1A00000u;  /* each 4 KB: mov r0, r0, then b on */
    }
    w[1023] = 0xE12FFF1Eu;
    flush(code, 16384 * 4);
    t[5] = timed(f, 1);
    t[6] = timed(f, 0);
    w[1023] = 0xEAFFFFFFu;
    w[16383] = 0xE12FFF1Eu;
    flush(code, 16384 * 4);
    t[7] = timed(f, 1);
    t[8] = timed(f, 0);
    host_log("code memory, in ticks: a call %lu; 1024 turns of a loop %lu in the program, %lu here; 4 KB straight "
             "%lu cold, %lu again in the program, %lu and %lu here; 64 KB %lu and %lu here",
             (unsigned long)t[0], (unsigned long)t[1], (unsigned long)t[4], (unsigned long)t[2], (unsigned long)t[3],
             (unsigned long)t[5], (unsigned long)t[6], (unsigned long)t[7], (unsigned long)t[8]);
}
#endif

/* a word of code written, run, written again and run again: the second must be what runs */
static const char* probe(int* ran) {
    uint32_t* w = (uint32_t*)code;
    int (*fn)(void) = (int (*)(void))(void*)code;
    s64 luma;
    int n;
    if (R_FAILED(svcGetSystemInfo(&luma, 0x10000, 0))) {
        return "not Luma";
    }
    for (n = 1; n <= 2; n++) {
        w[0] = 0xE3A00000u | (uint32_t)n;               /* mov r0, #n */
        w[1] = 0xE12FFF1Eu;                             /* bx lr */
        if (R_FAILED(flush(code, 8))) {
            return "the data cache call failed";
        }
        ran[n - 1] = fn();
        if (ran[n - 1] != n) {
            return "code written again didn't run";
        }
    }
    return NULL;
}

void* host_code(uint32_t* size) {
    if (!ready) {
        const char* why = NULL;
        int ran[2] = { 0, 0 };
        host_log("sound CPU: trying the translator");   /* a crash in the calls below stops after this line */
        if (R_FAILED(svcDuplicateHandle(&self, CUR_PROCESS_HANDLE)) ||
            R_FAILED(svcControlProcessMemory(self, (u32)code, (u32)code, CODE_SIZE, MEMOP_PROT,
                                             MEMPERM_READ | MEMPERM_WRITE | MEMPERM_EXECUTE))) {
            why = "no executable memory";
        } else {
            why = probe(ran);
        }
#ifdef MVC2_PROF
        if (why == NULL) {
            bench();
        }
#endif
        host_log("sound CPU: %s%s (data cache call %08lX, svc 0x94 r0 %08lX, the probe ran %d then %d)",
                 why ? "interpreted: " : "translated", why ? why : "", (unsigned long)flush_res,
                 (unsigned long)icache_r0, ran[0], ran[1]);
        ready = why ? -1 : 1;
    }
    if (ready < 0) {
        return NULL;
    }
    *size = CODE_SIZE;
    return code;
}

/* this core's caches: the data written out, the instructions read again */
void host_code_flush(void* p, uint32_t size) {
    flush(p, size);
}
