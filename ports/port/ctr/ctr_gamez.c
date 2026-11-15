/* ctr_gamez.c  MVC2_PROF_GAMEZ: the game's frame by its routines, each with its time in all, its time in the
   library's zones and its own (neither the library nor the routines it calls), logged every 600 frames. With
   MVC2_PERFZONES=1, its own cycles and the events PERFZONES counts too, the library's calls in them (sprite_disp's
   go to its caller's, as the counters' reads would cost more than it) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <3ds.h>

#include "naomi_host.h"
#include "prof.h"
#include "ctr_gamez.h"

#define DEPTH 16
#define WINDOW 600
#define EVENTS 10

static const char* const fixed_names[GZ_MOVE_LIST] = {
    "System_Main", "SysTrans", "SysTrans2", "load_main", "sound_vsync", "key_repeat", "vms_check", "col_send",
    "game_main", "all_work_move", "pl_work_move", "scr_move", "stage_frame", "pl_hit_work_set", "bg_move", "fog_set",
    "hit_ck0", "vs_sys_move", "flash_move", "disp_prio_clr", "pl_work_disp", "game_disp", "sprite_disp",
    "sprite_tex_set", "lz_unpack",
};

/* the library's zones on the game's thread; not the matrices' and maths', which time one call in 64 and count it 64
   times, their tick reads with it, so a routine's own time has those calls in it */
static const int lib_zones[] = { PZ_NL_OBJ, PZ_NL_SPRITE, PZ_NL_STRIP, PZ_NL_TEX, PZ_GAME_BOARD, PZ_GAME_ROM,
                                 PZ_GAME_IRQ };

static const PerfCounterEvent events[EVENTS] = {
    PERFCOUNTEREVT_CORE_DATA_CACHE_READ_MISS, PERFCOUNTEREVT_CORE_DATA_CACHE_LINE_EVICTION,
    PERFCOUNTEREVT_CORE_DATA_CACHE_WRITE_MISS, PERFCOUNTEREVT_CORE_STALL_BY_DATA_HAZARD,
    PERFCOUNTEREVT_CORE_MAIN_TLB_MISS, PERFCOUNTEREVT_CORE_DATA_MICRO_TLB_MISS,
    PERFCOUNTEREVT_CORE_INST_EXECUTED, PERFCOUNTEREVT_CORE_INST_CACHE_MISS,
    PERFCOUNTEREVT_CORE_STALL_BY_LSU_FULL, PERFCOUNTEREVT_CORE_STALL_BY_LACK_OF_INST,
};
static const char* const event_names[EVENTS] = {
    "D reads missed", "lines evicted", "D writes missed", "data stalls", "main TLB misses", "micro TLB misses",
    "instructions", "I misses", "LSU full stalls", "instruction stalls",
};

typedef struct {
    uint64_t all, cycles, ev[EVENTS];
    int64_t lib, own;               /* signed: the sampled library zones count one call in 64 at 64 times its time */
    uint32_t calls;
} Zone;

typedef struct {
    int zone, pmu_on;
    uint32_t t0, lib0, nested;
    int32_t inner;
    uint32_t pmu[3], inner_pmu[3];
} Call;

static Zone zones[GZ_COUNT];
static Call stack[DEPTH];
static int depth, perf = -1, ev_at[2] = { -1, -1 };
static uint32_t frames, ev_frames[EVENTS], game0, lib0_window;
static uint64_t top_all;

static uint32_t lib_now(void) {
    uint32_t t = 0;
    size_t i;
    for (i = 0; i < sizeof lib_zones / sizeof lib_zones[0]; i++)
        t += prof_ticks(lib_zones[i]);
    return t;
}

static void pmu_read(uint32_t* v) {
    u64 x = 0;
    svcControlPerformanceCounter(&x, PERFCOUNTEROP_GET_VALUE, PERFCOUNTERREG_CORE_COUNT_REG_0, 0);
    v[0] = (uint32_t)x;
    svcControlPerformanceCounter(&x, PERFCOUNTEROP_GET_VALUE, PERFCOUNTERREG_CORE_COUNT_REG_1, 0);
    v[1] = (uint32_t)x;
    svcControlPerformanceCounter(&x, PERFCOUNTEROP_GET_VALUE, PERFCOUNTERREG_CORE_CYCLE_COUNTER, 0);
    v[2] = (uint32_t)x;
}

/* which of the events the two counters have now (PERFZONES changes them every 60 frames) */
static int event_index(PerfCounterRegister reg) {
    u64 x = 0;
    int i;
    svcControlPerformanceCounter(&x, PERFCOUNTEROP_GET_EVENT, reg, 0);
    for (i = 0; i < EVENTS; i++)
        if ((u64)events[i] == x)
            return i;
    return -1;
}

static const char* zone_name(int z, char* buf) {
    if (z >= GZ_DISP_LIST) {
        sprintf(buf, "work_list_disp %d", z - GZ_DISP_LIST);
    } else if (z >= GZ_MOVE_LIST) {
        sprintf(buf, "work_list_move %d", z - GZ_MOVE_LIST);
    } else {
        return fixed_names[z] != NULL ? fixed_names[z] : "?";
    }
    return buf;
}

static double ms(int64_t ticks) {
    return (double)ticks * 1000.0 / SYSCLOCK_ARM11 / WINDOW;
}

static void report(void) {
    uint32_t game = prof_ticks(PZ_GAME) - game0, lib = lib_now() - lib0_window;
    char buf[32], line[480];
    int z, e, n;
    int64_t own = 0;
    for (z = 0; z < GZ_COUNT; z++)
        own += zones[z].own;
    host_log("game zones, %u frames: game %.2f ms a frame, the library's zones %.2f, the zones below %.2f in all and %.2f "
             "their own (a routine's own time takes in its module's static routines it calls, and the matrices' and "
             "maths' calls)", WINDOW, ms(game), ms(lib), ms(top_all), ms(own));
    for (z = 0; z < GZ_COUNT; z++) {
        Zone* q = &zones[z];
        if (q->calls == 0)
            continue;
        n = snprintf(line, sizeof line, "game zone %-18s %7.1f calls %6.2f ms in all, %6.2f in the library, %6.2f its own",
                     zone_name(z, buf), (double)q->calls / WINDOW, ms(q->all), ms(q->lib), ms(q->own));
        if (perf > 0) {
            n += snprintf(line + n, sizeof line - n, "; its own: %.0f cycles", (double)q->cycles / WINDOW);
            for (e = 0; e < EVENTS && n < (int)sizeof line; e++)
                if (ev_frames[e] != 0)
                    n += snprintf(line + n, sizeof line - n, ", %s %.0f", event_names[e], (double)q->ev[e] / ev_frames[e]);
        }
        host_log("%s", line);
    }
    memset(zones, 0, sizeof zones);
    memset(ev_frames, 0, sizeof ev_frames);
    top_all = 0;
}

void gamez_in(int zone) {
    Call* c;
    if (perf < 0) {
        const char* e = getenv("MVC2_PERFZONES");
        perf = e != NULL && atoi(e) != 0;
        game0 = prof_ticks(PZ_GAME);
        lib0_window = lib_now();
    }
    if (depth >= DEPTH) {
        depth++;
        return;
    }
    if (depth == 0 && zone == GZ_SYSTEM_MAIN && perf > 0) {
        ev_at[0] = event_index(PERFCOUNTERREG_CORE_COUNT_REG_0);
        ev_at[1] = event_index(PERFCOUNTERREG_CORE_COUNT_REG_1);
        if (ev_at[0] >= 0)
            ev_frames[ev_at[0]]++;
        if (ev_at[1] >= 0)
            ev_frames[ev_at[1]]++;
    }
    c = &stack[depth++];
    c->zone = zone;
    c->inner = c->nested = 0;
    memset(c->inner_pmu, 0, sizeof c->inner_pmu);
    c->lib0 = lib_now();
    c->pmu_on = perf > 0 && zone != GZ_SPRITE_DISP;
    if (c->pmu_on)
        pmu_read(c->pmu);
    c->t0 = prof_now();
}

void gamez_out(void) {
    uint32_t t = prof_now(), all, over, v[3] = { 0, 0, 0 };
    int32_t lib, mine;
    Call* c;
    Zone* q;
    int i;
    if (--depth >= DEPTH)
        return;
    c = &stack[depth];
    if (c->pmu_on)
        pmu_read(v);
    q = &zones[c->zone];
    over = (1 + 2 * c->nested) * prof_read_ticks;     /* the tick reads inside, this one's and the calls' below */
    all = t - c->t0;
    all = all > over ? all - over : 0;
    lib = (int32_t)(lib_now() - c->lib0);
    mine = (int32_t)all - lib;                        /* the game's time, the routines below included */
    q->all += all;
    q->lib += lib;
    q->own += mine - c->inner;
    q->calls++;
    if (c->pmu_on) {
        for (i = 0; i < 3; i++)
            v[i] -= c->pmu[i];
        if (ev_at[0] >= 0)
            q->ev[ev_at[0]] += v[0] > c->inner_pmu[0] ? v[0] - c->inner_pmu[0] : 0;
        if (ev_at[1] >= 0)
            q->ev[ev_at[1]] += v[1] > c->inner_pmu[1] ? v[1] - c->inner_pmu[1] : 0;
        q->cycles += v[2] > c->inner_pmu[2] ? v[2] - c->inner_pmu[2] : 0;
    }
    if (depth > 0) {
        Call* up = &stack[depth - 1];
        up->inner += mine;
        up->nested += 1 + c->nested;
        if (c->pmu_on)
            for (i = 0; i < 3; i++)
                up->inner_pmu[i] += v[i];
    } else {
        top_all += all;
        if (c->zone == GZ_SYSTEM_MAIN && ++frames % WINDOW == 0) {
            report();
            game0 = prof_ticks(PZ_GAME);
            lib0_window = lib_now();
        }
    }
}
