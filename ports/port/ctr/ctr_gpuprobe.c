/* ctr_gpuprobe.c  Profile builds: the GPU's frame timed job by job, and work taken out of its list to see
   what it costs.

   MVC2_GPU_SPLIT=1: each GX job's end stamped by its interrupt (the clear's two buffers, the pillars' copies, the
   command list, the output's transfer), the stamps of a frame read at the next one's end, when the GPU is idle.
   The stamps come after libctru's queue has started the next job.

   MVC2_GPU_PROBE=[F:]a,b,...: from frame F (1 if not given), 600 frames each, the frame's commands changed before
   they are sent: off; scissor (no fragments: an 8x8 box in a corner the game doesn't draw to); nolight (the
   lighting that looks the palettes up, on for every draw, unbound at the frame's start, since the list writes
   it only when it is bound); tex8 (each texture read as its first 8x8 texels, so the texture cache holds them);
   nocread (the colour buffer not read: blending takes no destination); nodepth (no depth test, the depth buffer
   neither read nor written). The pictures are wrong in all but off; the split is on with it */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <3ds.h>
#include "prof.h"

void host_log(const char* fmt, ...);

enum { PROBE_OFF, PROBE_SCISSOR, PROBE_NOLIGHT, PROBE_TEX8, PROBE_NOCREAD, PROBE_NODEPTH, PROBES };
enum { EVENTS = 16, WINDOW = 600, MAX_STEPS = 32 };

static const char* const probe_names[PROBES] = { "off", "scissor", "nolight", "tex8", "nocread", "nodepth" };

static int split = -1;
static volatile u32 ev_tick[EVENTS];
static volatile u8 ev_id[EVENTS];
static volatile u32 ev_n;
static u32 sent, waited, begun;

static u8 steps[MAX_STEPS];
static int nsteps, probe;
static u32 probe_from = 1, frames;

static void stamp(void* arg) {
    u32 n = ev_n;
    if (n < EVENTS) {
        ev_tick[n] = (u32)svcGetSystemTick();
        ev_id[n] = (u8)(uintptr_t)arg;
        ev_n = n + 1;
    }
}

void gpu_split_p3d(void) {
    if (split > 0) {
        stamp((void*)(uintptr_t)GSPGPU_EVENT_P3D);
    }
}

static void probe_parse(const char* s) {
    const char* colon = strchr(s, ':');
    if (colon != NULL) {
        probe_from = (u32)strtoul(s, NULL, 10);
        s = colon + 1;
    }
    while (*s != '\0' && nsteps < MAX_STEPS) {
        size_t len = strcspn(s, ",");
        int i;
        for (i = 0; i < PROBES; i++) {
            if (strlen(probe_names[i]) == len && strncmp(s, probe_names[i], len) == 0) {
                steps[nsteps++] = (u8)i;
                break;
            }
        }
        if (i == PROBES) {
            host_log("gpu probe: \"%.*s\" not known", (int)len, s);
        }
        s += len + (s[len] == ',');
    }
}

void gpu_split_init(void) {
    const char* e = getenv("MVC2_GPU_SPLIT");
    const char* p = getenv("MVC2_GPU_PROBE");
    split = e != NULL && strcmp(e, "0") != 0;
    if (p != NULL) {
        probe_parse(p);
        if (nsteps > 0) {
            split = 1;
            host_log("gpu probe: from frame %lu, %d steps of %d frames (MVC2_GPU_PROBE)", (unsigned long)probe_from,
                     nsteps, WINDOW);
        }
    }
    if (split) {
        gspSetEventCallback(GSPGPU_EVENT_PSC0, stamp, (void*)(uintptr_t)GSPGPU_EVENT_PSC0, false);
        gspSetEventCallback(GSPGPU_EVENT_PSC1, stamp, (void*)(uintptr_t)GSPGPU_EVENT_PSC1, false);
        gspSetEventCallback(GSPGPU_EVENT_PPF, stamp, (void*)(uintptr_t)GSPGPU_EVENT_PPF, false);
        host_log("gpu split: the clear, the copies, the list and the output timed by their interrupts "
                 "(MVC2_GPU_SPLIT)");
    }
}

void gpu_split_begun(uint32_t wait_from) {
    if (split > 0) {
        waited = wait_from;
        begun = (u32)svcGetSystemTick();
    }
}

/* the last frame's jobs, in the order they were queued: the clear (PSC0, PSC1 at once), the pillars' copies (PPF),
   the list (P3D, one a part with MVC2_LIST_SPLIT: the list ends at the last), the output (PPF). A job that ends
   before its frame was sent is taken to end as it is sent; the time from the output's end to C3D_FrameBegin's
   return counts only when it ended in the wait */
void gpu_split_take(void) {
    u32 i, n = ev_n, t, last, fill_at, list_from = 0, list_at = 0, copies = 0;
    int listed = 0, out = 0;
    if (split <= 0) {
        return;
    }
    if (sent != 0) {
        last = fill_at = t = sent;
        for (i = 0; i < n; i++) {
            if ((int32_t)(ev_tick[i] - t) > 0) {
                t = ev_tick[i];
            }
            switch (ev_id[i]) {
            case GSPGPU_EVENT_PSC0:
                PROF_ADD(PZ_GPU_FILL, t - sent);
                last = fill_at = t;
                break;
            case GSPGPU_EVENT_PSC1: PROF_ADD(PZ_GPU_FILL1, t - sent); break;
            case GSPGPU_EVENT_PPF:
                if (!listed) {
                    copies++;
                    last = t;
                } else if (!out) {
                    PROF_ADD(PZ_GPU_3D, list_at - list_from);
                    PROF_ADD(PZ_GPU_OUT, t - list_at);
                    if (begun != 0 && (int32_t)(t - waited) > 0 && (int32_t)(begun - t) > 0) {
                        PROF_ADD(PZ_GPU_AFTER, begun - t);
                    }
                    out = 1;
                }
                break;
            case GSPGPU_EVENT_P3D:
                if (!listed) {
                    if (copies > 0) {
                        PROF_ADD(PZ_GPU_COPIES, last - fill_at);
                        PROF_COUNT(PZ_GPU_COPIES, copies);
                    }
                    list_from = last;
                    listed = 1;
                }
                list_at = t;
                break;
            default: break;
            }
        }
        if (listed && !out) {
            PROF_ADD(PZ_GPU_3D, list_at - list_from);
        }
    }
    ev_n = 0;
    sent = 0;
    begun = 0;
}

/* the interrupts stamped since the last take, by name */
void gpu_split_events(char* out, unsigned size) {
    static const char* const names[] = { "PSC0", "PSC1", "VBlank0", "VBlank1", "PPF", "P3D", "DMA" };
    u32 i, n = ev_n;
    unsigned at = 0;
    out[0] = '\0';
    if (split <= 0) {
        snprintf(out, size, " (not stamped without MVC2_GPU_SPLIT)");
        return;
    }
    for (i = 0; i < n && i < EVENTS && at + 9 < size; i++) {
        at += (unsigned)snprintf(out + at, size - at, " %s", ev_id[i] < 7 ? names[ev_id[i]] : "?");
    }
}

void gpu_split_sent(void) {
    if (split > 0) {
        sent = (u32)svcGetSystemTick();
    }
}

/* a frame's commands changed for the probe under way; w as reg_check_frame reads it */
static u32 patch(u32 reg, u32 v) {
    switch (probe) {
    case PROBE_SCISSOR:
        if (reg == GPUREG_SCISSORTEST_MODE) {
            return GPU_SCISSOR_NORMAL;
        }
        if (reg == GPUREG_SCISSORTEST_POS) {
            return 0;
        }
        if (reg == GPUREG_SCISSORTEST_DIM) {
            return 7u << 16 | 7u;
        }
        break;
    case PROBE_TEX8:
        if (reg == GPUREG_TEXUNIT0_DIM || reg == GPUREG_TEXUNIT1_DIM || reg == GPUREG_TEXUNIT2_DIM) {
            return 8u << 16 | 8u;
        }
        if (reg == GPUREG_TEXUNIT0_LOD || reg == GPUREG_TEXUNIT1_LOD || reg == GPUREG_TEXUNIT2_LOD) {
            return v & ~0x0F0F0000u;
        }
        break;
    case PROBE_NOCREAD:
        if (reg == GPUREG_COLORBUFFER_READ) {
            return 0;
        }
        break;
    case PROBE_NODEPTH:
        if (reg == GPUREG_DEPTHBUFFER_READ || reg == GPUREG_DEPTHBUFFER_WRITE) {
            return 0;
        }
        if (reg == GPUREG_DEPTH_COLOR_MASK) {
            return v & ~0x1001u;
        }
        break;
    default: break;
    }
    return v;
}

int gpu_probe_nolight(void) {
    return probe == PROBE_NOLIGHT;
}

int gpu_probe_on(void) {
    return nsteps > 0;
}

void gpu_probe_frame(uint32_t* w, uint32_t n) {
    u32 k = 0;
    int now = PROBE_OFF;
    frames++;
    if (nsteps == 0) {
        return;
    }
    if (frames >= probe_from) {
        now = steps[(frames - probe_from) / WINDOW % (u32)nsteps];
    }
    if (now != probe) {
        probe = now;
        host_log("gpu probe: %s from frame %lu", probe_names[probe], (unsigned long)frames);
    }
    if (probe == PROBE_OFF) {
        return;
    }
    while (k + 2 <= n) {
        u32 h = w[k + 1], reg = h & 0xFFFF, extra = h >> 20 & 0xFF, i;
        w[k] = patch(reg, w[k]);
        for (i = 0; i < extra && k + 2 + i < n; i++) {
            if (h >> 31) {
                reg++;
            }
            w[k + 2 + i] = patch(reg, w[k + 2 + i]);
        }
        k += 2 + extra + (extra & 1);
    }
}
