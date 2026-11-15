/* ctr_regcheck.c  Profile builds, MVC2_REG_CHECK=1: each frame's GPU commands read into a copy of the GPU's
   registers, its uniforms, shader code and tables, and the whole of it hashed at every draw, so that two ways of
   writing the commands can be shown to leave the GPU the same at each draw. =2 also logs each frame's hash;
   MVC2_REG_CHECK_DUMP=F:D logs the registers and uniforms at frame F's draw D; MVC2_REG_CHECK_SKIP=r,r,... (hex)
   leaves those registers out of the hash. Each 600 frames it also counts the draws with the lighting on, and those
   whose combiner reads the lighting's colours with it off */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "naomi_host.h"
#include "prof.h"

enum { REGS = 0x300, LOG_EVERY = 600 };

/* a port that fills a table a word at a time from an index register */
typedef struct {
    uint32_t* t;
    uint32_t size, at;
    int dirty;
    uint32_t hash;
} TABLE;

/* a shader's float uniforms: four words a vector in the f32 mode, three in the f24 */
typedef struct {
    uint32_t f[96][4];
    uint32_t at, pos, f32, tmp[4];
    int dirty;
    uint32_t hash;
} UNIFS;

static int mode = -1;
static uint32_t regs[REGS];
static uint8_t skip[REGS];          /* commands and ports: not state */
static int regs_dirty = 1;
static uint32_t regs_hash;
static UNIFS vs_unif, gs_unif;
static uint32_t vs_code_t[4096], vs_op_t[128], gs_code_t[4096], gs_op_t[128];
static uint32_t light_t[32 * 256], fog_t[128], proc_t[8 * 256], gas_t[32];
static TABLE vs_code = { vs_code_t, 4096 }, vs_op = { vs_op_t, 128 }, gs_code = { gs_code_t, 4096 },
             gs_op = { gs_op_t, 128 }, light = { light_t, 32 * 256 }, fog = { fog_t, 128 }, proc = { proc_t, 8 * 256 },
             gas = { gas_t, 32 };
static uint32_t fixed[16][3], fixed_tmp[3], fixed_at, fixed_pos, immediate;
static int fixed_dirty = 1;
static uint32_t fixed_hash;
static uint32_t frames, draws, frame_draws, frame_hash, run_hash = 2166136261u, skipped, f24s, far_regs;
static int frame_bad;
static uint32_t dump_frame = 0xFFFFFFFFu, dump_draw;
static uint8_t unhashed[REGS];      /* MVC2_REG_CHECK_SKIP */
static uint32_t lit_draws, unlit_reads;

static uint32_t fnv(uint32_t h, const uint32_t* w, uint32_t n) {
    uint32_t i;
    for (i = 0; i < n; i++) {
        h = (h ^ w[i]) * 16777619u;
    }
    return h;
}

static void table_put(TABLE* t, uint32_t v) {
    t->t[t->at % t->size] = v;
    t->at++;
    t->dirty = 1;
}

static void unif_put(UNIFS* u, uint32_t v) {
    u->tmp[u->pos++] = v;
    if (u->pos == (u->f32 ? 4u : 3u)) {
        if (!u->f32) {
            u->tmp[3] = 0xF24F24F2u;
            f24s++;
        }
        if (u->at < 96) {
            memcpy(u->f[u->at], u->tmp, sizeof u->tmp);
        }
        u->at++;
        u->pos = 0;
        u->dirty = 1;
    }
}

static void init(void) {
    static const uint16_t commands[] = { 0x010, 0x063, 0x110, 0x111, 0x22E, 0x22F, 0x231, 0x25F, 0x28F, 0x2BF,
                                         0x2C0, 0x290, 0x2CB, 0x2D5, 0x29B, 0x2A5, 0x1C5, 0x0E6, 0x0AF, 0x232, 0x123,
                                         0x124 };
    const char* e = getenv("MVC2_REG_CHECK");
    const char* d = getenv("MVC2_REG_CHECK_DUMP");
    const char* k = getenv("MVC2_REG_CHECK_SKIP");
    unsigned i;
    mode = e != NULL ? atoi(e) : 0;
    if (mode <= 0) {
        mode = 0;
        return;
    }
    for (i = 0; i < sizeof commands / sizeof *commands; i++) {
        skip[commands[i]] = 1;
    }
    for (i = 0x238; i <= 0x23D; i++) {      /* the command buffers' sizes, addresses and jumps */
        skip[i] = 1;
    }
    for (i = 0; i < 8; i++) {               /* the ports */
        skip[0x2C1 + i] = skip[0x291 + i] = skip[0x2CC + i] = skip[0x2D6 + i] = skip[0x29C + i] = skip[0x2A6 + i] = 1;
        skip[0x1C8 + i] = skip[0x0E8 + i] = skip[0x0B0 + i] = 1;
    }
    skip[0x233] = skip[0x234] = skip[0x235] = 1;
    if (d != NULL) {
        unsigned f, n;
        if (sscanf(d, "%u:%u", &f, &n) == 2) {
            dump_frame = f;
            dump_draw = n;
        }
    }
    while (k != NULL && *k != '\0') {
        char* end;
        unsigned long r = strtoul(k, &end, 16);
        if (end == k) {
            break;
        }
        if (r < REGS) {
            unhashed[r] = 1;
        }
        k = *end == ',' ? end + 1 : end;
    }
    host_log("reg check: the GPU's registers, uniforms and tables hashed at each draw (MVC2_REG_CHECK=%d)", mode);
}

static void dump(void) {
    char line[200];
    unsigned i, n = 0;
    host_log("reg check: frame %lu draw %lu", (unsigned long)frames, (unsigned long)frame_draws);
    for (i = 0; i < REGS; i++) {
        if (regs[i] != 0 && !skip[i]) {
            n += (unsigned)snprintf(line + n, sizeof line - n, " %03x=%08lx", i, (unsigned long)regs[i]);
            if (n > 150) {
                host_log("reg check: regs%s", line);
                n = 0;
            }
        }
    }
    if (n > 0) {
        host_log("reg check: regs%s", line);
    }
    for (i = 0; i < 96; i++) {
        const uint32_t* f = vs_unif.f[i];
        if (f[0] | f[1] | f[2] | f[3]) {
            host_log("reg check: vs c%u %08lx %08lx %08lx %08lx", i, (unsigned long)f[0], (unsigned long)f[1],
                     (unsigned long)f[2], (unsigned long)f[3]);
        }
    }
    for (i = 0; i < 16; i++) {
        if (fixed[i][0] | fixed[i][1] | fixed[i][2]) {
            host_log("reg check: fixed %u %08lx %08lx %08lx", i, (unsigned long)fixed[i][0], (unsigned long)fixed[i][1],
                     (unsigned long)fixed[i][2]);
        }
    }
}

static uint32_t sub(TABLE* t) {
    if (t->dirty) {
        t->hash = fnv(2166136261u, t->t, t->size);
        t->dirty = 0;
    }
    return t->hash;
}

static uint32_t sub_unif(UNIFS* u) {
    if (u->dirty) {
        u->hash = fnv(2166136261u, &u->f[0][0], 96 * 4);
        u->dirty = 0;
    }
    return u->hash;
}

/* a combiner source of 1 or 2: the lighting's primary or secondary colour */
static int reads_lighting(void) {
    static const uint16_t sources[6] = { 0x0C0, 0x0C8, 0x0D0, 0x0D8, 0x0F0, 0x0F8 };
    int i, j;
    for (i = 0; i < 6; i++) {
        uint32_t v = regs[sources[i]];
        for (j = 0; j < 28; j += 4) {
            uint32_t n = v >> j & 15;
            if (j != 12 && (n == 1 || n == 2)) {
                return 1;
            }
        }
    }
    return 0;
}

static void draw(void) {
    uint32_t h[12];
    int lit = (regs[0x08F] & 1) && !(regs[0x1C6] & 1);
    lit_draws += (uint32_t)lit;
    unlit_reads += (uint32_t)(!lit && reads_lighting());
    if (regs_dirty) {
        uint32_t i, x = 2166136261u;
        for (i = 0; i < REGS; i++) {
            x = (x ^ (unhashed[i] ? 0 : regs[i])) * 16777619u;
        }
        regs_hash = x;
        regs_dirty = 0;
    }
    if (fixed_dirty) {
        fixed_hash = fnv(2166136261u, &fixed[0][0], 16 * 3);
        fixed_dirty = 0;
    }
    h[0] = regs_hash;
    h[1] = sub_unif(&vs_unif);
    h[2] = sub_unif(&gs_unif);
    h[3] = sub(&vs_code);
    h[4] = sub(&vs_op);
    h[5] = sub(&gs_code);
    h[6] = sub(&gs_op);
    h[7] = sub(&light);
    h[8] = sub(&fog);
    h[9] = sub(&proc);
    h[10] = sub(&gas);
    h[11] = fixed_hash;
    frame_hash = fnv(frame_hash, h, 12);
    if (frames == dump_frame && frame_draws == dump_draw) {
        dump();
    }
    frame_draws++;
    draws++;
}

static void put(uint32_t reg, uint32_t v, uint32_t mask) {
    if (reg >= REGS) {
        far_regs++;
        return;
    }
    switch (reg) {
    case 0x22E:
    case 0x22F: draw(); return;
    case 0x2C0: vs_unif.at = v & 0xFF; vs_unif.f32 = v >> 31; vs_unif.pos = 0; return;
    case 0x290: gs_unif.at = v & 0xFF; gs_unif.f32 = v >> 31; gs_unif.pos = 0; return;
    case 0x2CB: vs_code.at = v & 0xFFF; return;
    case 0x2D5: vs_op.at = v & 0x7F; return;
    case 0x29B: gs_code.at = v & 0xFFF; return;
    case 0x2A5: gs_op.at = v & 0x7F; return;
    case 0x1C5: light.at = (v >> 8 & 0x1F) * 256 + (v & 0xFF); return;
    case 0x0E6: fog.at = v & 0x7F; return;
    case 0x0AF: proc.at = (v >> 8 & 0xF) * 256 + (v & 0xFF); return;
    case 0x123: gas.at = v & 0x1F; return;
    case 0x124: table_put(&gas, v); return;
    case 0x232: fixed_at = v & 0xF; fixed_pos = 0; return;
    case 0x233:
    case 0x234:
    case 0x235:
        fixed_tmp[fixed_pos++] = v;
        if (fixed_pos == 3) {
            if (fixed_at == 0xF) {
                immediate++;
            } else {
                memcpy(fixed[fixed_at], fixed_tmp, sizeof fixed_tmp);
                fixed_dirty = 1;
            }
            fixed_pos = 0;
        }
        return;
    case 0x23C:
    case 0x23D:
        frame_bad = 1;      /* a jump to words not read here */
        return;
    default: break;
    }
    if (reg >= 0x2C1 && reg <= 0x2C8) {
        unif_put(&vs_unif, v);
    } else if (reg >= 0x291 && reg <= 0x298) {
        unif_put(&gs_unif, v);
    } else if (reg >= 0x2CC && reg <= 0x2D3) {
        table_put(&vs_code, v);
    } else if (reg >= 0x2D6 && reg <= 0x2DD) {
        table_put(&vs_op, v);
    } else if (reg >= 0x29C && reg <= 0x2A3) {
        table_put(&gs_code, v);
    } else if (reg >= 0x2A6 && reg <= 0x2AD) {
        table_put(&gs_op, v);
    } else if (reg >= 0x1C8 && reg <= 0x1CF) {
        table_put(&light, v);
    } else if (reg >= 0x0E8 && reg <= 0x0EF) {
        table_put(&fog, v);
    } else if (reg >= 0x0B0 && reg <= 0x0B7) {
        table_put(&proc, v);
    } else if (!skip[reg]) {
        uint32_t m = (mask & 1 ? 0xFFu : 0) | (mask & 2 ? 0xFF00u : 0) | (mask & 4 ? 0xFF0000u : 0) |
                     (mask & 8 ? 0xFF000000u : 0);
        uint32_t n = (regs[reg] & ~m) | (v & m);
        if (n != regs[reg]) {
            regs[reg] = n;
            regs_dirty = 1;
        }
    }
}

void reg_check_frame(const uint32_t* w, uint32_t n) {
    uint32_t k = 0;
    if (mode < 0) {
        init();
    }
    if (mode == 0) {
        return;
    }
    frame_hash = 2166136261u;
    frame_draws = 0;
    frame_bad = 0;
    while (k + 2 <= n) {
        uint32_t h = w[k + 1], reg = h & 0xFFFF, extra = h >> 20 & 0xFF, mask = h >> 16 & 0xF, i;
        put(reg, w[k], mask);
        for (i = 0; i < extra && k + 2 + i < n; i++) {
            if (h >> 31) {
                reg++;
            }
            put(reg, w[k + 2 + i], mask);
        }
        k += 2 + extra + (extra & 1);
    }
    if (frame_bad) {
        skipped++;
    } else {
        run_hash = fnv(run_hash, &frame_hash, 1);
    }
    if (mode >= 2) {
        host_log("reg check: frame %lu: %08lx, %lu draws%s", (unsigned long)frames, (unsigned long)frame_hash,
                 (unsigned long)frame_draws, frame_bad ? " (a jump: not counted)" : "");
    }
    if (++frames % LOG_EVERY == 0) {
        host_log("reg check: frames %lu, %08lx, %lu draws, %lu frames not counted, %lu f24 uniforms, %lu immediate "
                 "vertices, %lu writes past the registers",
                 (unsigned long)frames, (unsigned long)run_hash, (unsigned long)draws, (unsigned long)skipped,
                 (unsigned long)f24s, (unsigned long)immediate, (unsigned long)far_regs);
        host_log("reg check: %lu draws with the lighting on, %lu reading it with it off", (unsigned long)lit_draws,
                 (unsigned long)unlit_reads);
    }
}
