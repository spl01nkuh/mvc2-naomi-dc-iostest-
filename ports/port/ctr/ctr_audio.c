/*
 * ctr_audio.c  The 3DS's sound out: each frame's sound made on the second core beside the next frame, so it is the
 * PC's sample for sample, and queued to NDSP about three frames ahead, as on the PC.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <3ds.h>

#include "ctr_audio.h"
#include "ctr_render.h"
#include "naomi_host.h"
#include "snd.h"
#include "prof.h"
#include "aica.h"
#ifdef MVC2_PROF
#include "arm7.h"
#include "arm7_jit.h"
#endif

#define CHANNEL 0
#define BUFS 8                      /* frames NDSP can hold */
#define BUF_SAMPLES 2048            /* two frames: a longer one when the game falls behind */
#define TARGET 3.0                  /* frames kept queued */
#define FRAME_SAMPLES (SND_RATE / 59.94)
#define REPORT 600
#define FRAME_US 16683
#define FILL_WAIT_NS 4000000        /* how often the idle thread looks at the queue, with MVC2_SNDFILL */

static int on, threaded;
static Thread thread;
static LightEvent go, done;         /* go: a frame to make; done: the thread is not making one */
static LightLock lock;              /* done and pending, between the two threads */
static struct {
    SND_WORK work;
    int a, b;
} pending[32];                      /* calls' work for the thread, after the frame */
static int pending_n;
static volatile int quit;
static ndspWaveBuf wave[BUFS];
static int16_t* pcm;                /* the buffers' samples, in linear memory */
static int next_buf;
static double level = -1;           /* the queue, smoothed, in frames */
static unsigned frames, last_dry;
static int direct_flush;            /* the kernel's cache call, not an IPC to the DSP service on the busy core */
static int pace;                    /* MVC2_SNDPACE: a longer frame after a slow one, so the sound keeps time */
static int fill;                    /* MVC2_SNDFILL: frames made while the game stalls, to keep the queue */
static int room1;                   /* MVC2_SNDROOM: a longer frame only as long as core 1 has room for, beside the lists */
static int held = 1;                /* no frame between the game's: from its frame's end to the next go */
static int calls;                   /* the game's sound calls under way */
static u64 last_frame;
static int32_t owed;                /* the real time since the game's frames began, less the sound made, in us */
static volatile unsigned extra_made;
static unsigned extra_paid;
static unsigned longer, extra;      /* for the log */
static volatile float cost;         /* the thread's time for each us of sound it makes, smoothed */
static float game_us = FRAME_US;    /* the game's own time a frame, less its waits on the sound, smoothed */
static volatile u32 go_tick;        /* when the game's last frame was handed over */
static int32_t next_more, go_more;  /* the us a frame is made longer by: the next one's, and the one handed over's */
static float turn_us;               /* with MVC2_SNDROOM: from a frame's go to its done, less its own longer part, smoothed */
static u64 wait_all, wait_seen;     /* the game's waits on the sound, all of them */
static volatile int making;         /* the frame handed over not yet made */
static volatile u32 busy_go;        /* core 1's list ticks when it was handed over */
static volatile u32 own_last, own_need;     /* a frame's own ticks: the last one's, and the larger of it and the smoothed */
static float own_avg;
static u64 own_ticks;               /* for the log */

/* for the log, over each REPORT frames */
static u64 make_ticks, wait_ticks;
static int dry, dropped;
static double lo = 99, hi;

#ifdef MVC2_PROF
static int ab;                      /* MVC2_ARMJIT=2: translated every other frame, each kind timed */
static u64 ab_ticks[2], ab_cycles[2];
static unsigned ab_frames[2];
static int sweep, sweep_chunk, sweep_dec8, sweep_track, sweep_fiq;  /* MVC2_SNDSWEEP=1: from frame 3601 a setting a window, in turn */
static int probe;                   /* MVC2_SNDPROBE=1: a register loop and a chase through memory, timed each frame */
static uint32_t probe_buf[32 * 1024 / 4];
static u64 probe_sum[2], probe_least[2];
static unsigned probe_n;
#define PROBE_STEPS 4096
#define PROBE_LINES (sizeof probe_buf / 32)

static void probe_run(void) {
    u64 t[3];
    uint32_t i, x = 1, at = 0;
    int k;
    t[0] = svcGetSystemTick();
    for (i = 0; i < PROBE_STEPS; i++) {
        x = x * 1664525u + 1013904223u;
        __asm__ volatile("" : "+r"(x));
    }
    t[1] = svcGetSystemTick();
    for (i = 0; i < PROBE_LINES; i++) {
        at = ((volatile uint32_t*)probe_buf)[at];
    }
    t[2] = svcGetSystemTick();
    probe_buf[PROBE_LINES * 8 - 1] = x + at;
    for (k = 0; k < 2; k++) {
        u64 d = t[k + 1] - t[k];
        probe_sum[k] += d;
        probe_least[k] = probe_n == 0 || d < probe_least[k] ? d : probe_least[k];
    }
    probe_n++;
}

/* MVC2_PERFCTR=1: the CPU's event counters, two events a window in turn, on each core */
static int perf, perf_pair, perf_set[2] = { -1, -1 }, perf_fiq_on;
static const PerfCounterEvent perf_ev[4][2] = {
    { PERFCOUNTEREVT_CORE_INST_CACHE_MISS, PERFCOUNTEREVT_CORE_INST_EXECUTED },
    { PERFCOUNTEREVT_CORE_DATA_CACHE_READ_MISS, PERFCOUNTEREVT_CORE_DATA_CACHE_WRITE_MISS },
    { PERFCOUNTEREVT_CORE_BRANCH_MISS_PREDICTED, PERFCOUNTEREVT_CORE_STALL_BY_LACK_OF_INST },
    { PERFCOUNTEREVT_CORE_STALL_BY_DATA_HAZARD, PERFCOUNTEREVT_CORE_DATA_CACHE_READ },
};
static const char* const perf_ev_name[4][2] = {
    { "I-cache misses", "instructions" }, { "D-cache read misses", "write misses" },
    { "mispredicted branches", "stalls for instructions" }, { "stalls for data", "D-cache reads" },
};
enum { PF_ARM, PF_FIQ, PF_MIX, PF_SOUND, PF_CORE0, PF_N };
static const char* const perf_zone[PF_N] = { "arm", "fiq (1 in 8)", "channels", "sound frame", "core 0's frame" };
static u64 perf_sum[PF_N][4];
static unsigned perf_n[PF_N], perf_fiqs;
static u32 perf_at[4], perf_fiq_at[4], perf_snd_at[4], perf_core0_at[4];
static int perf_core0_ok;

static void perf_read(u32* v) {
    u64 x = 0;
    svcControlPerformanceCounter(&x, PERFCOUNTEROP_GET_VALUE, PERFCOUNTERREG_CORE_COUNT_REG_0, 0);
    v[0] = (u32)x;
    svcControlPerformanceCounter(&x, PERFCOUNTEROP_GET_VALUE, PERFCOUNTERREG_CORE_COUNT_REG_1, 0);
    v[1] = (u32)x;
    svcControlPerformanceCounter(&x, PERFCOUNTEROP_GET_VALUE, PERFCOUNTERREG_CORE_CYCLE_COUNTER, 0);
    v[2] = (u32)x;
    v[3] = (u32)svcGetSystemTick();
}

static void perf_add(int z, const u32* from, const u32* to) {
    int k;
    for (k = 0; k < 4; k++) {
        perf_sum[z][k] += to[k] - from[k];
    }
    perf_n[z]++;
}

/* this core's counters to the window's events; 1 when they were changed */
static int perf_events(int core) {
    u64 x;
    if (perf_set[core] == perf_pair) {
        return 0;
    }
    svcControlPerformanceCounter(&x, PERFCOUNTEROP_SET_EVENT, PERFCOUNTERREG_CORE_COUNT_REG_0, perf_ev[perf_pair][0]);
    svcControlPerformanceCounter(&x, PERFCOUNTEROP_SET_EVENT, PERFCOUNTERREG_CORE_COUNT_REG_1, perf_ev[perf_pair][1]);
    perf_set[core] = perf_pair;
    return 1;
}

static void perf_mix(int at) {
    u32 v[4];
    if (threadGetCurrent() != thread) {
        return;     /* the game's own runs of the sound CPU */
    }
    switch (at) {
    case AICA_PERF_CHUNK:
        perf_read(perf_at);
        break;
    case AICA_PERF_ARM_END:
        perf_read(v);
        perf_add(PF_ARM, perf_at, v);
        memcpy(perf_at, v, sizeof v);
        break;
    case AICA_PERF_MIX_END:
        perf_read(v);
        perf_add(PF_MIX, perf_at, v);
        break;
    case AICA_PERF_FIQ:
        perf_fiq_on = (perf_fiqs++ & 7) == 0;
        if (perf_fiq_on) {
            perf_read(perf_fiq_at);
        }
        break;
    case AICA_PERF_FIQ_END:
        if (perf_fiq_on) {
            perf_read(v);
            perf_add(PF_FIQ, perf_fiq_at, v);
        }
        break;
    }
}

static void perf_report(void) {
    char line[640];
    int z, n;
    n = snprintf(line, sizeof line, "perf: %s and %s a 1000 cycles, and cycles a call:", perf_ev_name[perf_pair][0],
                 perf_ev_name[perf_pair][1]);
    for (z = 0; z < PF_N && n < (int)sizeof line; z++) {
        double c = (double)perf_sum[z][2];
        n += snprintf(line + n, sizeof line - n, "%s %s %.1f, %.1f, %.0f", z ? ";" : "", perf_zone[z],
                      c ? perf_sum[z][0] * 1000.0 / c : 0.0, c ? perf_sum[z][1] * 1000.0 / c : 0.0,
                      perf_n[z] ? c / perf_n[z] : 0.0);
    }
    if (n < (int)sizeof line && perf_sum[PF_SOUND][3]) {
        snprintf(line + n, sizeof line - n, "; %.3f cycles a tick", (double)perf_sum[PF_SOUND][2] / perf_sum[PF_SOUND][3]);
    }
    host_log("%s", line);
    memset(perf_sum, 0, sizeof perf_sum);
    memset(perf_n, 0, sizeof perf_n);
    perf_pair = (perf_pair + 1) % 4;
}
#endif
static const char* sweep_name = "";

static double ms(u64 ticks) {
    return ticks * 1000.0 / SYSCLOCK_ARM11;
}

static double queued_frames(void) {
    double n = 0;
    int i;
    for (i = 0; i < BUFS; i++) {
        if (wave[i].status == NDSP_WBUF_QUEUED || wave[i].status == NDSP_WBUF_PLAYING) {
            n += wave[i].nsamples;
        }
    }
    if (n > 0) {
        n -= ndspChnGetSamplePos(CHANNEL);
    }
    return n / FRAME_SAMPLES;
}

static void put(const int16_t* s, int n) {
    ndspWaveBuf* w = &wave[next_buf];
    if (w->status == NDSP_WBUF_QUEUED || w->status == NDSP_WBUF_PLAYING) {
        dropped++;
        return;
    }
    n = n < BUF_SAMPLES ? n : BUF_SAMPLES;
    if (s != NULL) {
        memcpy(w->data_pcm16, s, (size_t)n * 4);
    } else {
        memset(w->data_pcm16, 0, (size_t)n * 4);
    }
    w->nsamples = (u32)n;
    if (direct_flush && R_FAILED(svcFlushProcessDataCache(CUR_PROCESS_HANDLE, (u32)w->data_pcm16, (u32)n * 4))) {
        direct_flush = 0;
        host_log("sound: the direct cache call failed, through the DSP service from now");
    }
    if (!direct_flush) {
        DSP_FlushDataCache(w->data_pcm16, (u32)n * 4);
    }
    ndspChnWaveBufAdd(CHANNEL, w);
    next_buf = (next_buf + 1) % BUFS;
}

/* the frame's samples to NDSP, at a rate that holds the queue near TARGET */
static void output(const int16_t* s, int n) {
    double q = queued_frames(), stretch;
    if (q < 0.25) {
        /* ran dry: after a stall, silence ahead to fill the queue again; when the game keeps falling behind,
           only the frame (more silence would only cut its sound up) */
        if (frames - last_dry > 120) {
            put(NULL, (int)FRAME_SAMPLES);
            put(NULL, (int)FRAME_SAMPLES);
        }
        last_dry = frames;
        q = level = queued_frames();
        dry++;
    } else if (q > BUFS - 2) {
        level = TARGET;
        dropped++;
        return;
    }
    lo = q < lo ? q : lo;
    hi = q > hi ? q : hi;
    level = level < 0 ? q : level + (q - level) * 0.05;
    stretch = (level - TARGET) * 0.0025;     /* a faster rate takes the queue down */
    stretch = stretch > 0.005 ? 0.005 : stretch < -0.005 ? -0.005 : stretch;
    ndspChnSetRate(CHANNEL, (float)(SND_RATE * (1.0 + stretch)));
    put(s, n);
}

static void note_cost(u64 ticks, int n) {
    float c = (float)ticks * 1e6f / SYSCLOCK_ARM11 / ((float)n * 1e6f / SND_RATE);
    cost = cost == 0 ? c : cost + (c - cost) * 0.1f;
}

static void make(void) {
    const int16_t* s;
    u64 t0;
#ifdef MVC2_PROF
    if (probe) {
        probe_run();
    }
    if (perf && threaded) {
        perf_events(1);
        perf_read(perf_snd_at);
    }
#endif
    t0 = svcGetSystemTick();
#ifdef MVC2_PROF
    int k = (ab_frames[0] + ab_frames[1]) & 1;
    uint64_t c0 = arm7_cycles();
    if (ab) {
        arm7_translate(k);
    }
#endif
    PROF_START(f);
#ifdef MVC2_PROF
    uint32_t lists0 = prof_lists1();
#endif
    int n = snd_frame(&s);
    PROF_STOP(PZ_SND_FRAME, f);
#ifdef MVC2_PROF
    PROF_ADD(PZ_SND_LISTS, prof_lists1() - lists0);
#endif
    make_ticks += svcGetSystemTick() - t0;
#ifdef MVC2_PROF
    if (perf && threaded) {
        u32 v[4];
        perf_read(v);
        perf_add(PF_SOUND, perf_snd_at, v);
    }
#endif
    if (n > 0) {
        note_cost(svcGetSystemTick() - t0, n);
    }
#ifdef MVC2_PROF
    ab_ticks[k] += svcGetSystemTick() - t0;
    ab_cycles[k] += arm7_cycles() - c0;
    ab_frames[k]++;
#endif
    if (n > 0) {
        PROF_START(t);
        output(s, n);
        PROF_STOP(PZ_SND_OUT, t);
    }
}

static void make_extra(void) {
    const int16_t* s;
    u64 t0 = svcGetSystemTick();
    int n = snd_frame_extra(&s);
    make_ticks += svcGetSystemTick() - t0;
    if (n > 0) {
        note_cost(svcGetSystemTick() - t0, n);
        output(s, n);
        extra++;
        extra_made++;
    }
}

/* with MVC2_SNDFILL: waits for the game's frame; meanwhile, while the game hasn't used the sound, a frame made
   whenever the queue runs low. 1: an extra frame is to be made */
static int wait_go(void) {
    int take = 0;
    if (!fill) {
        LightEvent_Wait(&go);
        return 0;
    }
    while (!take && LightEvent_WaitTimeout(&go, FILL_WAIT_NS)) {
        LightLock_Lock(&lock);
        take = !held && calls == 0 && queued_frames() < 1.5 &&
               (float)(u32)(svcGetSystemTick() - go_tick) * 1e6f / SYSCLOCK_ARM11 > game_us * 1.25f;
        if (take) {
            LightEvent_Clear(&done);
        }
        LightLock_Unlock(&lock);
    }
    return take;
}

static void sound_thread(void* arg) {
    int i;
    uint32_t fpscr;
    (void)arg;
    __asm__ volatile("vmrs %0, fpscr" : "=r"(fpscr));
    host_log("FPSCR %08lX on the sound thread", (unsigned long)fpscr);
    for (;;) {
        int between;
        PROF_START(idle);
        between = wait_go();
        PROF_STOP(PZ_SND_IDLE, idle);
        if (quit) {
            break;
        }
        u32 own0 = (u32)svcGetSystemTick(), busy0 = ctr_lists1_busy();
        if (between) {
            make_extra();
        } else {
            make();
        }
        PROF_START(t);
        LightLock_Lock(&lock);
        for (i = 0; i < pending_n; i++) {
            LightLock_Unlock(&lock);
            pending[i].work(pending[i].a, pending[i].b);
            LightLock_Lock(&lock);
        }
        PROF_COUNT(PZ_SND_DEFER, (uint32_t)pending_n);
        pending_n = 0;
        PROF_STOP(PZ_SND_DEFER, t);
        if (room1 && !between) {
            /* the lists, above the sound on core 1, are in the time it took */
            float turn = (float)(u32)(svcGetSystemTick() - go_tick) * 1e6f / SYSCLOCK_ARM11 - cost * (float)go_more;
            turn_us = turn_us == 0 || turn > turn_us ? turn : turn_us + (turn - turn_us) * 0.1f;
        }
        if (!between) {
            u32 own = (u32)svcGetSystemTick() - own0 - (ctr_lists1_busy() - busy0);
            own_avg += ((float)own - own_avg) * 0.25f;
            own_last = own;
            own_need = (float)own > own_avg ? own : (u32)own_avg;
            own_ticks += own;
            making = 0;
        }
        LightEvent_Signal(&done);
        LightLock_Unlock(&lock);
        if (!between) {
            ctr_lists1_sound_done();
        }
    }
}

float ctr_audio_own_ms(void) {
    return (float)own_last * 1000.0f / SYSCLOCK_ARM11;
}

int ctr_audio_making(uint32_t* busy_at_go, uint32_t* need) {
    if (!making) {
        return 0;
    }
    *busy_at_go = busy_go;
    *need = own_need;
    return 1;
}

static void wait_done(int zone) {
    u64 t0;
    if (threadGetCurrent() == thread || LightEvent_TryWait(&done)) {
        return;     /* the thread's own calls (pending work) wait for nothing */
    }
    t0 = svcGetSystemTick();
    PROF_START(t);
    PROF_PLACE(PP_SND_WAIT);
    LightEvent_Wait(&done);
    PROF_STOP(zone, t);
    wait_ticks += svcGetSystemTick() - t0;
    wait_all += svcGetSystemTick() - t0;
    PROF_COUNT(zone, 1);
}

/* none between the game's frames from now to the next go */
static void hold(void) {
    if (fill) {
        LightLock_Lock(&lock);
        held = 1;
        LightLock_Unlock(&lock);
    }
}

static void call_begin(void) {
    if (fill && threadGetCurrent() != thread) {
        LightLock_Lock(&lock);
        calls++;
        LightLock_Unlock(&lock);
    }
}

static void call_end(void) {
    if (fill && threadGetCurrent() != thread) {
        LightLock_Lock(&lock);
        calls--;
        LightLock_Unlock(&lock);
    }
}

static void wait_mid(void) {
    wait_done(PZ_SND_SYNC_MID);
}

static int defer(SND_WORK work, int a, int b) {
    int taken = 0;
    LightLock_Lock(&lock);
    if (!LightEvent_TryWait(&done) && pending_n < 32) {
        pending[pending_n].work = work;
        pending[pending_n].a = a;
        pending[pending_n].b = b;
        pending_n++;
        taken = 1;
    }
    LightLock_Unlock(&lock);
    return taken;
}

static void report(void) {
    if (++frames % REPORT != 0) {
        return;
    }
    host_log("audio: %.2f ms a frame (%.0f%% of one) on core %d, %.2f of its own; the game waited %.2f ms a frame; "
             "queue %.2f to %.2f frames, %d ran dry, %d dropped%s", ms(make_ticks) / REPORT, ms(make_ticks) / REPORT * 5.994,
             threaded ? 1 : 0, ms(own_ticks) / REPORT, ms(wait_ticks) / REPORT, lo, hi, dry, dropped, sweep_name);
#ifdef MVC2_PROF
    if (sweep && frames >= 6 * REPORT) {
        static const char* const names[] = { "; sweep: the defaults", "; sweep: chunk 128", "; sweep: ARMDEC8=0",
                                             "; sweep: chunk 64", "; sweep: ARMTRACK=0", "; sweep: FIQFAST=0" };
        int k = (int)(frames / REPORT - 6) % 6;
        aica_set_chunk(k == 1 ? 128 : k == 3 ? 64 : sweep_chunk);
        arm7_dec8(k == 2 ? 0 : sweep_dec8);
        arm7_track(k == 4 ? 0 : sweep_track);
        arm7_fiq_fast(k == 5 ? 0 : sweep_fiq);
        sweep_name = names[k];
    }
    {
        static uint64_t last_cycles;
        ARM7_JIT_STATS* s = &arm7_jit_stats;
        uint64_t all = arm7_cycles() - last_cycles;
        host_log("sound CPU a frame: %.0f cycles, %.0f%% translated; %.1f blocks entered, %.1f refused, %.1f steps "
                 "interpreted in them; %.2f made (%.0f host words, %.3f ms), %.2f flushes, %.2f checks, %.1f rechecked, "
                 "%.2f made again; %lu resets, %lu blocks held",
                 (double)all / REPORT, all ? 100.0 * (double)s->jit_cycles / (double)all : 0.0,
                 (double)s->entries / REPORT, (double)s->refused / REPORT, (double)s->steps / REPORT,
                 (double)s->made / REPORT, (double)s->words / REPORT, ms(s->made_ticks) / REPORT,
                 (double)s->flushes / REPORT, (double)s->checks / REPORT, (double)s->rechecked / REPORT,
                 (double)s->remade / REPORT, (unsigned long)s->resets, (unsigned long)s->held);
        last_cycles += all;
        if (s->guest_held || s->checked) {
            host_log("sound CPU translated (2): %lu guest words held in %lu host words hot (%.2f a word) and %lu "
                     "cold; %lu links, %lu compactions; %.1f blocks checked a frame, %lu wrong",
                     (unsigned long)s->guest_held, (unsigned long)s->hot_held,
                     s->guest_held ? (double)s->hot_held / s->guest_held : 0.0, (unsigned long)s->cold_held,
                     (unsigned long)s->links, (unsigned long)s->compactions, (double)s->checked / REPORT, (unsigned long)s->wrong);
            s->checked = 0;
        }
        if (ab && ab_frames[0] && ab_frames[1]) {
            host_log("sound CPU every other frame: translated %.2f ms a frame (%.0f cycles), interpreted %.2f ms (%.0f)",
                     ms(ab_ticks[1]) / ab_frames[1], (double)ab_cycles[1] / ab_frames[1],
                     ms(ab_ticks[0]) / ab_frames[0], (double)ab_cycles[0] / ab_frames[0]);
        }
        if (perf) {
            perf_report();
        }
        if (probe_n) {
            host_log("sound probe: a register step %.2f ticks (least %.2f), a line from memory %.1f ticks (least %.1f)",
                     (double)probe_sum[0] / probe_n / PROBE_STEPS, (double)probe_least[0] / PROBE_STEPS,
                     (double)probe_sum[1] / probe_n / PROBE_LINES, (double)probe_least[1] / PROBE_LINES);
            probe_sum[0] = probe_sum[1] = 0;
            probe_n = 0;
        }
        ab_ticks[0] = ab_ticks[1] = ab_cycles[0] = ab_cycles[1] = 0;
        ab_frames[0] = ab_frames[1] = 0;
        s->jit_cycles = 0;
        s->entries = s->refused = s->made = s->words = s->made_ticks = 0;
        s->flushes = s->checks = s->rechecked = s->remade = s->resets = s->steps = 0;
    }
#endif
    if (pace || fill) {
        host_log("audio: %u frames made longer, %u made between the game's", longer, extra);
        if (room1) {
            host_log("audio: a frame of it done %.2f ms after it was handed over, of the game's %.2f", turn_us / 1000.0f,
                     game_us / 1000.0f);
        }
    }
    longer = extra = 0;
    make_ticks = wait_ticks = own_ticks = 0;
    dry = dropped = 0;
    lo = 99;
    hi = 0;
}

void ctr_audio_init(void) {
    float mix[12] = { 1.0f, 1.0f };
    s32 prio = 0x30;
    int core = getenv("MVC2_SNDCORE") != NULL ? atoi(getenv("MVC2_SNDCORE")) : 1;
    int i;
    if (on || !snd_enabled()) {
        return;
    }
#ifdef MVC2_PROF
    ab = getenv("MVC2_ARMJIT") != NULL && atoi(getenv("MVC2_ARMJIT")) == 2;
    sweep = getenv("MVC2_SNDSWEEP") != NULL && atoi(getenv("MVC2_SNDSWEEP")) != 0;
    probe = getenv("MVC2_SNDPROBE") != NULL && atoi(getenv("MVC2_SNDPROBE")) != 0;
    if (probe) {
        for (i = 0; i < (int)PROBE_LINES; i++) {
            probe_buf[i * 8] = (uint32_t)((i * 8 + 8) % (PROBE_LINES * 8));
        }
    }
    perf = getenv("MVC2_PERFCTR") != NULL && atoi(getenv("MVC2_PERFCTR")) != 0;
    if (perf && envIsHomebrew()) {
        host_log("perf counters: not from the Homebrew Launcher, whose exheader may not grant them");
        perf = 0;
    }
    if (perf) {
        u64 x;
        Result rc = svcControlPerformanceCounter(&x, PERFCOUNTEROP_ENABLE, 0, 0);
        if (R_FAILED(rc)) {
            host_log("perf counters: not to be had (%08lX)", (unsigned long)rc);
            perf = 0;
        } else {
            u32 a[4], b[4];
            perf_read(a);
            while ((u32)svcGetSystemTick() - a[3] < 100000u) {
            }
            perf_read(b);
            if (a[2] == b[2]) {
                host_log("perf counters: their cycle counter doesn't run (an emulator?), so off");
                svcControlPerformanceCounter(&x, PERFCOUNTEROP_DISABLE, 0, 0);
                perf = 0;
            } else {
                host_log("perf counters: on, two events a window in turn");
            }
        }
    }
#endif
    /* the whole model (MVC2_DSP=1, MVC2_ARMRATE=64) is too slow for an old 3DS; MVC2_REVERB=1 is the cheap
       reverb */
    pace = getenv("MVC2_SNDPACE") == NULL || atoi(getenv("MVC2_SNDPACE")) != 0;    /* on unless =0 */
    fill = getenv("MVC2_SNDFILL") == NULL || atoi(getenv("MVC2_SNDFILL")) != 0;
    room1 = getenv("MVC2_SNDROOM") != NULL && atoi(getenv("MVC2_SNDROOM")) != 0;
    setenv("MVC2_DSP", "0", 0);
    setenv("MVC2_ARMRATE", "16", 0);
    setenv("MVC2_ARMFAST", "1", 0);
    {
        bool n3ds = false;
        APT_CheckNew3DS(&n3ds);
#ifdef OLD_3DS_TEST
        n3ds = false;
#endif
        if (!n3ds) {
            setenv("MVC2_SNDCHUNK", "256", 0);     /* fewer turns between the sound CPU and the mixer */
            setenv("MVC2_ARMDEC8", "1", 0);        /* half the decode table in the cache */
            setenv("MVC2_FIQFAST", "0", 0);        /* fiq_native's 6.8 KB costs the interpreter more in misses */
        }
    }
#ifdef MVC2_PROF
    sweep_chunk = getenv("MVC2_SNDCHUNK") != NULL ? atoi(getenv("MVC2_SNDCHUNK")) : 16;
    sweep_dec8 = getenv("MVC2_ARMDEC8") != NULL && atoi(getenv("MVC2_ARMDEC8")) != 0;
    sweep_track = getenv("MVC2_ARMTRACK") == NULL || atoi(getenv("MVC2_ARMTRACK")) != 0;
    sweep_fiq = getenv("MVC2_FIQFAST") == NULL || atoi(getenv("MVC2_FIQFAST")) != 0;
#endif
    if (R_FAILED(ndspInit())) {
        host_log("audio: no NDSP (on a 3DS it needs sdmc:/3ds/dspfirm.cdc)");
        return;
    }
    pcm = linearAlloc(BUFS * BUF_SAMPLES * 4);
    direct_flush = !envIsHomebrew();
    if (pcm == NULL) {
        ndspExit();
        return;
    }
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnReset(CHANNEL);
    ndspChnSetInterp(CHANNEL, NDSP_INTERP_LINEAR);
    ndspChnSetRate(CHANNEL, SND_RATE);
    ndspChnSetFormat(CHANNEL, NDSP_FORMAT_STEREO_PCM16);
    ndspChnSetMix(CHANNEL, mix);
    memset(wave, 0, sizeof wave);
    for (i = 0; i < BUFS; i++) {
        wave[i].data_pcm16 = pcm + i * BUF_SAMPLES * 2;
        wave[i].status = NDSP_WBUF_DONE;
    }
    on = 1;
    LightLock_Init(&lock);
    LightEvent_Init(&go, RESET_ONESHOT);
    LightEvent_Init(&done, RESET_STICKY);
    LightEvent_Signal(&done);
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    if (core == 1) {
        APT_SetAppCpuTimeLimit(80);
        thread = threadCreate(sound_thread, NULL, 64 * 1024, prio - 1, 1, false);
    }
    threaded = thread != NULL;
#ifdef MVC2_PROF
    if (perf && threaded) {
        aica_perf = perf_mix;
    }
#endif
    host_log("audio: NDSP, the sound made on %s", threaded ? "core 1" : "the game's core");
}

void ctr_audio_frame(void) {
    if (!on) {
        return;
    }
#ifdef MVC2_PROF
    if (perf) {
        u32 v[4];
        int changed = perf_events(0);
        perf_read(v);
        if (perf_core0_ok && !changed) {
            perf_add(PF_CORE0, perf_core0_at, v);
        }
        memcpy(perf_core0_at, v, sizeof v);
        perf_core0_ok = 1;
    }
#endif
    if (pace) {
        u64 now = svcGetSystemTick();
        unsigned made = extra_made;
        int32_t more = 0;
        if (last_frame) {
            int32_t dt = (int32_t)((now - last_frame) * 1000000 / SYSCLOCK_ARM11);
            owed += dt - FRAME_US;
            game_us += ((float)dt - (float)(wait_all - wait_seen) * 1e6f / SYSCLOCK_ARM11 - game_us) * 0.1f;
        }
        wait_seen = wait_all;
        owed -= (int32_t)(made - extra_paid) * FRAME_US;
        extra_paid = made;
        owed = owed < -FRAME_US ? -FRAME_US : owed > 4 * FRAME_US ? 4 * FRAME_US : owed;
        if (level > TARGET + 1.5) {
            owed = owed < 0 ? owed : 0;
        } else if (owed > FRAME_US / 4) {
            int32_t room = cost > 0 ? (int32_t)(game_us * 0.85f / cost) - FRAME_US : FRAME_US;
            if (room1 && cost > 0 && turn_us > 0 && level >= TARGET - 1.5) {
                /* while the queue holds: what core 1 has left beside the lists, up to an eighth of a frame more
                   than the game's frames run over by, so that a catch-up is spread over frames; and at least that */
                int32_t over = game_us > FRAME_US ? (int32_t)game_us - FRAME_US : 0;
                int32_t left = (int32_t)((game_us * 0.85f - turn_us) / cost);
                left = left < over + FRAME_US / 8 ? left : over + FRAME_US / 8;
                left = left > over ? left : over;
                room = room < left ? room : left;
            }
            more = owed < FRAME_US ? owed : FRAME_US;
            more = more < room ? more : room > 0 ? room : 0;
            owed -= more;
            longer += more > 0;
        }
        last_frame = now;
        next_more = more;
        snd_set_frame_us(FRAME_US + (uint32_t)more);
    }
    if (threaded) {
        hold();
        wait_done(PZ_SND_SYNC);
        snd_prepare();
        report();
        snd_set_sync(wait_mid);    /* again after a switch, which puts the sound's state back */
        snd_set_defer(defer);
        snd_set_calls(call_begin, call_end);
        LightLock_Lock(&lock);
        held = 0;
        go_more = next_more;
        go_tick = (u32)svcGetSystemTick();
        busy_go = ctr_lists1_busy();
        making = 1;
        LightEvent_Clear(&done);
        LightLock_Unlock(&lock);
        LightEvent_Signal(&go);
    } else {
        make();
        report();
    }
}

void ctr_audio_idle(void) {
    if (threaded) {
        hold();
        wait_done(PZ_SND_SYNC);
    }
}

void ctr_audio_dump(void) {
    char name[64];
    FILE* f;
    uint32_t off, v;
    if (!on) {
        return;
    }
    ctr_audio_idle();
    host_log("sound: the chip at frame %u", host_frame());
    aica_dump(host_log);
    /* its registers' first 32 KB (not the monitor's, which a read clears), then its RAM */
    snprintf(name, sizeof name, "sound_%u.bin", host_frame());
    if ((f = fopen(name, "wb")) != NULL) {
        for (off = 0; off < 0x8000; off += 4) {
            v = off != 0x2810 ? aica_sh4_read32(0x00700000u + off) : 0;
            fwrite(&v, 4, 1, f);
        }
        fwrite(aica_sh4_ptr(0x00800000u), 1, aica_ram_size(), f);
        fclose(f);
        host_log("sound: and in %s", name);
    }
}

#ifdef MVC2_PROF
static FILE* dump_f;
static void dump_out(const void* p, uint32_t size) {
    fwrite(p, 1, size, dump_f);
}
#endif

void ctr_audio_exit(void) {
    if (!on) {
        return;
    }
#ifdef MVC2_PROF
    if (perf) {
        u64 x;
        wait_done(PZ_SND_SYNC);
        aica_perf = NULL;
        svcControlPerformanceCounter(&x, PERFCOUNTEROP_DISABLE, 0, 0);
        perf = 0;
    }
    if (getenv("MVC2_JITDUMP") && (dump_f = fopen("sdmc:/3ds/mvc2/jit.bin", "wb")) != NULL) {
        wait_done(PZ_SND_SYNC);
        arm7_jit_dump(dump_out);
        fclose(dump_f);
    }
#endif
    if (threaded) {
        hold();
        wait_done(PZ_SND_SYNC);
        quit = 1;
        LightEvent_Signal(&go);
        threadJoin(thread, U64_MAX);
        threadFree(thread);
        thread = NULL;
        threaded = 0;
        snd_set_sync(NULL);
        snd_set_defer(NULL);
        snd_set_calls(NULL, NULL);
    }
    snd_close();
    ndspChnReset(CHANNEL);
    ndspExit();
    linearFree(pcm);
    pcm = NULL;
    on = 0;
}

void ctr_audio_quiet(void) {
    if (on) {
        ndspSetMasterVol(0.0f);
    }
}
