/*
 * arm7.c  The AICA's sound CPU, an ARM7DI (ARMv4, no Thumb), interpreted; the AICA's interrupt is its FIQ.
 * Cycles are counted roughly: one an instruction, more for memory and branches.
 */

#include <string.h>

#include "arm7.h"
#ifdef ARM7_JIT
#include "arm7_jit.h"
#include "naomi_host.h"
#endif

enum { M_USR = 0x10, M_FIQ = 0x11, M_IRQ = 0x12, M_SVC = 0x13, M_ABT = 0x17, M_UND = 0x1B, M_SYS = 0x1F };
#define F_N 0x80000000u
#define F_Z 0x40000000u
#define F_C 0x20000000u
#define F_V 0x10000000u
#define F_I 0x80u
#define F_F 0x40u

static uint32_t r[16];
static uint32_t cpsr;
static uint32_t spsr;               /* the current mode's */
static uint32_t bank_r8[2][5];      /* r8-r12: [0] every mode but FIQ, [1] FIQ */
static uint32_t bank_r13[6], bank_r14[6], bank_spsr[6];   /* usr/sys, fiq, irq, svc, abt, und */
static int fiq_line;
static uint64_t cycles_run;
static uint16_t cond_ok[16];        /* for each condition, a bit for each NZCV value it passes */
static int run_left;                /* arm7_stop: the run ends after the instruction */
static int fiq_run;                 /* arm7_run_fiq: the run ends when the mode leaves FIQ */
static int event;                   /* the run must look: a stop, or an FIQ it can take */
#define DEC_N 8192                  /* the decode tables cover the RAM's first 32 KB */
static uint32_t dec_live[DEC_N / 32];   /* the words with an entry in either table */
static uint32_t code_gen;           /* counts the entries dropped */
static uint32_t code_changes;       /* writes from outside to the first 32 KB */
static int code_track = 1;          /* MVC2_ARMTRACK: every write drops the entries it covers, so a fetch trusts them */
static void dec_drop(uint32_t i);
static void dec_clear(void);

#ifdef ARM7_JIT
static int jit_on;                  /* there is memory to run code from */
static int jit_wanted, jit_side = 1, jit_tried;    /* wanted: arm7_translate */
static ARM7_JIT_STATE jit_state = { r, &cpsr, &event, &aica_ram, &aica_ram_mask };
static int jit2, jit2_ready;         /* arm7_translate2's mode; the translator made */
static int chk_on, chk_io, chk_n;   /* a block checked: the RAM's bytes before the interpreter's writes, I/O seen */
typedef struct {
    uint32_t a;
    int size;
    uint8_t old[4], now[4];
} CHK_WRITE;
static CHK_WRITE chk_log[64];
static void chk_save(uint32_t a, int size);
#define CHK_SAVE(a, size) do { if (chk_on) chk_save(a, size); } while (0)
#define CHK_IO() (chk_io |= chk_on)
#else
#define CHK_SAVE(a, size) ((void)0)
#define CHK_IO() ((void)0)
#endif

/* built into arm7_run, so that its state stays in registers */
#define INLINE static inline __attribute__((always_inline))

static int bank_of(uint32_t mode) {
    switch (mode & 0x1F) {
    case M_FIQ: return 1;
    case M_IRQ: return 2;
    case M_SVC: return 3;
    case M_ABT: return 4;
    case M_UND: return 5;
    default: return 0;
    }
}

/* a new CPSR: the banked registers follow its mode */
static void set_cpsr(uint32_t v) {
    int from = bank_of(cpsr), to = bank_of(v);
    if (from != to) {
        bank_r13[from] = r[13];
        bank_r14[from] = r[14];
        bank_spsr[from] = spsr;
        if ((from == 1) != (to == 1)) {
            memcpy(bank_r8[from == 1], &r[8], sizeof bank_r8[0]);
            memcpy(&r[8], bank_r8[to == 1], sizeof bank_r8[0]);
        }
        r[13] = bank_r13[to];
        r[14] = bank_r14[to];
        spsr = bank_spsr[to];
    }
    if (fiq_run && (v & 0x1F) != M_FIQ) {
        run_left = 0;
        event = 1;
    }
    if (fiq_line && !(v & F_F)) {
        event = 1;
    }
    cpsr = v;
}

static void exception(uint32_t mode, uint32_t vector, uint32_t ret, uint32_t mask) {
    uint32_t old = cpsr;
    set_cpsr((cpsr & ~0x1Fu) | mode | mask);
    spsr = old;
    r[14] = ret;
    r[15] = vector;
}

void arm7_reset(void) {
    int c, f;
#ifdef ARM7_JIT
    if (jit_on) {
        arm7_jit_reset();
    }
    if (jit2_ready > 0) {
        arm7_jit2_reset();
    }
#endif
    dec_clear();
    memset(r, 0, sizeof r);
    memset(bank_r8, 0, sizeof bank_r8);
    memset(bank_r13, 0, sizeof bank_r13);
    memset(bank_r14, 0, sizeof bank_r14);
    memset(bank_spsr, 0, sizeof bank_spsr);
    spsr = 0;
    cpsr = M_SVC | F_I | F_F;
    fiq_line = 0;
    for (c = 0; c < 16; c++) {
        cond_ok[c] = 0;
        for (f = 0; f < 16; f++) {
            int n = f >> 3 & 1, z = f >> 2 & 1, cy = f >> 1 & 1, v = f & 1, ok;
            switch (c) {
            case 0: ok = z; break;
            case 1: ok = !z; break;
            case 2: ok = cy; break;
            case 3: ok = !cy; break;
            case 4: ok = n; break;
            case 5: ok = !n; break;
            case 6: ok = v; break;
            case 7: ok = !v; break;
            case 8: ok = cy && !z; break;
            case 9: ok = !cy || z; break;
            case 10: ok = n == v; break;
            case 11: ok = n != v; break;
            case 12: ok = !z && n == v; break;
            case 13: ok = z || n != v; break;
            case 14: ok = 1; break;
            default: ok = 0; break;
            }
            cond_ok[c] |= (uint16_t)(ok << f);
        }
    }
}

void arm7_set_fiq(int on) {
    fiq_line = on;
    if (on && !(cpsr & F_F)) {
        event = 1;
    }
}

uint32_t arm7_pc(void) {
    return r[15];
}

void arm7_get_regs(ARM7_REGS* s) {
    memcpy(s->r, r, sizeof r);
    s->cpsr = cpsr;
    s->spsr = spsr;
    memcpy(s->b8, bank_r8, sizeof s->b8);
    memcpy(s->b13, bank_r13, sizeof s->b13);
    memcpy(s->b14, bank_r14, sizeof s->b14);
    memcpy(s->bs, bank_spsr, sizeof s->bs);
    s->fiq_line = fiq_line;
}

void arm7_set_regs(const ARM7_REGS* s) {
    memcpy(r, s->r, sizeof r);
    cpsr = s->cpsr;
    spsr = s->spsr;
    memcpy(bank_r8, s->b8, sizeof s->b8);
    memcpy(bank_r13, s->b13, sizeof s->b13);
    memcpy(bank_r14, s->b14, sizeof s->b14);
    memcpy(bank_spsr, s->bs, sizeof s->bs);
    fiq_line = s->fiq_line;
}

int arm7_fiq_line(void) {
    return fiq_line;
}

int arm7_fiq_due(void) {
    return fiq_line && !(cpsr & F_F) && bank_of(cpsr) != 1;
}

uint32_t arm7_code_changes(void) {
    return code_changes;
}

uint64_t arm7_cycles(void) {
    return cycles_run;
}

/* ---- memory: the RAM directly, the rest through the AICA ---- */

/* a write: the decoded word it covers dropped; over translated code, the blocks are checked again, and a block
   running stops after it */
static inline void code_written(uint32_t a) {
    a &= aica_ram_mask;
    if (a < DEC_N * 4 && (dec_live[a >> 7] >> (a >> 2 & 31) & 1)) {
        dec_drop(a >> 2);
    }
#ifdef ARM7_JIT
    if (a - arm7_jit_range[0] < arm7_jit_range[1] && arm7_jit_covers(a & ~3u)) {
        arm7_jit_check();
        event = 1;
    }
    if (a - arm7_jit2_range[0] < arm7_jit2_range[1] && arm7_jit2_covers(a & ~3u)) {
        arm7_jit2_check();
        event = 1;
    }
#endif
}

static inline uint32_t rd32(uint32_t a) {
    uint32_t v;
    if ((a & 0xFF800000u) == 0) {
        memcpy(&v, aica_ram + (a & aica_ram_mask & ~3u), 4);
        return v;
    }
    CHK_IO();
    return aica_arm_read32(a & ~3u);
}

static inline uint32_t rd8(uint32_t a) {
    if ((a & 0xFF800000u) == 0) {
        return aica_ram[a & aica_ram_mask];
    }
    CHK_IO();
    return aica_arm_read8(a);
}

static inline uint32_t rd16(uint32_t a) {
    uint16_t v;
    if ((a & 0xFF800000u) == 0) {
        memcpy(&v, aica_ram + (a & aica_ram_mask & ~1u), 2);
        return v;
    }
    CHK_IO();
    return aica_arm_read16(a & ~1u);
}

static inline void wr32(uint32_t a, uint32_t v) {
    if ((a & 0xFF800000u) == 0) {
        CHK_SAVE(a & aica_ram_mask & ~3u, 4);
        memcpy(aica_ram + (a & aica_ram_mask & ~3u), &v, 4);
        code_written(a);
        return;
    }
    CHK_IO();
    aica_arm_write32(a & ~3u, v);
}

static inline void wr16(uint32_t a, uint32_t v) {
    uint16_t h = (uint16_t)v;
    if ((a & 0xFF800000u) == 0) {
        CHK_SAVE(a & aica_ram_mask & ~1u, 2);
        memcpy(aica_ram + (a & aica_ram_mask & ~1u), &h, 2);
        code_written(a);
        return;
    }
    CHK_IO();
    aica_arm_write16(a & ~1u, h);
}

static inline void wr8(uint32_t a, uint32_t v) {
    if ((a & 0xFF800000u) == 0) {
        CHK_SAVE(a & aica_ram_mask, 1);
        aica_ram[a & aica_ram_mask] = (uint8_t)v;
        code_written(a);
        return;
    }
    CHK_IO();
    aica_arm_write8(a, v & 0xFF);
}

/* a word load from an address not on a word: the word turned so that the addressed byte is lowest */
static inline uint32_t ldr_word(uint32_t a) {
    uint32_t v = rd32(a), s = (a & 3) * 8;
    return s ? v >> s | v << (32 - s) : v;
}

/* ---- the barrel shifter ---- */

/* operand 2 of a data processing instruction; *carry gets the shifter's carry out */
static inline uint32_t shifter(uint32_t op, uint32_t* carry) {
    uint32_t c = cpsr >> 29 & 1;
    if (op & (1u << 25)) {
        uint32_t rot = (op >> 7) & 0x1E, imm = op & 0xFF;
        if (rot) {
            imm = imm >> rot | imm << (32 - rot);
            c = imm >> 31;
        }
        *carry = c;
        return imm;
    } else {
        uint32_t rm = r[op & 15], type = (op >> 5) & 3, amt;
        if (op & 0x10) {
            amt = r[(op >> 8) & 15] & 0xFF;
            if ((op & 15) == 15) {
                rm += 4;            /* the PC reads 12 ahead with a register shift */
            }
            if (amt == 0) {
                *carry = c;
                return rm;
            }
            switch (type) {
            case 0:
                if (amt < 32) {
                    *carry = rm >> (32 - amt) & 1;
                    return rm << amt;
                }
                *carry = amt == 32 ? rm & 1 : 0;
                return 0;
            case 1:
                if (amt < 32) {
                    *carry = rm >> (amt - 1) & 1;
                    return rm >> amt;
                }
                *carry = amt == 32 ? rm >> 31 : 0;
                return 0;
            case 2:
                if (amt < 32) {
                    *carry = (uint32_t)((int32_t)rm >> (amt - 1)) & 1;
                    return (uint32_t)((int32_t)rm >> amt);
                }
                *carry = rm >> 31;
                return (uint32_t)((int32_t)rm >> 31);
            default:
                amt &= 31;
                if (amt == 0) {
                    *carry = rm >> 31;
                    return rm;
                }
                *carry = rm >> (amt - 1) & 1;
                return rm >> amt | rm << (32 - amt);
            }
        }
        amt = (op >> 7) & 31;
        switch (type) {
        case 0:
            if (amt) {
                c = rm >> (32 - amt) & 1;
                rm <<= amt;
            }
            *carry = c;
            return rm;
        case 1:
            if (amt == 0) {
                *carry = rm >> 31;
                return 0;
            }
            *carry = rm >> (amt - 1) & 1;
            return rm >> amt;
        case 2:
            if (amt == 0) {
                *carry = rm >> 31;
                return (uint32_t)((int32_t)rm >> 31);
            }
            *carry = (uint32_t)((int32_t)rm >> (amt - 1)) & 1;
            return (uint32_t)((int32_t)rm >> amt);
        default:
            if (amt == 0) {         /* RRX */
                *carry = rm & 1;
                return c << 31 | rm >> 1;
            }
            *carry = rm >> (amt - 1) & 1;
            return rm >> amt | rm << (32 - amt);
        }
    }
}

/* the offset of a single data transfer with a register: an immediate shift only */
static inline uint32_t reg_offset(uint32_t op) {
    uint32_t rm = r[op & 15], amt = (op >> 7) & 31;
    switch ((op >> 5) & 3) {
    case 0: return rm << amt;
    case 1: return amt ? rm >> amt : 0;
    case 2: return (uint32_t)((int32_t)rm >> (amt ? amt : 31));
    default: return amt ? rm >> amt | rm << (32 - amt) : (cpsr >> 29 & 1) << 31 | rm >> 1;
    }
}

static inline void set_nz(uint32_t v) {
    cpsr = (cpsr & ~(F_N | F_Z)) | (v & F_N) | (v ? 0 : F_Z);
}

static inline void set_nzcv(uint32_t res, uint32_t c, uint32_t v) {
    cpsr = (cpsr & 0x0FFFFFFFu) | (res & F_N) | (res ? 0 : F_Z) | (c << 29) | (v << 28);
}

/* ---- the instructions ---- */

INLINE int data_processing(uint32_t op, int* branched) {
    uint32_t carry, b, a, res = 0, rd = (op >> 12) & 15, opc = (op >> 21) & 15;
    int s = op >> 20 & 1, write = 1;
    uint32_t rn = (op >> 16) & 15;
    if ((op & 0x02000FF0u) == 0) {          /* a register, not shifted */
        b = r[op & 15];
        carry = cpsr >> 29 & 1;
    } else {
        b = shifter(op, &carry);
    }
    a = r[rn];
    if (rn == 15 && !(op & (1u << 25)) && (op & 0x10)) {
        a += 4;
    }
    switch (opc) {
    case 0x0: res = a & b; if (s) { set_nz(res); cpsr = (cpsr & ~F_C) | carry << 29; } break;
    case 0x1: res = a ^ b; if (s) { set_nz(res); cpsr = (cpsr & ~F_C) | carry << 29; } break;
    case 0x2: res = a - b; if (s) set_nzcv(res, a >= b, ((a ^ b) & (a ^ res)) >> 31); break;
    case 0x3: res = b - a; if (s) set_nzcv(res, b >= a, ((b ^ a) & (b ^ res)) >> 31); break;
    case 0x4: res = a + b; if (s) set_nzcv(res, res < a, (~(a ^ b) & (a ^ res)) >> 31); break;
    case 0x5: {
        uint32_t c = cpsr >> 29 & 1;
        uint64_t w = (uint64_t)a + b + c;
        res = (uint32_t)w;
        if (s) set_nzcv(res, (uint32_t)(w >> 32), (~(a ^ b) & (a ^ res)) >> 31);
        break;
    }
    case 0x6: {
        uint32_t c = cpsr >> 29 & 1;
        uint64_t w = (uint64_t)a - b - (1 - c);
        res = (uint32_t)w;
        if (s) set_nzcv(res, (uint32_t)(w >> 32) == 0, ((a ^ b) & (a ^ res)) >> 31);
        break;
    }
    case 0x7: {
        uint32_t c = cpsr >> 29 & 1;
        uint64_t w = (uint64_t)b - a - (1 - c);
        res = (uint32_t)w;
        if (s) set_nzcv(res, (uint32_t)(w >> 32) == 0, ((b ^ a) & (b ^ res)) >> 31);
        break;
    }
    case 0x8: res = a & b; set_nz(res); cpsr = (cpsr & ~F_C) | carry << 29; write = 0; break;
    case 0x9: res = a ^ b; set_nz(res); cpsr = (cpsr & ~F_C) | carry << 29; write = 0; break;
    case 0xA: res = a - b; set_nzcv(res, a >= b, ((a ^ b) & (a ^ res)) >> 31); write = 0; break;
    case 0xB: res = a + b; set_nzcv(res, res < a, (~(a ^ b) & (a ^ res)) >> 31); write = 0; break;
    case 0xC: res = a | b; if (s) { set_nz(res); cpsr = (cpsr & ~F_C) | carry << 29; } break;
    case 0xD: res = b; if (s) { set_nz(res); cpsr = (cpsr & ~F_C) | carry << 29; } break;
    case 0xE: res = a & ~b; if (s) { set_nz(res); cpsr = (cpsr & ~F_C) | carry << 29; } break;
    default: res = ~b; if (s) { set_nz(res); cpsr = (cpsr & ~F_C) | carry << 29; } break;
    }
    if (rd == 15 && s) {
        /* to the PC with S: back from an exception, the mode's saved status put back */
        if (write) {
            r[15] = res & ~3u;
            *branched = 1;
        }
        if (bank_of(cpsr) != 0) {
            set_cpsr(spsr);
        }
        return write ? 3 : 1;
    }
    if (write) {
        r[rd] = rd == 15 ? res & ~3u : res;
        *branched |= rd == 15;
        return rd == 15 ? 3 : 1;
    }
    return 1;
}

static void psr_transfer(uint32_t op) {
    if (!(op & (1u << 21))) {                     /* MRS */
        r[(op >> 12) & 15] = op & (1u << 22) ? spsr : cpsr;
        return;
    } else {                                    /* MSR */
        uint32_t v, mask = 0;
        if (op & (1u << 25)) {
            uint32_t rot = (op >> 7) & 0x1E, imm = op & 0xFF;
            v = rot ? imm >> rot | imm << (32 - rot) : imm;
        } else {
            v = r[op & 15];
        }
        if (op & (1u << 19)) mask |= 0xFF000000u;
        if (op & (1u << 16)) mask |= 0x000000FFu;
        if (op & (1u << 22)) {
            if (bank_of(cpsr) != 0) {
                spsr = (spsr & ~mask) | (v & mask);
            }
            return;
        }
        if ((cpsr & 0x1F) == M_USR) {
            mask &= 0xFF000000u;
        }
        set_cpsr((cpsr & ~mask) | (v & mask) | 0x10);
    }
}

static int multiply(uint32_t op) {
    uint32_t rd = (op >> 16) & 15, rn = (op >> 12) & 15, rs = r[(op >> 8) & 15], rm = r[op & 15];
    if (op & (1u << 23)) {                      /* long */
        uint64_t res;
        if (op & (1u << 22)) {
            res = (uint64_t)((int64_t)(int32_t)rm * (int32_t)rs);
        } else {
            res = (uint64_t)rm * rs;
        }
        if (op & (1u << 21)) {
            res += (uint64_t)r[rd] << 32 | r[rn];
        }
        r[rn] = (uint32_t)res;
        r[rd] = (uint32_t)(res >> 32);
        if (op & (1u << 20)) {
            cpsr = (cpsr & ~(F_N | F_Z)) | ((uint32_t)(res >> 32) & F_N) | (res ? 0 : F_Z);
        }
        return 4;
    } else {
        uint32_t res = rm * rs;
        if (op & (1u << 21)) {
            res += r[rn];
        }
        r[rd] = res;
        if (op & (1u << 20)) {
            set_nz(res);
        }
        return 3;
    }
}

INLINE int single_transfer(uint32_t op, int* branched) {
    if ((op & 0x03200000u) == 0x01000000u) {    /* an immediate offset, no write back */
        uint32_t off = op & 0xFFF, rd = (op >> 12) & 15;
        uint32_t addr = op & (1u << 23) ? r[(op >> 16) & 15] + off : r[(op >> 16) & 15] - off;
        if (op & (1u << 20)) {
            uint32_t v = op & (1u << 22) ? rd8(addr) : ldr_word(addr);
            if (rd == 15) {
                r[15] = v & ~3u;
                *branched = 1;
                return 5;
            }
            r[rd] = v;
            return 3;
        }
        if (op & (1u << 22)) {
            wr8(addr, r[rd] + (rd == 15 ? 4 : 0));
        } else {
            wr32(addr, r[rd] + (rd == 15 ? 4 : 0));
        }
        return 2;
    }
    uint32_t rn = (op >> 16) & 15, rd = (op >> 12) & 15, base = r[rn], off, addr;
    int up = op >> 23 & 1, pre = op >> 24 & 1, wb = op >> 21 & 1, load = op >> 20 & 1, byte = op >> 22 & 1;
    off = op & (1u << 25) ? reg_offset(op) : op & 0xFFF;
    addr = pre ? (up ? base + off : base - off) : base;
    if (load) {
        uint32_t v = byte ? rd8(addr) : ldr_word(addr);
        if (!pre) {
            r[rn] = up ? base + off : base - off;
        } else if (wb) {
            r[rn] = addr;
        }
        r[rd] = rd == 15 ? v & ~3u : v;
        *branched |= rd == 15;
        return rd == 15 ? 5 : 3;
    } else {
        uint32_t v = r[rd] + (rd == 15 ? 4 : 0);
        if (byte) {
            wr8(addr, v);
        } else {
            wr32(addr, v);
        }
        if (!pre) {
            r[rn] = up ? base + off : base - off;
        } else if (wb) {
            r[rn] = addr;
        }
        return 2;
    }
}

static int half_transfer(uint32_t op) {
    uint32_t rn = (op >> 16) & 15, rd = (op >> 12) & 15, base = r[rn], off, addr, v;
    int up = op >> 23 & 1, pre = op >> 24 & 1, wb = op >> 21 & 1, load = op >> 20 & 1, sh = (op >> 5) & 3;
    off = op & (1u << 22) ? ((op >> 4) & 0xF0) | (op & 15) : r[op & 15];
    addr = pre ? (up ? base + off : base - off) : base;
    if (load) {
        switch (sh) {
        case 1: v = rd16(addr); break;
        case 2: v = (uint32_t)(int32_t)(int8_t)rd8(addr); break;
        default: v = (uint32_t)(int32_t)(int16_t)rd16(addr); break;
        }
    } else {
        wr16(addr, r[rd] + (rd == 15 ? 4 : 0));
        v = 0;
    }
    if (!pre) {
        r[rn] = up ? base + off : base - off;
    } else if (wb) {
        r[rn] = addr;
    }
    if (load) {
        r[rd] = v;
    }
    return 3;
}

INLINE int block_transfer(uint32_t op, int* branched) {
    uint32_t rn = (op >> 16) & 15, list = op & 0xFFFF, base = r[rn], addr, n = 0, i;
    int up = op >> 23 & 1, pre = op >> 24 & 1, s = op >> 22 & 1, wb = op >> 21 & 1, load = op >> 20 & 1;
    int user = s && !(load && (list & 0x8000)) && bank_of(cpsr) != 0;
    uint32_t saved = cpsr, end;
    n = list - (list >> 1 & 0x5555u);           /* the registers in the list */
    n = (n & 0x3333u) + (n >> 2 & 0x3333u);
    n = (n + (n >> 4)) & 0x0F0Fu;
    n = (n + (n >> 8)) & 0x1Fu;
    if (up) {
        addr = base + (pre ? 4 : 0);
        end = base + n * 4;
    } else {
        addr = base - n * 4 + (pre ? 0 : 4);
        end = base - n * 4;
    }
    if (user) {
        set_cpsr((cpsr & ~0x1Fu) | M_USR);
    }
    if (load) {
        if (wb) {
            r[rn] = end;
        }
        for (i = 0; i < 16; i++) {
            if (list >> i & 1) {
                r[i] = rd32(addr);
                addr += 4;
            }
        }
        if (list & 0x8000) {
            r[15] &= ~3u;
            *branched = 1;
            if (s && bank_of(cpsr) != 0) {
                set_cpsr(spsr);
            }
        }
    } else {
        int first = 1;
        for (i = 0; i < 16; i++) {
            if (list >> i & 1) {
                uint32_t v = i == rn && first ? base : r[i];
                if (i == 15) {
                    v += 4;
                }
                wr32(addr, v);
                addr += 4;
                first = 0;
            }
        }
        if (wb) {
            r[rn] = end;
        }
    }
    if (user) {
        set_cpsr(saved);
    }
    return (int)n + 2 + (load && (list & 0x8000) ? 2 : 0);
}

void arm7_stop(void) {
    run_left = 0;
    event = 1;
}

/* the FIQ's handler, if it is due: taken now and run until it returns, or for about `cycles` */
int arm7_run_fiq(int cycles) {
    int done;
    if (!fiq_line || (cpsr & F_F)) {
        return 0;
    }
    fiq_run = 1;
    done = arm7_run(cycles);
    fiq_run = 0;
    return done;
}

int arm7_fiq_finish(uint32_t at, int used, int cycles, uint32_t r10, uint32_t r11, uint32_t r12, int sp_set,
                    uint32_t sp) {
    uint32_t ret = r[15];
    int done;
    exception(M_FIQ, 0x1C, ret + 4, F_I | F_F);
    r[8] = spsr;
    r[10] = r10;
    r[11] = r11;
    r[12] = r12;
    if (sp_set) {
        r[13] = sp;
    }
    r[14] = ret;
    r[15] = at;
    fiq_run = 1;
    done = arm7_run(cycles - used);
    fiq_run = 0;
    return used + done;
}

void arm7_fiq_return(uint32_t r10, uint32_t r11, uint32_t r12, int sp_set, uint32_t sp) {
    uint32_t ret = r[15];
    exception(M_FIQ, 0x1C, ret + 4, F_I | F_F);
    r[8] = spsr;
    r[10] = r10;
    r[11] = r11;
    r[12] = r12;
    if (sp_set) {
        r[13] = sp;
    }
    r[14] = ret;
    set_cpsr(spsr);
    r[15] = ret;
}

/* ---- the commonest instructions, decoded once ---- */

/* a kind and its fields for each word of code, kept while the word is the same; K_STEP is the rest */
enum {
    K_STEP, K_B, K_BL, K_ADDI, K_SUBI, K_SUBSI, K_CMPI, K_TSTI, K_TSTI_C0, K_MOVI, K_MOVR, K_MOVSR,
    K_LDRB, K_LDR, K_LDR_PC, K_STRB, K_STR, K_PUSH_PC, K_POP_PC,
    K_ANDI, K_ORRI, K_BICI, K_ADD_PC, K_ADDR, K_CMPR, K_PSR, K_COUNT
};
typedef struct {
    uint32_t op;
    const void* go;                 /* the kind's code in arm7_run */
    int32_t v;
    uint8_t rd, rn, rm, cond;
} DEC;
static DEC dec[DEC_N];

static int decode(uint32_t op, int32_t* v) {
    uint32_t rn = (op >> 16) & 15, rd = (op >> 12) & 15, rm = op & 15;
    switch ((op >> 25) & 7) {
    case 5:
        *v = (int32_t)(op << 8) >> 6;
        return op & (1u << 24) ? K_BL : K_B;
    case 1: {                       /* data processing, an immediate */
        uint32_t rot = (op >> 7) & 0x1E, imm = op & 0xFF;
        *v = (int32_t)(rot ? imm >> rot | imm << (32 - rot) : imm);
        if ((op & 0x01900000u) == 0x01000000u) {
            return K_PSR;
        }
        if (rd == 15) {
            return K_STEP;
        }
        if (rn == 15) {
            return ((op >> 20) & 0x1F) == 0x08 ? K_ADD_PC : K_STEP;
        }
        switch ((op >> 20) & 0x1F) {
        case 0x00: return K_ANDI;
        case 0x18: return K_ORRI;
        case 0x1C: return K_BICI;
        case 0x08: return K_ADDI;
        case 0x04: return K_SUBI;
        case 0x05: return K_SUBSI;
        case 0x15: return K_CMPI;
        case 0x11: return rot == 0 ? K_TSTI : *v >= 0 ? K_TSTI_C0 : K_STEP;
        case 0x1A: return K_MOVI;
        }
        return K_STEP;
    }
    case 0:
        if ((op & 0x0F9000F0u) == 0x01000000u) {
            return K_PSR;
        }
        if ((op & 0xFF0) != 0 || rd == 15 || rm == 15) {    /* a register, not shifted */
            return K_STEP;
        }
        switch ((op >> 20) & 0x1F) {
        case 0x1A: return rn == 0 ? K_MOVR : K_STEP;
        case 0x1B: return rn == 0 ? K_MOVSR : K_STEP;
        case 0x08: return rn != 15 ? K_ADDR : K_STEP;
        case 0x15: return rn != 15 ? K_CMPR : K_STEP;
        }
        return K_STEP;
    case 2:                         /* LDR, LDRB, STR, STRB, an immediate offset, no write back */
        if ((op & 0x01200000u) != 0x01000000u || rd == 15) {
            return K_STEP;
        }
        *v = op & (1u << 23) ? (int32_t)(op & 0xFFF) : -(int32_t)(op & 0xFFF);
        if (rn == 15) {
            return (op & 0x00500000u) == 0x00100000u ? K_LDR_PC : K_STEP;
        }
        switch ((op >> 20) & 5) {
        case 0: return K_STR;
        case 1: return K_LDR;
        case 4: return K_STRB;
        default: return K_LDRB;
        }
    case 4:                         /* STMFD rn!, {pc} and LDMFD rn!, {pc} */
        if (rn != 15 && (op & 0x01F0FFFFu) == 0x01208000u) {
            return K_PUSH_PC;
        }
        if (rn != 15 && (op & 0x01F0FFFFu) == 0x00B08000u) {
            return K_POP_PC;
        }
        return K_STEP;
    }
    return K_STEP;
}

/* one instruction whose condition passed; gives its cycles */
INLINE int step(uint32_t op, uint32_t pc, int* branched) {
    switch ((op >> 25) & 7) {
    case 0:
        if ((op & 0x0FC000F0u) == 0x00000090u || (op & 0x0F8000F0u) == 0x00800090u) {
            return multiply(op);
        } else if ((op & 0x0FB00FF0u) == 0x01000090u) {     /* SWP */
            uint32_t a = r[(op >> 16) & 15], v;
            if (op & (1u << 22)) {
                v = rd8(a);
                wr8(a, r[op & 15]);
            } else {
                v = ldr_word(a);
                wr32(a, r[op & 15]);
            }
            r[(op >> 12) & 15] = v;
            return 4;
        } else if ((op & 0x90u) == 0x90u) {
            return half_transfer(op);
        } else if ((op & 0x0FFFFFF0u) == 0x012FFF10u) {     /* BX */
            r[15] = r[op & 15] & ~3u;
            *branched = 1;
            return 3;
        } else if ((op & 0x01900000u) == 0x01000000u) {
            psr_transfer(op);
            return 1;
        }
        return data_processing(op, branched);
    case 1:
        if ((op & 0x01900000u) == 0x01000000u) {
            psr_transfer(op);
            return 1;
        }
        return data_processing(op, branched);
    case 2:
        return single_transfer(op, branched);
    case 3:
        if (op & 0x10) {
            exception(M_UND, 0x04, pc + 4, F_I);
            *branched = 1;
            return 3;
        }
        return single_transfer(op, branched);
    case 4:
        return block_transfer(op, branched);
    case 5: {
        uint32_t off = (op & 0x00FFFFFFu) << 2;
        if (off & 0x02000000u) {
            off |= 0xFC000000u;
        }
        if (op & (1u << 24)) {
            r[14] = pc + 4;
        }
        r[15] = pc + 8 + off;
        *branched = 1;
        return 3;
    }
    case 6:
        exception(M_UND, 0x04, pc + 4, F_I);
        *branched = 1;
        return 3;
    default:
        if (op & (1u << 24)) {
            exception(M_SVC, 0x08, pc + 4, F_I);
        } else {
            exception(M_UND, 0x04, pc + 4, F_I);
        }
        *branched = 1;
        return 3;
    }
}

#ifdef ARM7_JIT
/* for the translated code: one instruction, as the interpreter runs it */
int arm7_jit_step(uint32_t op, uint32_t pc) {
    int branched = 0, n;
    JIT_STAT(arm7_jit_stats.steps++);
    if (op >> 28 != 14 && !(cond_ok[op >> 28] >> (cpsr >> 28) & 1)) {
        r[15] = pc + 4;
        return 1;
    }
    r[15] = pc + 8;
    if ((op & 0x0F200000u) == 0x05000000u) {           /* the sound chip's registers, near the call */
        n = single_transfer(op, &branched);
    } else if ((op & 0x0FBF0FFFu) == 0x010F0000u ||
               ((op & 0x0DB0F000u) == 0x0120F000u && ((op & (1u << 25)) || !(op & 0xFF0u)))) {
        psr_transfer(op);                               /* MRS, MSR */
        n = 1;
    } else {
        n = step(op, pc, &branched);
    }
    if (!branched) {
        r[15] = pc + 4;
    }
    return n;
}

uint32_t arm7_jit_ldr(uint32_t a) {
    return ldr_word(a);
}

uint32_t arm7_jit_ldrb(uint32_t a) {
    return rd8(a);
}

void arm7_jit_str(uint32_t a, uint32_t v) {
    wr32(a, v);
}

void arm7_jit_strb(uint32_t a, uint32_t v) {
    wr8(a, v);
}
#endif

#ifdef ARM7_JIT
static void chk_save(uint32_t a, int size) {
    if (chk_n == 64) {
        chk_io = 1;
        return;
    }
    chk_log[chk_n].a = a;
    chk_log[chk_n].size = size;
    memcpy(chk_log[chk_n].old, aica_ram + a, (size_t)size);
    chk_n++;
}

typedef struct {
    uint32_t r[16], cpsr, spsr, b8[2][5], b13[6], b14[6], bs[6];
} CHK_STATE;

static void chk_get(CHK_STATE* s) {
    memcpy(s->r, r, sizeof r);
    s->cpsr = cpsr;
    s->spsr = spsr;
    memcpy(s->b8, bank_r8, sizeof s->b8);
    memcpy(s->b13, bank_r13, sizeof s->b13);
    memcpy(s->b14, bank_r14, sizeof s->b14);
    memcpy(s->bs, bank_spsr, sizeof s->bs);
}

static void chk_set(const CHK_STATE* s) {
    memcpy(r, s->r, sizeof r);
    cpsr = s->cpsr;
    spsr = s->spsr;
    memcpy(bank_r8, s->b8, sizeof s->b8);
    memcpy(bank_r13, s->b13, sizeof s->b13);
    memcpy(bank_r14, s->b14, sizeof s->b14);
    memcpy(bank_spsr, s->bs, sizeof s->bs);
}

/* a block run both ways from the same state: the interpreter first, its writes undone, then the block; any
   difference logged and the interpreter's kept. A run that touches the AICA keeps the interpreter's alone. */
static int jit2_checked(uint32_t pc, int fiq, int done, int cycles) {
    static int logged;
    static CHK_STATE before, after;
    uint32_t end, p = pc;
    int d = 0, dj, i, why = 0, at = 0;
    if (arm7_jit2_run(pc, fiq, done, cycles, &end) < 0) {
        return -1;
    }
    chk_get(&before);
    chk_on = 1;
    chk_io = chk_n = 0;
    for (;;) {
        uint32_t op;
        memcpy(&op, aica_ram + p, 4);
        d += arm7_jit_step(op, p);
        if (chk_io || event || !arm7_jit2_goes_on(p, r[15])) {
            break;
        }
        p = r[15];
    }
    chk_on = 0;
    if (chk_io || event) {
        return done + d;
    }
    chk_get(&after);
    for (i = 0; i < chk_n; i++) {
        memcpy(chk_log[i].now, aica_ram + chk_log[i].a, (size_t)chk_log[i].size);
    }
    for (i = chk_n - 1; i >= 0; i--) {
        memcpy(aica_ram + chk_log[i].a, chk_log[i].old, (size_t)chk_log[i].size);
    }
    chk_set(&before);
    dj = arm7_jit2_run(pc, fiq, done, cycles, NULL);
    JIT_STAT(arm7_jit_stats.checked++);
    if (dj - done != d) {
        why = 'c';
    }
    for (i = 0; i < 16 && !why; i++) {
        if (r[i] != after.r[i]) {
            why = 'r';
            at = i;
        }
    }
    if (!why) {
        CHK_STATE now;
        chk_get(&now);
        if (memcmp(&now, &after, sizeof now)) {
            why = 'p';
        }
    }
    for (i = 0; i < chk_n && !why; i++) {
        if (memcmp(aica_ram + chk_log[i].a, chk_log[i].now, (size_t)chk_log[i].size)) {
            why = 'm';
            at = i;
        }
    }
    if (!why) {
        return dj;
    }
    JIT_STAT(arm7_jit_stats.wrong++);
    if (logged < 40) {
        logged++;
        host_log("sound CPU check: block %04lx-%04lx mode %d wrong (%c%d): cycles %d/%d, r%d %08lx/%08lx, "
                 "cpsr %08lx/%08lx, write %06lx", (unsigned long)pc, (unsigned long)end, fiq, why, at, dj - done, d,
                 why == 'r' ? at : 0, (unsigned long)r[why == 'r' ? at : 0],
                 (unsigned long)after.r[why == 'r' ? at : 0], (unsigned long)cpsr, (unsigned long)after.cpsr,
                 why == 'm' ? (unsigned long)chk_log[at].a : 0ul);
    }
    chk_set(&after);
    for (i = 0; i < chk_n; i++) {
        memcpy(aica_ram + chk_log[i].a, chk_log[i].now, (size_t)chk_log[i].size);
    }
    return done + d;
}

static int jit2_go(uint32_t pc, int done, int cycles) {
    int fiq = (cpsr & 0x1F) == M_FIQ;
    if (!jit2_ready) {
        jit2_ready = arm7_jit2_init(&jit_state, jit2) ? 1 : -1;     /* on the thread that runs it */
        host_log("sound CPU translator (2): %s, %lu KB of tables", jit2_ready > 0 ? "on" : "off",
                 (unsigned long)arm7_jit2_tables / 1024);
    }
    if (jit2_ready < 0) {
        return -1;
    }
    if (jit2 & 16) {
        return jit2_checked(pc, fiq, done, cycles);
    }
    return arm7_jit2_run(pc, fiq, done, cycles, NULL);
}
#endif

void arm7_translate2(int mode) {
#ifdef ARM7_JIT
    jit2 = mode;
#else
    (void)mode;
#endif
}

void arm7_track(int on) {
    code_track = on;
}

void arm7_translate(int on) {
#ifdef ARM7_JIT
    if (jit_wanted && !on) {
        dec_clear();                /* translated code's stores to the RAM went past code_written */
    }
    jit_wanted = on;
#else
    (void)on;
#endif
}

int arm7_run_interpreted(int cycles) {
#ifdef ARM7_JIT
    int done;
    jit_side = 0;
    done = arm7_run(cycles);
    jit_side = 1;
    return done;
#else
    return arm7_run(cycles);
#endif
}

void arm7_code_written(uint32_t addr, uint32_t size) {
    uint32_t i, last = addr + size < DEC_N * 4 ? addr + size : DEC_N * 4;
    if (addr < DEC_N * 4) {
        code_changes++;
    }
    for (i = addr >> 2; i < (last + 3) >> 2; i++) {
        if (dec_live[i >> 5] >> (i & 31) & 1) {
            dec_drop(i);
        }
    }
#ifdef ARM7_JIT
    uint32_t a = addr > arm7_jit_range[0] ? addr & ~3u : arm7_jit_range[0];
    uint32_t end = addr + size < arm7_jit_range[0] + arm7_jit_range[1] ? addr + size : arm7_jit_range[0] + arm7_jit_range[1];
    for (; jit_on && a < end; a += 4) {
        if (arm7_jit_covers(a)) {
            arm7_jit_check();
            break;
        }
    }
    for (a = addr & ~3u; jit2_ready > 0 && a < addr + size && a < ARM7_JIT_WORDS * 4; a += 4) {
        if (arm7_jit2_covers(a)) {
            arm7_jit2_check();
            break;
        }
    }
#endif
}

/* ---- loops that only look: a slot table read until a byte has a bit, run whole turns at a time ---- */

/*
 * S_COUNT:  L: ldrb x,[b,#o]; tst x,#m (or movs x,x); beq P (or bne, P after it)
 *           P: add b,b,#s; subs c,c,#1; bne L  (or add c,c,#1; cmp c,#n; bne L)
 * S_END:    L: add b,b,#s; cmp b,e; (an eq instruction); beq out; ldrb x,[b,#o]; tst x,#m; beq L
 * A turn is skipped while the test gives Z; one whose byte is found, or the last, is the interpreter's.
 */
enum { S_COUNT, S_UP, S_END };
typedef struct {
    uint32_t pc, at[8], word[8];
    int words, shape, turn;         /* turn: its cycles */
    const void* plain[2];           /* the first instruction's own code in each arm7_run */
    uint8_t x, b, c, movs;
    uint32_t o, s, m, n;
} FAST;
#define FAST_N 16
static FAST fast[FAST_N];
static int fasts, fast_on;
static int step_small;              /* K_STEP through one step(), not a copy for each value of bits 20-27; 2: but
                                       for the driver's commonest few */

static __attribute__((noinline)) int step_one(uint32_t op, uint32_t pc, int* branched) {
    return step(op, pc, branched);
}

void arm7_small(int on) {
    step_small = on;
}

void arm7_fast(int on) {
    fast_on = on;
}

static uint32_t word_at(uint32_t a) {
    uint32_t v;
    memcpy(&v, aica_ram + a, 4);
    return v;
}

static uint32_t imm_of(uint32_t op) {
    uint32_t rot = (op >> 7) & 0x1E, imm = op & 0xFF;
    return rot ? imm >> rot | imm << (32 - rot) : imm;
}

static uint32_t b_to(uint32_t op, uint32_t a) {
    return a + 8 + (uint32_t)((int32_t)(op << 8) >> 6);
}

static void keep(FAST* f, uint32_t a) {
    f->at[f->words] = a;
    f->word[f->words++] = word_at(a);
}

static int is_ldrb(uint32_t op, FAST* f) {   /* ldrb x,[b,#o] */
    f->x = (op >> 12) & 15;
    f->b = (op >> 16) & 15;
    f->o = op & 0xFFF;
    return (op & 0xFFF00000u) == 0xE5D00000u && f->x != 15 && f->b != 15 && f->x != f->b;
}

static int is_tst(uint32_t op, FAST* f) {    /* tst x,#m, not rotated */
    f->m = op & 0xFF;
    return (op & 0xFFFFFF00u) == (0xE3100000u | (uint32_t)f->x << 16);
}

static int is_add(uint32_t op, int rd, uint32_t* s) {
    *s = imm_of(op);
    return (op & 0xFFFFF000u) == (0xE2800000u | (uint32_t)rd << 16 | (uint32_t)rd << 12);
}

static int fast_match(uint32_t pc, FAST* f) {
    uint32_t op, p;
    f->words = 0;
    if (pc + 32 > DEC_N * 4) {
        return 0;
    }
    op = word_at(pc);
    if (is_ldrb(op, f)) {
        f->movs = word_at(pc + 4) == (0xE1B00000u | (uint32_t)f->x << 12 | f->x);
        if (!f->movs && !is_tst(word_at(pc + 4), f)) {
            return 0;
        }
        op = word_at(pc + 8);
        if ((op & 0xFF000000u) == 0x0A000000u) {
            p = b_to(op, pc + 8);
            f->turn = 3 + 1 + 3;
        } else if ((op & 0xFF000000u) == 0x1A000000u) {
            p = pc + 12;
            f->turn = 3 + 1 + 1;
        } else {
            return 0;
        }
        if (p + 16 > DEC_N * 4 || (p & 3) || !is_add(word_at(p), f->b, &f->s)) {
            return 0;
        }
        keep(f, pc);
        keep(f, pc + 4);
        keep(f, pc + 8);
        keep(f, p);
        op = word_at(p + 4);
        keep(f, p + 4);
        f->c = (op >> 12) & 15;
        if (f->c == 15 || f->c == f->x || f->c == f->b) {
            return 0;
        }
        if (op == (0xE2500001u | (uint32_t)f->c << 16 | (uint32_t)f->c << 12)) {
            f->shape = S_COUNT;
            p += 8;
            f->turn += 1 + 1 + 3;
        } else if (op == (0xE2800001u | (uint32_t)f->c << 16 | (uint32_t)f->c << 12)
                   && (word_at(p + 8) & 0xFFFFF000u) == (0xE3500000u | (uint32_t)f->c << 16)) {
            f->shape = S_UP;
            f->n = imm_of(word_at(p + 8));
            keep(f, p + 8);
            p += 12;
            f->turn += 1 + 1 + 1 + 3;
        } else {
            return 0;
        }
        op = word_at(p);
        keep(f, p);
        return (op & 0xFF000000u) == 0x1A000000u && b_to(op, p) == pc;
    }
    if (is_add(op, (op >> 16) & 15, &f->s)) {
        f->b = (op >> 16) & 15;
        op = word_at(pc + 4);
        f->c = op & 15;             /* e */
        if (f->b == 15 || (op & 0xFFFFFFF0u) != (0xE1500000u | (uint32_t)f->b << 16) || f->c == 15 || f->c == f->b
            || word_at(pc + 8) >> 28 != 0 || (word_at(pc + 12) & 0xFF000000u) != 0x0A000000u
            || !is_ldrb(word_at(pc + 16), f) || f->b != ((word_at(pc) >> 16) & 15) || !is_tst(word_at(pc + 20), f)) {
            return 0;
        }
        op = word_at(pc + 24);
        if ((op & 0xFF000000u) != 0x0A000000u || b_to(op, pc + 24) != pc || f->x == f->c) {
            return 0;
        }
        for (p = pc; p <= pc + 24; p += 4) {
            keep(f, p);
        }
        f->shape = S_END;
        f->turn = 1 + 1 + 1 + 1 + 3 + 1 + 3;
        return 1;
    }
    return 0;
}

static int fast_find(uint32_t pc) {
    FAST f;
    int i;
    for (i = 0; i < fasts; i++) {
        if (fast[i].pc == pc) {
            break;
        }
    }
    if (i == FAST_N || !fast_match(pc, &f)) {
        return -1;
    }
    f.pc = pc;
    f.plain[0] = i < fasts ? fast[i].plain[0] : NULL;     /* the other table's, if it had the loop */
    f.plain[1] = i < fasts ? fast[i].plain[1] : NULL;
    fast[i] = f;
    fasts += i == fasts;
    return i;
}

/* turns skipped from L while all of them would start before `cycles`; the state as after them */
INLINE int fast_run(const FAST* f, int done, int cycles) {
    const uint8_t* ram = aica_ram;
    uint32_t b = r[f->b], c = r[f->c], x = r[f->x], a, k = 0;
    int i;
    for (i = 0; i < f->words; i++) {
        if (word_at(f->at[i]) != f->word[i]) {
            return done;
        }
    }
    for (;;) {
        if (done + f->turn - 3 >= cycles) {
            break;
        }
        if (f->shape == S_END) {
            a = b + f->s;
            if (a == c || ((a + f->o) & 0xFF800000u)) {
                break;
            }
            x = ram[(a + f->o) & aica_ram_mask];
            if (x & f->m) {
                break;
            }
            b = a;
        } else {
            a = b + f->o;
            if (a & 0xFF800000u) {
                break;
            }
            x = ram[a & aica_ram_mask];
            if (f->movs ? x : x & f->m) {
                break;
            }
            if (f->shape == S_COUNT ? c == 1 : c + 1 == f->n) {
                break;
            }
            b += f->s;
            c = f->shape == S_COUNT ? c - 1 : c + 1;
        }
        done += f->turn;
        k++;
    }
    if (k) {
        r[f->b] = b;
        r[f->x] = x;
        if (f->shape == S_END) {
            uint32_t res = b - c;
            set_nzcv(res, b >= c, ((b ^ c) & (b ^ res)) >> 31);
            set_nz(x & f->m);
        } else if (f->shape == S_COUNT) {
            uint32_t was = c + 1;
            r[f->c] = c;
            set_nzcv(c, was >= 1, ((was ^ 1) & (was ^ c)) >> 31);
        } else {
            uint32_t res = c - f->n;
            r[f->c] = c;
            set_nzcv(res, c >= f->n, ((c ^ f->n) & (c ^ res)) >> 31);
        }
    }
    return done;
}

/* a case of the dispatch for each value of bits 20-27, so that step is built for each with those bits known */
#define STEP1(n) case n: done += step((op & 0x000FFFFFu) | (uint32_t)(n) << 20, pc, &branched); break;
#define STEP4(n) STEP1(n) STEP1(n + 1) STEP1(n + 2) STEP1(n + 3)
#define STEP16(n) STEP4(n) STEP4(n + 4) STEP4(n + 8) STEP4(n + 12)
#define STEP64(n) STEP16(n) STEP16(n + 16) STEP16(n + 32) STEP16(n + 48)
#define STEP_HOT STEP1(0x1A) STEP1(0x00) STEP1(0x92) STEP1(0x8B) STEP1(0x79) STEP1(0x04) STEP1(0x03) \
    STEP1(0x48)                     /* 91% of the sound driver's outside its FIQ */

#include "arm7_fiq.h"

/* on to the instruction at `to`: straight to its fetch, as nothing here can raise an event */
#define NEXT(n, to) { done += (n); pc = (to); r[15] = pc; if (direct && done < cycles) goto fetch; continue; }

typedef struct {
    uint32_t op;
    const void* go;
} DEC8;                             /* MVC2_ARMDEC8: the decode table at half the size, the fields from the word */
static DEC8 dec8[DEC_N];
static int dec8_on;

static void dec_drop(uint32_t i) {
    dec_live[i >> 5] &= ~(1u << (i & 31));
    dec[i].go = NULL;
    dec8[i].go = NULL;
    code_gen++;
}

static void dec_clear(void) {
    memset(dec, 0, sizeof dec);
    memset(dec8, 0, sizeof dec8);
    memset(dec_live, 0, sizeof dec_live);
    code_gen++;
}

void arm7_dec8(int on) {
    dec8_on = on;
}

static int fast_index(uint32_t pc) {
    int i;
    for (i = 0; i < fasts - 1 && fast[i].pc != pc; i++) {
    }
    return i;
}

#define RUN arm7_run16
#define TABLE dec
#define DEC_T DEC
#define WIDE 1
#define RD e->rd
#define RN e->rn
#define RM e->rm
#define COND e->cond
#define FAST_I e->rm
#define V_B ((uint32_t)e->v)
#define V_OFF ((uint32_t)e->v)
#define V_IMM ((uint32_t)e->v)
#include "arm7_run.h"
#undef RUN
#undef TABLE
#undef DEC_T
#undef WIDE
#undef RD
#undef RN
#undef RM
#undef COND
#undef FAST_I
#undef V_B
#undef V_OFF
#undef V_IMM

#define RUN arm7_run8
#define TABLE dec8
#define DEC_T DEC8
#define WIDE 0
#define RD ((op >> 12) & 15)
#define RN ((op >> 16) & 15)
#define RM (op & 15)
#define COND (op >> 28)
#define FAST_I fast_index(pc)
#define V_B ((uint32_t)((int32_t)(op << 8) >> 6))
#define V_OFF (op & (1u << 23) ? op & 0xFFFu : 0u - (op & 0xFFFu))
#define V_IMM imm_of(op)
#include "arm7_run.h"

/* runs about `cycles`, or until arm7_stop; the FIQ is taken between instructions */
int arm7_run(int cycles) {
    return dec8_on ? arm7_run8(cycles) : arm7_run16(cycles);
}
