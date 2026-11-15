/* ctr_prof.c  The zone timers on the 3DS: each frame's totals in a ring, summaries in prof.log, written by a thread */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <3ds.h>

#include "naomi_host.h"
#include "prof.h"
#include "ctr_front.h"
#include "early_load.h"

#define WINDOW 600              /* frames a summary covers */
#define RING 64                 /* frames held for prof.bin (MVC2_PROFBIN=1) */
#define HIST 500                /* frame times, in half milliseconds */
#define WORST 4
#define TEXT_SIZE 8192

static const char* const names[PZ_COUNT] = {
    [PZ_FRAME] = "frame", [PZ_GAME] = "game", [PZ_PRESENT] = "present", [PZ_LISTS] = "lists",
    [PZ_TEXTURES] = "textures", [PZ_DRAWS] = "draws", [PZ_GPU_WAIT] = "gpu wait", [PZ_VBLANK] = "vblank",
    [PZ_ROM_MISS] = "rom miss", [PZ_ROM_POLL] = "rom poll", [PZ_SND_PREPARE] = "snd prepare",
    [PZ_SND_SYNC] = "snd sync", [PZ_LIST_SLICES] = "  slices", [PZ_LIST_BUCKET] = "  bucket",
    [PZ_LIST_ADD] = "  add", [PZ_LIST_SORT] = "  trans sort", [PZ_SND_FRAME] = "snd frame", [PZ_SND_ARM] = "snd arm", [PZ_SND_FIQ] = "snd fiq",
    [PZ_SND_IO] = "snd io", [PZ_SND_CHANNELS] = "snd channels", [PZ_SND_MIX] = "snd mix",
    [PZ_SND_DEFER] = "snd defer", [PZ_SND_OUT] = "snd out", [PZ_SND_IDLE] = "snd idle",
    [PZ_SND_SYNC_MID] = "snd sync mid",
    [PZ_ROM_THREAD] = "rom thread", [PZ_TEX_ALLOC] = "  tex alloc", [PZ_TEX_FLUSH] = "  tex flush",
    [PZ_LIST_TRANS] = "  trans add", [PZ_NL_OBJ] = "  nl models", [PZ_NL_SPRITE] = "  nl sprites",
    [PZ_NL_STRIP] = "  nl strips", [PZ_NL_TEX] = "  nl tex loads",
    [PZ_HANDOFF] = "handoff", [PZ_PIPE_WAIT] = "pipe wait", [PZ_RENDER_IDLE] = "render idle", [PZ_VERT_FLUSH] = "  vert flush",
    [PZ_TRANS_PREP] = "    prep", [PZ_TRANS_CUTS] = "    cuts", [PZ_TRANS_GROUP] = "    group", [PZ_TRANS_ORDER] = "    order",
    [PZ_TEX_BIG] = "  tex big", [PZ_DRAW_MODELS] = "  model draws", [PZ_GPU_LIST] = "gpu list",
    [PZ_TL_UNIFS] = "    tl unifs", [PZ_TL_CORNER] = "    tl corner", [PZ_TL_DEPTH] = "    tl depth",
    [PZ_TL_APPLY] = "    tl apply", [PZ_TL_WRITE] = "    tl write", [PZ_TL_FRAG] = "    tl frag",
    [PZ_TL_C3D] = "    tl citro3d", [PZ_TL_BACK] = "    tl back",
    [PZ_TL_C_REJECT] = "      not tested", [PZ_TL_C_WAY1] = "      memo way 1", [PZ_TL_C_WAY2] = "      memo way 2",
    [PZ_TL_C_AT] = "      texels kept", [PZ_TL_C_BIG] = "      too big", [PZ_TL_C_PAST] = "      tris after",
    [PZ_PACK_BYTES] = "    pack bytes", [PZ_PACK_LINES] = "    pack lines", [PZ_PACK_TOUCH] = "    unpacked lines",
    [PZ_PACK_DISTINCT] = "    unpacked once",
    [PZ_LN_DRAWS] = "    md batch", [PZ_LN_DRAWS + 1] = "    md batch 1x", [PZ_LN_DRAWS + 2] = "    md draw",
    [PZ_LN_DRAWS + 3] = "    md draw 1x", [PZ_LN_DRAWS + 4] = "    md mesh", [PZ_LN_DRAWS + 5] = "    md mesh 1x",
    [PZ_LN_DRAWS + 6] = "    md run", [PZ_LN_DRAWS + 7] = "    md run 1x", [PZ_LN_DRAWS + 8] = "    md prep",
    [PZ_LN_DRAWS + 9] = "    md prep 1x", [PZ_LN_DRAWS + 10] = "    md state", [PZ_LN_DRAWS + 11] = "    md state 1x",
    [PZ_LN_DRAWS + 12] = "    md st", [PZ_LN_DRAWS + 13] = "    md st 1x", [PZ_LN_DRAWS + 14] = "    md tex",
    [PZ_LN_DRAWS + 15] = "    md tex 1x", [PZ_LN_DRAWS + 16] = "    md seen", [PZ_LN_DRAWS + 17] = "    md seen 1x",
    [PZ_LN_DRAWS + 18] = "    md frag", [PZ_LN_DRAWS + 19] = "    md frag 1x", [PZ_LN_DRAWS + 20] = "    md cmd",
    [PZ_LN_DRAWS + 21] = "    md cmd 1x", [PZ_LN_DRAWS + 22] = "    md all 1x", [PZ_LN_DRAWS + 23] = "    md groups",
    [PZ_LN_STATES] = "    sl seen", [PZ_LN_STATES + 1] = "    sl seen 1x", [PZ_LN_STATES + 2] = "    sl state",
    [PZ_LN_STATES + 3] = "    sl state 1x", [PZ_LN_STATES + 4] = "    sl st", [PZ_LN_STATES + 5] = "    sl st 1x",
    [PZ_LN_STATES + 6] = "    sl source", [PZ_LN_STATES + 7] = "    sl source 1x", [PZ_LN_STATES + 8] = "    sl find",
    [PZ_LN_STATES + 9] = "    sl find 1x", [PZ_LN_STATES + 10] = "    sl gtex", [PZ_LN_STATES + 11] = "    sl gtex 1x",
    [PZ_LN_STATES + 12] = "    sl lru", [PZ_LN_STATES + 13] = "    sl lru 1x", [PZ_LN_STATES + 14] = "    sl copy",
    [PZ_LN_STATES + 15] = "    sl copy 1x", [PZ_LN_STATES + 16] = "    sl all 1x", [PZ_LN_STATES + 17] = "    sl states",
    [PZ_NLG_PRE] = "  nl group pre", [PZ_NLG_FIND] = "  nl group find", [PZ_NLG_PUT] = "  nl group put",
    [PZ_NLG_HERE] = "  nl group here", [PZ_LIST_GPREP] = "  group prep",
    [PZ_LISTS1] = "lists core 1", [PZ_LISTS1_WAIT] = "  lists wait",
    [PZ_SLICE_SCAN] = "    slice scan", [PZ_SLICE_SORT] = "    slice sort", [PZ_SLICE_CUT] = "    slice cut",
    [PZ_SCAN_GROUPS] = "      groups", [PZ_ADD_TEX] = "    add tex", [PZ_TRANS_TEX] = "    trans tex",
    [PZ_LISTS1_STAGE] = "  core1 slices", [PZ_LISTS1_STAGE + 1] = "  core1 add", [PZ_LISTS1_STAGE + 2] = "  core1 sort",
    [PZ_LISTS1_STAGE + 3] = "  core1 trans", [PZ_LISTS1_STAGE + 4] = "  core1 prep",
    [PZ_DRAW_BEGIN] = "  draw begin", [PZ_CONSOLE_FLUSH] = "  console flush", [PZ_FRAME_END] = "  frame end",
    [PZ_CMD_FLUSH] = "  cmd flush",
    [PZ_GAME_APT] = "  apt loop", [PZ_GAME_INPUT] = "  input", [PZ_GAME_SETUP] = "  frame setup",
    [PZ_GAME_IRQ] = "  vblank irqs", [PZ_GAME_AUDIO] = "  audio frame", [PZ_GAME_MATRIX] = "  nl matrix",
    [PZ_GAME_MATH] = "  nl math", [PZ_GAME_BOARD] = "  nl board", [PZ_GAME_ROM] = "  rom reads",
    [PZ_STATES] = "  states", [PZ_SND_LISTS] = "  snd lists", [PZ_GX_RUN] = "    gx run",
    [PZ_GX_SUBMIT] = "      gx submit", [PZ_GX_LATE] = "  gx late",
    [PZ_GPU_FILL] = "  gpu fill", [PZ_GPU_FILL1] = "  gpu fill 1", [PZ_GPU_COPIES] = "  gpu copies",
    [PZ_GPU_3D] = "  gpu 3d", [PZ_GPU_OUT] = "gpu out", [PZ_GPU_AFTER] = "gpu after",
    [PZ_TEX_STAGE] = "  tex stage", [PZ_GPU_IDLE] = "gpu idle",
    [PZ_GD_OPEN] = "  gd open", [PZ_GD_READ] = "  gd read",
};

/* running totals, each written by its zone's thread only; a frame's share is the difference */
typedef struct {
    volatile uint32_t ticks, calls, items, pad;
} Zone;
static Zone zones[PZ_COUNT] __attribute__((aligned(32)));
uint32_t prof_read_ticks;

typedef struct {
    uint32_t frame;
    uint32_t ticks[PZ_COUNT], calls[PZ_COUNT], items[PZ_COUNT];
} Rec;

static Rec seen, worst[WORST];
static uint64_t win_ticks[PZ_COUNT], win_calls[PZ_COUNT], win_items[PZ_COUNT];
static uint32_t win_max[PZ_COUNT];
static uint16_t hist[HIST];
static uint32_t frames, last_tick, win_start;

static Rec ring[RING];
static volatile uint32_t ring_head, ring_tail;

static FILE *log_file, *bin_file;
static char text[TEXT_SIZE];
static volatile int text_len, quit;
static uint32_t missed;
static LightEvent wake;
static Thread writer;

uint32_t prof_now(void) {
    return (uint32_t)svcGetSystemTick();
}

void prof_add(int zone, uint32_t ticks) {
    zones[zone].ticks += ticks;
    zones[zone].calls++;
}

uint32_t prof_lists1(void) {
    return zones[PZ_LISTS1].ticks;
}

#ifdef MVC2_PROF_GAMEZ
uint32_t prof_ticks(int zone) {
    return zones[zone].ticks;
}
#endif

void prof_count(int zone, uint32_t n) {
    zones[zone].items += n;
}

static double ms(uint64_t ticks) {
    return (double)ticks * 1000.0 / SYSCLOCK_ARM11;
}

static double percentile(int pct) {
    uint32_t want = (frames - win_start) * pct / 100, sum = 0;
    int i;
    for (i = 0; i < HIST; i++) {
        sum += hist[i];
        if (sum > want)
            break;
    }
    return i * 0.5;
}

static int put(int n, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
static int put(int n, const char* fmt, ...) {
    va_list ap;
    if (n >= TEXT_SIZE)
        return n;
    va_start(ap, fmt);
    n += vsnprintf(text + n, TEXT_SIZE - n, fmt, ap);
    va_end(ap);
    return n < TEXT_SIZE ? n : TEXT_SIZE;
}

static const char* name(int i, char* buf) {
    if (names[i] != NULL)
        return names[i];
    sprintf(buf, "zone %d", i);
    return buf;
}

static void write_out(void);
#ifdef MVC2_PROF_FUNCS
static void write_places(FILE* f);
#endif

static void summary(void) {
    uint32_t n = frames - win_start;
    char buf[16];
    int i, w, len = 0;
    if (n == 0 || log_file == NULL || win_ticks[PZ_FRAME] == 0)
        return;
    if (text_len != 0) {
        missed++;
        return;
    }
    len = put(len, "frames %lu-%lu: %.1f a second; frame p50 %.1f p90 %.1f p99 %.1f max %.1f ms\n",
              (unsigned long)win_start + 1, (unsigned long)frames, n * 1000.0 / ms(win_ticks[PZ_FRAME]),
              percentile(50), percentile(90), percentile(99), ms(win_max[PZ_FRAME]));
    len = put(len, "  %-14s %9s %9s %9s %9s\n", "zone", "ms/frame", "max ms", "calls", "items");
    for (i = 0; i < PZ_COUNT; i++) {
        if (win_calls[i] == 0 && win_items[i] == 0)
            continue;
        len = put(len, "  %-14s %9.2f %9.2f %9.1f %9.1f\n", name(i, buf), ms(win_ticks[i]) / n, ms(win_max[i]),
                  (double)win_calls[i] / n, (double)win_items[i] / n);
    }
    for (w = 0; w < WORST && worst[w].frame != 0; w++) {
        len = put(len, "  worst %lu: %.1f ms:", (unsigned long)worst[w].frame, ms(worst[w].ticks[PZ_FRAME]));
        for (i = 1; i < PZ_COUNT; i++)
            if ((uint64_t)worst[w].ticks[i] * 50u >= worst[w].ticks[PZ_FRAME])     /* 2% of the frame or more */
                len = worst[w].items[i] != 0
                          ? put(len, " %s %.1f (%lu, %lu items)", name(i, buf), ms(worst[w].ticks[i]),
                                (unsigned long)worst[w].calls[i], (unsigned long)worst[w].items[i])
                          : put(len, " %s %.1f (%lu)", name(i, buf), ms(worst[w].ticks[i]), (unsigned long)worst[w].calls[i]);
        len = put(len, "\n");
    }
    if (missed != 0)
        len = put(len, "  (%lu summaries not written: the SD card was busy)\n", (unsigned long)missed);
    __sync_synchronize();
    text_len = len;
    if (writer != NULL)
        LightEvent_Signal(&wake);
    else
        write_out();
}

void prof_frame(void) {
    uint32_t now = prof_now(), next;
    Rec r;
    int i, w;
    if (last_tick != 0)
        prof_add(PZ_FRAME, now - last_tick);
    last_tick = now;
    r.frame = ++frames;
    for (i = 0; i < PZ_COUNT; i++) {
        uint32_t t = zones[i].ticks, c = zones[i].calls, it = zones[i].items;
        r.ticks[i] = t - seen.ticks[i];
        r.calls[i] = c - seen.calls[i];
        r.items[i] = it - seen.items[i];
        seen.ticks[i] = t;
        seen.calls[i] = c;
        seen.items[i] = it;
        win_ticks[i] += r.ticks[i];
        win_calls[i] += r.calls[i];
        win_items[i] += r.items[i];
        if (r.ticks[i] > win_max[i])
            win_max[i] = r.ticks[i];
    }
    if (r.calls[PZ_FRAME] != 0) {
        uint32_t b = (uint32_t)(ms(r.ticks[PZ_FRAME]) * 2.0);
        hist[b < HIST ? b : HIST - 1]++;
        for (w = 0; w < WORST; w++) {
            if (worst[w].frame == 0 || r.ticks[PZ_FRAME] > worst[w].ticks[PZ_FRAME]) {
                memmove(&worst[w + 1], &worst[w], (WORST - 1 - w) * sizeof(Rec));
                worst[w] = r;
                break;
            }
        }
    }
    if (bin_file != NULL) {
        next = (ring_head + 1) % RING;
        if (next != ring_tail) {        /* full: the frame is left out */
            ring[ring_head] = r;
            __sync_synchronize();
            ring_head = next;
        }
        if (frames % 16 == 0)
            LightEvent_Signal(&wake);
    }
    if (frames % WINDOW == 0) {
        summary();
        memset(win_ticks, 0, sizeof win_ticks);
        memset(win_calls, 0, sizeof win_calls);
        memset(win_items, 0, sizeof win_items);
        memset(win_max, 0, sizeof win_max);
        memset(hist, 0, sizeof hist);
        memset(worst, 0, sizeof worst);
        win_start = frames;
    }
}

static void write_out(void) {
    int len = text_len;
    if (len != 0) {
        fwrite(text, 1, len, log_file);
#ifdef MVC2_PROF_FUNCS
        write_places(log_file);
#endif
        fflush(log_file);
        __sync_synchronize();
        text_len = 0;
    }
    if (bin_file != NULL) {
        uint32_t head = ring_head, tail = ring_tail;
        if (head < tail) {
            fwrite(&ring[tail], sizeof(Rec), RING - tail, bin_file);
            tail = 0;
        }
        if (tail < head)
            fwrite(&ring[tail], sizeof(Rec), head - tail, bin_file);
        __sync_synchronize();
        ring_tail = head;
    }
}

/* the SD card's writes on a thread of their own: it only waits for them, so the game runs meanwhile */
static void writer_main(void* arg) {
    (void)arg;
    while (!quit) {
        LightEvent_Wait(&wake);
        write_out();
    }
    write_out();
}

/* the calibration: a loop of known length on each core, the tick's own cost, and the memory's latency */
static uint32_t spin(uint32_t n) {
    uint32_t t0 = prof_now();
    __asm__ volatile("1: subs %0, %0, #1\n\tbne 1b" : "+r"(n) : : "cc");
    return prof_now() - t0;
}

static volatile uint32_t core1_ticks;
static void spin_core1(void* arg) {
    core1_ticks = spin((uint32_t)arg);
}

static double chase(uint32_t size, uint32_t steps) {
    uint32_t lines = size / 32, i, seed = 12345, t0;
    uint32_t* buf = host_try_malloc(size);
    void** p;
    if (buf == NULL)
        return 0.0;
    for (i = 0; i < lines; i++)
        buf[i * 8] = i;
    for (i = lines - 1; i > 0; i--) {   /* one cycle through every line, in a random order */
        uint32_t j, t;
        seed = seed * 1664525u + 1013904223u;
        j = (seed >> 8) % i;
        t = buf[i * 8];
        buf[i * 8] = buf[j * 8];
        buf[j * 8] = t;
    }
    for (i = 0; i < lines; i++)
        buf[i * 8] = (uint32_t)&buf[buf[i * 8] * 8];
    p = (void**)buf;
    for (i = 0; i < lines; i++)
        p = *p;
    t0 = prof_now();
    for (i = 0; i < steps; i++)
        p = *p;
    t0 = prof_now() - t0;
    __asm__ volatile("" : : "r"(p));
    free(buf);
    return ms(t0) * 1e6 / steps;
}

/* ns an 88-byte item (a list's triangle) stored into lines not in the cache (with evict, which is read first to
   push them out): each item's lines read `ahead` items before it (-1: never, 0: just before), `work` passes of a
   loop between items, their time taken off */
static double stores(uint8_t* buf, uint32_t size, const uint8_t* evict, int ahead, uint32_t work, uint32_t work_ticks) {
    enum { ITEM = 88 };
    uint32_t n = size / ITEM - 16, i, t0, sum = 0, loop;
    uint32_t item[ITEM / 4];
    for (i = 0; i < ITEM / 4; i++)
        item[i] = i;
    for (i = 0; evict != NULL && i < 64u << 10; i += 32)   /* four times the data cache */
        sum += evict[i];
    __asm__ volatile("" : : "r"(sum));
    t0 = prof_now();
    for (i = 0; i < n; i++) {
        if (ahead >= 0)
            early_lines(buf + (i + (uint32_t)ahead) * ITEM, ITEM);
        memcpy(buf + i * ITEM, item, ITEM);
        __asm__ volatile("" : : "r"(buf) : "memory");
        loop = work;
        if (loop != 0)
            __asm__ volatile("1: subs %0, %0, #1\n\tbne 1b" : "+r"(loop) : : "cc");
    }
    t0 = prof_now() - t0 - work_ticks * n;
    return ms(t0) * 1e6 / n;
}

static void store_calibration(void) {
    static const int ahead[] = { -1, 0, 1, 2, 4, 8 };
    const uint32_t size = 96u << 10, work = 256;
    uint8_t* buf = host_try_malloc(size * 12);
    uint8_t* evict = host_try_malloc(64u << 10);
    uint32_t work_ticks;
    int w, k, part = 0;
    if (buf == NULL || evict == NULL) {
        free(buf);
        free(evict);
        return;
    }
    memset(evict, 1, 64u << 10);
    work_ticks = spin(work * 1000) / 1000;
    for (w = 0; w < 2; w++) {
        fprintf(log_file, "calibration: 88-byte stores to lines not in the cache, ns an item (%s):", w ? "work between" : "back to back");
        for (k = 0; k < 6; k++) {
            double t = stores(buf + part++ * size, size, evict, ahead[k], w ? work : 0, w ? work_ticks : 0);
            if (ahead[k] < 0)
                fprintf(log_file, " untouched %.1f", t);
            else
                fprintf(log_file, ", read %d ahead %.1f", ahead[k], t);
        }
        fprintf(log_file, "\n");
    }
    stores(buf, 8u << 10, NULL, -1, 0, 0);
    fprintf(log_file, "calibration: 88-byte stores to lines in the cache, ns an item: %.1f (the work between: %.0f ns)\n",
            stores(buf, 8u << 10, NULL, -1, 0, 0), ms(work_ticks) * 1e6);
    free(buf);
    free(evict);
}

/* ns a KB to store words into buf, to read it back, and to flush it from the data cache */
static void vram_pass(uint32_t* buf, uint32_t size, double* store, double* read, double* flush) {
    uint32_t n = size / 4, i, t0, sum = 0;
    for (i = 0; i < n; i++)   /* once untimed, so the timed passes don't take page faults */
        buf[i] = i;
    GSPGPU_FlushDataCache(buf, size);
    t0 = prof_now();
    for (i = 0; i < n; i++)
        buf[i] = i ^ 0x5A5A5A5Au;
    __asm__ volatile("" : : "r"(buf) : "memory");
    *store = ms(prof_now() - t0) * 1e6 / (size >> 10);
    t0 = prof_now();
    GSPGPU_FlushDataCache(buf, size);
    *flush = ms(prof_now() - t0) * 1e6 / (size >> 10);
    t0 = prof_now();
    for (i = 0; i < n; i++)
        sum += buf[i];
    __asm__ volatile("" : : "r"(sum));
    *read = ms(prof_now() - t0) * 1e6 / (size >> 10);
}

/* texture memory written by the CPU: stores into vramAlloc'd memory against linear memory, and their read-back
   (MVC2_VRAM_PROBE) */
static void vram_calibration(void) {
    const uint32_t size = 256u << 10;
    uint32_t* v = vramAlloc(size);
    uint32_t* l = linearAlloc(size);
    double vs, vr, vf, ls, lr, lf;
    if (v == NULL || l == NULL) {
        fprintf(log_file, "calibration: no %s for the VRAM store test\n", v == NULL ? "VRAM" : "linear memory");
    } else {
        vram_pass(v, size, &vs, &vr, &vf);
        vram_pass(l, size, &ls, &lr, &lf);
        fprintf(log_file, "calibration: 256 KB of word stores, ns a KB: VRAM stores %.0f, flush %.0f, reads %.0f; "
                "linear stores %.0f, flush %.0f, reads %.0f\n", vs, vf, vr, ls, lf, lr);
    }
    if (v != NULL)
        vramFree(v);
    if (l != NULL)
        linearFree(l);
}

static void calibrate(void) {
    const uint32_t loops = 1u << 22;
    uint32_t t0, t1, i;
    Thread th;
    t0 = spin(loops);
    th = threadCreate(spin_core1, (void*)loops, 4096, 0x18, 1, false);
    if (th != NULL) {
        threadJoin(th, U64_MAX);
        threadFree(th);
    }
    fprintf(log_file, "calibration: a loop of %lu on core 0 %.2f ms (%.2f ticks a pass)", (unsigned long)loops,
            ms(t0), (double)t0 / loops);
    if (th != NULL)
        fprintf(log_file, ", on core 1 %.2f ms: core 1 gives %.0f%%\n", ms(core1_ticks), 100.0 * t0 / core1_ticks);
    else
        fprintf(log_file, ", no thread on core 1\n");
    t1 = prof_now();
    for (i = 0; i < 10000; i++)
        (void)prof_now();
    t1 = prof_now() - t1;
    prof_read_ticks = t1 / 10000;
    fprintf(log_file, "calibration: a tick read %.2f us\n", ms(t1) * 1000.0 / 10000);
    fprintf(log_file, "calibration: memory, ns a read: 16 KB %.1f, 64 KB %.1f, 256 KB %.1f, 2 MB %.1f\n",
            chase(16 << 10, 1 << 20), chase(64 << 10, 1 << 20), chase(256 << 10, 1 << 20), chase(2 << 20, 1 << 20));
    store_calibration();
    if (getenv("MVC2_VRAM_PROBE") != NULL)
        vram_calibration();
    fflush(log_file);
}

#ifdef MVC2_PROF_FUNCS
/* with -finstrument-functions: the game thread's place, a function entered or the caller returned to, sampled
   every millisecond; the addresses are named on the PC from the ELF */
#define SLOTS 8192u
static volatile uint32_t place;
static void* game_tls;
static uint32_t slot_pc[SLOTS], slot_n[SLOTS], samples, dropped, used;
static Thread sampler;

__attribute__((no_instrument_function)) void __cyg_profile_func_enter(void* fn, void* site) {
    (void)site;
    if (getThreadLocalStorage() == game_tls)
        place = (uint32_t)fn;
}

__attribute__((no_instrument_function)) void __cyg_profile_func_exit(void* fn, void* site) {
    (void)fn;
    if (getThreadLocalStorage() == game_tls)
        place = (uint32_t)site;
}

void prof_place(uint32_t p) {
    place = p;
}

static void sampler_main(void* arg) {
    (void)arg;
    while (!quit) {
        uint32_t pc = place, h = ((pc >> 1) * 2654435761u) >> 19;
        svcSleepThread(1000000);
        if (pc == 0)
            continue;
        while (slot_pc[h] != pc && slot_pc[h] != 0)
            h = (h + 1) & (SLOTS - 1);
        if (slot_pc[h] == 0) {
            if (used >= SLOTS * 3 / 4) {
                dropped++;
                continue;
            }
            slot_pc[h] = pc;
            used++;
        }
        slot_n[h]++;
        samples++;
    }
}

/* the busiest places so far, on the writer's thread */
static void write_places(FILE* f) {
    static uint8_t taken[SLOTS];
    uint32_t total = samples, k, i;
    memset(taken, 0, sizeof taken);
    fprintf(f, "places: %lu samples, %lu places, %lu not kept\n", (unsigned long)total, (unsigned long)used,
            (unsigned long)dropped);
    for (k = 0; k < 60 && total != 0; k++) {
        uint32_t best = SLOTS;
        for (i = 0; i < SLOTS; i++)
            if (!taken[i] && slot_n[i] != 0 && (best == SLOTS || slot_n[i] > slot_n[best]))
                best = i;
        if (best == SLOTS)
            break;
        taken[best] = 1;
        static const char* const outside[] = { "", "(present)", "(sound wait)", "(ROM read)" };
        fprintf(f, "  %08lx %6lu %5.1f%% %s\n", (unsigned long)slot_pc[best], (unsigned long)slot_n[best],
                100.0 * slot_n[best] / total, slot_pc[best] < 4 ? outside[slot_pc[best]] : "");
    }
}
#endif

void prof_start(void) {
    s32 prio = 0x30;
    const char* bin = getenv("MVC2_PROFBIN");
    log_file = fopen(CTR_HOME "/" CTR_PROF ".log", "w");
    if (log_file == NULL)
        return;
    calibrate();
    if (bin != NULL && bin[0] == '1') {
        bin_file = fopen(CTR_HOME "/" CTR_PROF ".bin", "wb");
        if (bin_file != NULL) {
            uint32_t head[3] = {0x3150564Du, PZ_COUNT, SYSCLOCK_ARM11};    /* "MVP1" */
            fwrite(head, sizeof head, 1, bin_file);
        }
    }
    LightEvent_Init(&wake, RESET_ONESHOT);
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    writer = threadCreate(writer_main, NULL, 16 << 10, prio > 0x18 ? prio - 1 : prio, 0, false);
    if (writer == NULL)
        fprintf(log_file, "no writer thread: summaries are written by the game\n");
#ifdef MVC2_PROF_FUNCS
    game_tls = getThreadLocalStorage();
    sampler = threadCreate(sampler_main, NULL, 4096, prio > 0x19 ? prio - 2 : prio, 0, false);
#endif
}

void prof_stop(void) {
    summary();
    if (writer != NULL) {
        quit = 1;
        LightEvent_Signal(&wake);
        threadJoin(writer, U64_MAX);
        threadFree(writer);
        writer = NULL;
    } else if (log_file != NULL) {
        write_out();
    }
#ifdef MVC2_PROF_FUNCS
    if (sampler != NULL) {
        threadJoin(sampler, U64_MAX);
        threadFree(sampler);
        sampler = NULL;
    }
#endif
    if (bin_file != NULL)
        fclose(bin_file);
    if (log_file != NULL)
        fclose(log_file);
    bin_file = log_file = NULL;
}

/* C3D_FrameEnd's hand-over to GSP, through the linker's --wrap (CMakeLists): the queue's run, and each command
   given to GSP, there or later from the GSP event thread */
static int gx_running;

void __real_gxCmdQueueRun(gxCmdQueue_s* queue);
void __wrap_gxCmdQueueRun(gxCmdQueue_s* queue) {
    uint32_t t = prof_now();
    gx_running = 1;
    __real_gxCmdQueueRun(queue);
    gx_running = 0;
    prof_add(PZ_GX_RUN, prof_now() - t);
}

Result __real_gspSubmitGxCommand(const u32 gxCommand[0x8]);
Result __wrap_gspSubmitGxCommand(const u32 gxCommand[0x8]) {
    int zone = gx_running ? PZ_GX_SUBMIT : PZ_GX_LATE;
    uint32_t t = prof_now();
    Result r = __real_gspSubmitGxCommand(gxCommand);
    prof_add(zone, prof_now() - t);
    prof_count(zone, (gxCommand[0] & 0xFF) == 1);
    return r;
}
