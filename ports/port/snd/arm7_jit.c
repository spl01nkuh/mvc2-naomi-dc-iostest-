/*
 * arm7_jit.c  The sound CPU's code translated a block at a time into the host's own ARM code (ARM hosts only).
 * A block runs exactly as the interpreter would: the same cycles, stopping where an event would stop it.
 */

#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "arm7_jit.h"
#include "prof.h"

#ifdef MVC2_PROF
ARM7_JIT_STATS arm7_jit_stats;
#endif

/* in a block: r11 the guest's registers, r10 the cycles done, r9 the sound RAM, r8 its mask without the low two
   bits, r7 its mask, r5 the guest's flags when they are not in the host's; r4 and r6 survive calls; the cycles to
   run are at [sp] */
enum { R_BASE = 11, R_DONE = 10, R_RAM = 9, R_MASKW = 8, R_MASK = 7, R_FLAGS = 5 };
enum { AL = 14 };

#define MAX_INSNS 32
#define MAX_POOL 64
#define BLOCK_ROOM 1536             /* host words a block may take, with its pool and the guest's words */
#define MAX_BLOCKS 4096
#define MAX_LINKS 16384

typedef struct {
    uint32_t pc, n, epoch;
    int budget;                     /* the most cycles the instructions before the last can take */
    int gate;                       /* the budget while it may run as it is, else more than any */
    const uint32_t* words;          /* the guest's words it was made from */
    const uint32_t* code;
} BLOCK;

static ARM7_JIT_STATE* st;
static uint32_t *code_start, *code_end, *code_free;
static uint32_t *enter_code, *leave_code;
static uint32_t *next_code, *next_checked_code;     /* on to the block at r0, if it can run; else out */
static uint32_t* link_code;                         /* next_code, the block kept at r2 for the next time */
static uint32_t* linked_code;                       /* the block last at the exit whose slot is at r2, if it can run */
static uint32_t *stub_ldr, *stub_ldrb, *stub_str, *stub_strb;  /* the address in r4, the value in r3 */
static BLOCK blocks[MAX_BLOCKS];
static int nblocks;
static BLOCK* map[ARM7_JIT_WORDS];
static BLOCK no_block = { .gate = INT_MAX };    /* where the first instruction is better interpreted */
static BLOCK* links[MAX_LINKS];     /* each exit's block last time */
static int nlinks;
static BLOCK* no_link;
static uint32_t covered[ARM7_JIT_WORDS / 32];   /* the words blocks were made from */
static uint32_t epoch = 1;
uint32_t arm7_jit_range[2];         /* the guest's bytes some block was made from: the lowest, and how many */

/* ---- the host's instructions ---- */

static uint32_t* p;                 /* where the next one goes */
static uint32_t pool[MAX_POOL];
static uint32_t* pool_use[MAX_POOL * 4];
static int pool_n, pool_uses, pool_full;

static void emit(uint32_t w) {
    *p++ = w;
}

/* the 12-bit operand of a data processing immediate, or -1 */
static int imm12(uint32_t v) {
    int rot;
    for (rot = 0; rot < 32; rot += 2) {
        uint32_t x = rot ? v << rot | v >> (32 - rot) : v;
        if (x < 256) {
            return (rot / 2) << 8 | (int)x;
        }
    }
    return -1;
}

static void dp_imm(int cond, int opc, int s, int rd, int rn, int enc) {
    emit((uint32_t)cond << 28 | 1u << 25 | (uint32_t)opc << 21 | (uint32_t)s << 20 | (uint32_t)rn << 16 |
         (uint32_t)rd << 12 | (uint32_t)enc);
}

/* rd = rn op rm, shifted by an immediate (type 0-3, amount 0-31) */
static void dp_reg(int cond, int opc, int s, int rd, int rn, int rm, int type, int amount) {
    emit((uint32_t)cond << 28 | (uint32_t)opc << 21 | (uint32_t)s << 20 | (uint32_t)rn << 16 | (uint32_t)rd << 12 |
         (uint32_t)amount << 7 | (uint32_t)type << 5 | (uint32_t)rm);
}

enum { AND = 0, EOR = 1, SUB = 2, ADD = 4, TST = 8, CMP = 10, ORR = 12, MOV = 13, BIC = 14 };

static void mov(int rd, int rm) {
    dp_reg(AL, MOV, 0, rd, 0, rm, 0, 0);
}

/* a word from the block's pool */
static void ldr_pool(int rd, uint32_t v) {
    int i;
    for (i = 0; i < pool_n && pool[i] != v; i++) {
    }
    if (i == pool_n) {
        if (pool_n == MAX_POOL) {
            pool_full = 1;
            i = 0;
        } else {
            pool[pool_n++] = v;
        }
    }
    if (pool_uses == MAX_POOL * 4) {
        pool_full = 1;
    } else {
        pool_use[pool_uses++] = p;
    }
    emit(0xE59F0000u | (uint32_t)rd << 12 | (uint32_t)i);     /* the index, until the pool's place is known */
}

static void const_to(int rd, uint32_t v) {
    int e = imm12(v);
    if (e >= 0) {
        dp_imm(AL, MOV, 0, rd, 0, e);
    } else if ((e = imm12(~v)) >= 0) {
        dp_imm(AL, 15, 0, rd, 0, e);                    /* MVN */
    } else {
        ldr_pool(rd, v);
    }
}

/* the pool after the code, each load pointed at its word */
static void place_pool(void) {
    int i;
    uint32_t* at = p;
    for (i = 0; i < pool_n; i++) {
        emit(pool[i]);
    }
    for (i = 0; i < pool_uses; i++) {
        uint32_t* u = pool_use[i];
        uint32_t off = (uint32_t)((at + (*u & 0xFFF)) - (u + 2)) * 4;
        *u = (*u & ~0xFFFu) | off;
    }
}

static void ldr_imm(int cond, int rd, int rn, int off) {      /* off: 0-4095 */
    emit((uint32_t)cond << 28 | 0x05900000u | (uint32_t)rn << 16 | (uint32_t)rd << 12 | (uint32_t)off);
}

static void str_imm(int cond, int rd, int rn, int off) {
    emit((uint32_t)cond << 28 | 0x05800000u | (uint32_t)rn << 16 | (uint32_t)rd << 12 | (uint32_t)off);
}

static int pend;                    /* cycles not yet added to r10: added before it is read and where paths part */

static void add_pending(void) {
    while (pend > 0) {
        int k = pend > 255 ? 255 : pend;
        emit(0xE2800000u | (uint32_t)R_DONE << 16 | (uint32_t)R_DONE << 12 | (uint32_t)k);    /* add r10, r10, #k */
        pend -= k;
    }
}

static void branch_to(int cond, int link, const uint32_t* to) {
    int32_t off;
    if (!link) {
        add_pending();
    }
    off = (int32_t)(to - (p + 2));
    emit((uint32_t)cond << 28 | (link ? 0x0B000000u : 0x0A000000u) | ((uint32_t)off & 0xFFFFFFu));
}

/* a branch whose place is filled in later */
static uint32_t* branch_later(int cond) {
    uint32_t* at;
    add_pending();
    at = p;
    emit((uint32_t)cond << 28 | 0x0A000000u);
    return at;
}

static void land(uint32_t* b) {
    int32_t off;
    add_pending();
    off = (int32_t)(p - (b + 2));
    *b = (*b & 0xFF000000u) | ((uint32_t)off & 0xFFFFFFu);
}

static void jump(const void* fn) {
    int32_t off = (int32_t)(((intptr_t)fn - (intptr_t)(p + 2)) >> 2);
    if (off >= -0x800000 && off < 0x800000) {
        branch_to(AL, 0, (const uint32_t*)fn);
    } else {
        ldr_pool(12, (uint32_t)(uintptr_t)fn);
        emit(0xE12FFF1Cu);                              /* bx r12 */
    }
}

static void call(const void* fn) {
    int32_t off = (int32_t)(((intptr_t)fn - (intptr_t)(p + 2)) >> 2);
    if (off >= -0x800000 && off < 0x800000) {
        branch_to(AL, 1, (const uint32_t*)fn);
    } else {
        ldr_pool(12, (uint32_t)(uintptr_t)fn);
        emit(0xE12FFF3Cu);                              /* BLX r12 */
    }
}

/* ---- the guest's flags: in the host's CPSR, in r5, or both ---- */

static int in_cpsr, in_reg;

static void flags_to_cpsr(void) {
    if (!in_cpsr) {
        emit(0xE128F000u | R_FLAGS);                    /* MSR CPSR_f, r5 */
        in_cpsr = 1;
    }
}

static void flags_to_reg(void) {
    if (!in_reg) {
        emit(0xE10F0000u | R_FLAGS << 12);              /* MRS r5, CPSR */
        in_reg = 1;
    }
}

/* the host's flags about to be lost */
static void flags_spill(void) {
    flags_to_reg();
    in_cpsr = 0;
}

/* r5 into the guest's CPSR, its other bits kept */
static void flags_store(void) {
    flags_to_reg();
    ldr_pool(12, (uint32_t)(uintptr_t)st->cpsr);
    ldr_imm(AL, 1, 12, 0);
    dp_imm(AL, BIC, 0, 1, 1, imm12(0xF0000000u));                    /* bic r1, r1, #0xF0000000 */
    dp_imm(AL, AND, 0, 2, R_FLAGS, imm12(0xF0000000u));
    dp_reg(AL, ORR, 0, 1, 1, 2, 0, 0);
    str_imm(AL, 1, 12, 0);
}

static void flags_load(void) {
    ldr_pool(12, (uint32_t)(uintptr_t)st->cpsr);
    ldr_imm(AL, R_FLAGS, 12, 0);
    in_reg = 1;
    in_cpsr = 0;
}

/* ---- the guest's registers ---- */

static void guest_to(int h, int g, uint32_t pc_value) {
    if (g == 15) {
        const_to(h, pc_value);
    } else {
        ldr_imm(AL, h, R_BASE, g * 4);
    }
}

static void guest_from(int cond, int h, int g) {
    str_imm(cond, h, R_BASE, g * 4);
}

static void add_done(int n) {
    pend += n;
}

/* on with the next PC in r0 */
static void leave_at(uint32_t pc) {
    flags_to_reg();
    add_pending();
    if (pc < ARM7_JIT_WORDS * 4 && nlinks < MAX_LINKS) {
        BLOCK** slot = &links[nlinks++];
        *slot = &no_block;
        ldr_pool(2, (uint32_t)(uintptr_t)slot);
        const_to(0, pc);
        branch_to(AL, 0, linked_code);
        return;
    }
    const_to(0, pc);
    branch_to(AL, 0, next_code);
}

/* after a call that may have touched the AICA: out before the next instruction if it raised an event */
static void check_event(uint32_t next) {
    uint32_t* b;
    ldr_pool(12, (uint32_t)(uintptr_t)st->event);
    ldr_imm(AL, 12, 12, 0);
    dp_imm(AL, CMP, 1, 0, 12, 0);
    b = branch_later(0);                                /* beq */
    const_to(0, next);
    branch_to(AL, 0, leave_code);
    land(b);
}

/* ---- the instructions ---- */

/* cycles an instruction can take, at most */
/* the most cycles the interpreter gives an instruction */
static int max_cycles(uint32_t op) {
    int rd = (op >> 12) & 15;
    switch ((op >> 25) & 7) {
    case 0:
        if ((op & 0x0FC000F0u) == 0x00000090u) {
            return 3;                                   /* MUL, MLA */
        }
        if ((op & 0x0F8000F0u) == 0x00800090u || (op & 0x0FB00FF0u) == 0x01000090u) {
            return 4;                                   /* the long multiplies, SWP */
        }
        if ((op & 0x90u) == 0x90u || (op & 0x0FFFFFF0u) == 0x012FFF10u) {
            return 3;                                   /* the halfword transfers, BX */
        }
        /* fall through */
    case 1:
        if ((op & 0x01900000u) == 0x01000000u) {
            return 1;                                   /* MRS, MSR */
        }
        return rd == 15 ? 3 : 1;
    case 2:
    case 3:
        if ((op >> 25 & 1) && (op & 0x10)) {
            return 3;
        }
        return !(op & (1u << 20)) ? 2 : rd == 15 ? 5 : 3;
    case 4: {
        uint32_t list = op & 0xFFFF, n = 0;
        for (; list; list &= list - 1) {
            n++;
        }
        return (int)n + 4;
    }
    default:
        return 3;
    }
}

static int uses_carry(uint32_t op) {
    int opc = (op >> 21) & 15;
    if (opc >= 5 && opc <= 7) {
        return 1;                                       /* ADC, SBC, RSC */
    }
    return !(op & (1u << 25)) && (op & 0xFF0) == 0x060;  /* RRX */
}

static void data_processing(uint32_t op, uint32_t pc) {
    int cond = op >> 28, opc = (op >> 21) & 15, s = op >> 20 & 1, imm = op >> 25 & 1;
    int rn = (op >> 16) & 15, rd = (op >> 12) & 15, rm = op & 15, rs = (op >> 8) & 15;
    int regshift = !imm && (op & 0x10), test = opc >= 8 && opc <= 11;
    uint32_t host;
    if (opc != MOV && opc != 15) {
        guest_to(0, rn, pc + (regshift ? 12 : 8));
    }
    if (!imm) {
        guest_to(1, rm, pc + (regshift ? 12 : 8));
        if (regshift) {
            guest_to(2, rs, pc + 8);
        }
    }
    uint32_t* skip = NULL;
    if (cond != AL || s || uses_carry(op)) {
        flags_to_cpsr();
    }
    if (cond != AL && s && !test) {
        skip = branch_later(cond ^ 1);                  /* the result's store must not see the new flags */
        cond = AL;
    }
    host = (uint32_t)cond << 28 | (op & 0x03F00000u) | 0u << 16 | (test ? 0u : 3u << 12);
    if (imm) {
        host |= op & 0xFFF;
    } else if (regshift) {
        host |= (op & 0xF0) | 2u << 8 | 1u;
    } else {
        host |= (op & 0xFF0) | 1u;
    }
    emit(host);
    if (s) {
        in_reg = 0;
    }
    if (!test) {
        guest_from(cond, 3, rd);
    }
    if (skip) {
        land(skip);
    }
    add_done(1);
}

/* a data processing instruction to the PC without S: on to the block there, else on in this one */
static void pc_write(uint32_t op, uint32_t pc) {
    uint32_t* skip = NULL;
    if (op >> 28 != AL) {
        flags_to_reg();
        flags_to_cpsr();
        add_done(1);
        skip = branch_later((int)(op >> 28) ^ 1);
        add_done(1);
    } else {
        add_done(2);
    }
    data_processing((op & 0x0FFFFFFFu) | (uint32_t)AL << 28, pc);
    dp_imm(AL, BIC, 0, 0, 3, 3);
    str_imm(AL, 0, R_BASE, 15 * 4);
    flags_to_reg();
    branch_to(AL, 0, next_code);
    if (skip) {
        land(skip);
    }
}

static void multiply(uint32_t op) {
    int cond = op >> 28, a = op >> 21 & 1, s = op >> 20 & 1;
    int rd = (op >> 16) & 15, rn = (op >> 12) & 15, rs = (op >> 8) & 15, rm = op & 15;
    ldr_imm(AL, 1, R_BASE, rm * 4);
    ldr_imm(AL, 2, R_BASE, rs * 4);
    if (a) {
        ldr_imm(AL, 0, R_BASE, rn * 4);
    }
    uint32_t* skip = NULL;
    if (cond != AL || s) {
        flags_to_cpsr();
    }
    add_done(1);
    if (cond != AL) {
        skip = branch_later(cond ^ 1);
    }
    emit(0xE0000090u | (uint32_t)a << 21 | (uint32_t)s << 20 | 3u << 16 | 0u << 12 | 2u << 8 | 1u);
    if (s) {
        in_reg = 0;
    }
    guest_from(AL, 3, rd);
    add_done(2);
    if (skip) {
        land(skip);
    }
}

/* any other instruction, through the interpreter; its flags come back, and its cycles in r0 */
static void interpret(uint32_t op, uint32_t pc) {
    flags_store();
    const_to(0, op);
    const_to(1, pc);
    call((const void*)arm7_jit_step);
    flags_load();
}

static void through_interpreter(uint32_t op, uint32_t pc) {
    interpret(op, pc);
    dp_reg(AL, ADD, 0, R_DONE, R_DONE, 0, 0, 0);
}

/* LDR, LDRB, STR, STRB; not a load to the PC, nor a write back to it. The RAM directly, the rest through stubs */
static void single_transfer(uint32_t op, uint32_t pc) {
    int cond = op >> 28, reg = op >> 25 & 1, pre = op >> 24 & 1, up = op >> 23 & 1, byte = op >> 22 & 1;
    int wb = !pre || (op >> 21 & 1), load = op >> 20 & 1;
    int rn = (op >> 16) & 15, rd = (op >> 12) & 15, rm = op & 15, a = wb ? 6 : 4;
    uint32_t off = op & 0xFFF, *skip = NULL;
    if (cond != AL) {
        flags_to_reg();
        flags_to_cpsr();
        add_done(1);
        skip = branch_later(cond ^ 1);
        add_done((load ? 3 : 2) - 1);
    } else {
        add_done(load ? 3 : 2);
    }
    /* r0 the base, r4 the address, r6 the base after; r3 the value stored */
    guest_to(0, rn, pc + 8);
    if (reg) {
        guest_to(1, rm, pc + 8);
        if ((op & 0xFF0) == 0x060) {
            flags_to_cpsr();                            /* RRX */
        }
        dp_reg(AL, up ? ADD : SUB, 0, a, 0, 1, (op >> 5) & 3, (op >> 7) & 31);
    } else if (off == 0) {
        mov(a, 0);
    } else if (imm12(off) >= 0) {
        dp_imm(AL, up ? ADD : SUB, 0, a, 0, imm12(off));
    } else {
        const_to(1, off);
        dp_reg(AL, up ? ADD : SUB, 0, a, 0, 1, 0, 0);
    }
    if (wb) {
        mov(4, pre ? 6 : 0);
    }
    if (!load) {
        guest_to(3, rd, pc + 12);
    }
    flags_spill();
    dp_reg(AL, MOV, 1, 1, 0, 4, 1, 23);                 /* movs r1, r4, lsr #23: not the RAM */
    if (load) {
        dp_reg(0, AND, 0, 0, 4, byte ? R_MASK : R_MASKW, 0, 0);
        emit((byte ? 0x07D00000u : 0x07900000u) | (uint32_t)R_RAM << 16);     /* ldr(b)eq r0, [r9, r0] */
        if (!byte) {
            dp_imm(0, AND, 0, 1, 4, 3);
            dp_reg(0, MOV, 0, 1, 0, 1, 0, 3);
            emit(0x01A00170u);                          /* moveq r0, r0, ror r1 */
        }
        branch_to(1, 1, byte ? stub_ldrb : stub_ldr);   /* blne */
        if (wb) {
            guest_from(AL, 6, rn);
        }
        guest_from(AL, 0, rd);
    } else {
        uint32_t *slow, *done_b, *ok;
        slow = branch_later(1);
        dp_reg(AL, AND, 0, 0, 4, byte ? R_MASK : R_MASKW, 0, 0);
        ldr_pool(12, (uint32_t)(uintptr_t)arm7_jit_range);
        emit(0xE89C0006u);                              /* ldm r12, {r1, r2} */
        dp_reg(AL, SUB, 0, 1, 0, 1, 0, 0);
        dp_reg(AL, CMP, 1, 0, 1, 2, 0, 0);
        emit((byte ? 0x27C00000u : 0x27800000u) | (uint32_t)R_RAM << 16 | 3u << 12);  /* str(b)hs r3, [r9, r0] */
        if (wb) {
            guest_from(2, 6, rn);
        }
        done_b = branch_later(2);                       /* bhs: not over translated code */
        land(slow);
        branch_to(AL, 1, byte ? stub_strb : stub_str);  /* the flags: ne if it raised an event */
        if (wb) {
            guest_from(AL, 6, rn);
        }
        ok = branch_later(0);
        const_to(0, pc + 4);
        branch_to(AL, 0, leave_code);
        land(ok);
        land(done_b);
    }
    if (skip) {
        land(skip);
    }
}

/* LDM and STM without the S bit, the base not the PC: whole in the RAM directly, else through the interpreter */
static void block_transfer(uint32_t op, uint32_t pc) {
    int cond = op >> 28, pre = op >> 24 & 1, up = op >> 23 & 1, wb = op >> 21 & 1, load = op >> 20 & 1;
    int rn = (op >> 16) & 15, list = op & 0xFFFF, n = 0, i, k, low;
    uint32_t *skip = NULL, *slow, *hit = NULL, *done_b;
    for (i = 0; i < 16; i++) {
        n += list >> i & 1;
    }
    k = n + 2 + (load && (list & 0x8000) ? 2 : 0);
    if (cond != AL) {
        flags_to_reg();
        flags_to_cpsr();
        add_done(1);
        skip = branch_later(cond ^ 1);
        add_done(k - 1);
    } else {
        add_done(k);
    }
    /* r4 the lowest address, r6 the base after */
    ldr_imm(AL, 0, R_BASE, rn * 4);
    dp_imm(AL, up ? ADD : SUB, 0, 6, 0, imm12((uint32_t)n * 4));
    low = up ? (pre ? 4 : 0) : -n * 4 + (pre ? 0 : 4);
    if (low > 0) {
        dp_imm(AL, ADD, 0, 4, 0, imm12((uint32_t)low));
    } else if (low < 0) {
        dp_imm(AL, SUB, 0, 4, 0, imm12((uint32_t)-low));
    } else {
        mov(4, 0);
    }
    flags_spill();
    dp_imm(AL, ADD, 0, 1, 4, imm12((uint32_t)(n - 1) * 4));
    dp_reg(AL, ORR, 0, 1, 1, 4, 0, 0);
    dp_reg(AL, BIC, 1, 1, 1, R_MASK, 0, 0);             /* bics r1, r1, r7: past the RAM, or a mirror of it */
    slow = branch_later(1);
    dp_imm(AL, BIC, 0, 2, 4, 3);
    if (!load) {
        ldr_pool(12, (uint32_t)(uintptr_t)arm7_jit_range);
        emit(0xE89C0003u);                              /* ldm r12, {r0, r1} */
        dp_imm(AL, ADD, 0, 3, 2, imm12((uint32_t)n * 4 - 1));
        dp_reg(AL, SUB, 0, 3, 3, 0, 0, 0);
        dp_imm(AL, ADD, 0, 1, 1, imm12((uint32_t)n * 4 - 1));
        dp_reg(AL, CMP, 1, 0, 3, 1, 0, 0);
        hit = branch_later(3);                          /* blo: over translated code */
    }
    dp_reg(AL, ADD, 0, 2, 2, R_RAM, 0, 0);
    if (load && wb) {
        guest_from(AL, 6, rn);
    }
    for (i = 0, k = 0; i < 16; i++) {
        if (list >> i & 1) {
            if (load) {
                ldr_imm(AL, 0, 2, k * 4);
                if (i == 15) {
                    dp_imm(AL, BIC, 0, 0, 0, 3);
                }
                guest_from(AL, 0, i);
            } else {
                guest_to(0, i, pc + 12);
                str_imm(AL, 0, 2, k * 4);
            }
            k++;
        }
    }
    if (!load && wb) {
        guest_from(AL, 6, rn);
    }
    done_b = branch_later(AL);
    land(slow);
    if (hit) {
        land(hit);
    }
    interpret(op, pc);                                  /* its cycles are counted already */
    if (!load) {
        check_event(pc + 4);
    }
    land(done_b);
    if (load && (list & 0x8000)) {
        ldr_imm(AL, 0, R_BASE, 15 * 4);
        branch_to(AL, 0, next_code);
    }
    if (skip) {
        land(skip);
    }
}

/* MRS from the CPSR */
static void status_read(uint32_t op) {
    int cond = op >> 28, rd = (op >> 12) & 15;
    uint32_t* skip = NULL;
    if (cond != AL) {
        flags_to_reg();
        flags_to_cpsr();
        skip = branch_later(cond ^ 1);
    }
    flags_store();                                      /* leaves the whole CPSR in r1 */
    guest_from(AL, 1, rd);
    if (skip) {
        land(skip);
    }
    add_done(1);
}

/* the block being made: where it starts, and its loops back to the start, whose budget is filled in at the end */
static uint32_t block_pc;
static uint32_t* block_code;
static uint32_t* loop_cmp[MAX_INSNS];
static int loops;
static uint32_t* seg_cmp;                            /* the test before the part after a branch not taken */
static int seg_budget, first_budget;

static uint32_t cmp_budget(int v) {
    while (imm12((uint32_t)v) < 0) {
        v++;
    }
    return 0xE3500000u | 3u << 16 | (uint32_t)imm12((uint32_t)v);   /* cmp r3, #v */
}

/* a part of the block ends; the next runs only if the cycles allow it all, else out to the PC next */
static void part_end(uint32_t next) {
    uint32_t* ok;
    if (seg_cmp) {
        *seg_cmp = cmp_budget(seg_budget);
    } else {
        first_budget = seg_budget;
    }
    seg_budget = 0;
    if (next == 0) {
        return;
    }
    flags_spill();
    add_pending();
    ldr_imm(AL, 3, 13, 0);
    dp_reg(AL, SUB, 0, 3, 3, R_DONE, 0, 0);
    seg_cmp = p;
    emit(0);
    ok = branch_later(12);                              /* bgt */
    leave_at(next);
    land(ok);
}

/* out to the PC to, straight back to the start if it is the start and the cycles allow */
static void exit_to(uint32_t to) {
    flags_to_reg();
    add_pending();
    if (to == block_pc && loops < MAX_INSNS) {
        ldr_imm(AL, 3, 13, 0);
        dp_reg(AL, SUB, 0, 3, 3, R_DONE, 0, 0);
        loop_cmp[loops++] = p;
        emit(0);                                        /* cmp r3, #budget */
        branch_to(12, 0, block_code);                   /* bgt */
    }
    leave_at(to);
}

/* B and BL; a branch not taken goes on in the block */
static void branch(uint32_t op, uint32_t pc) {
    int cond = op >> 28;
    uint32_t to = pc + 8 + (uint32_t)((int32_t)(op << 8) >> 6), *not_taken = NULL;
    if (cond != AL) {
        flags_to_reg();
        flags_to_cpsr();
        add_done(1);
        not_taken = branch_later(cond ^ 1);
        add_done(2);
    } else {
        add_done(3);
    }
    if (op & (1u << 24)) {
        const_to(0, pc + 4);
        guest_from(AL, 0, 14);
    }
    exit_to(to);
    if (not_taken) {
        land(not_taken);
    }
}

enum { T_NOP, T_DP, T_MUL, T_ST, T_BT, T_MRS, T_MID, T_END, T_B, T_PC };

/* how an instruction is translated: T_MID goes through the interpreter and on, T_END through it and out */
static int classify(uint32_t op) {
    int rn = (op >> 16) & 15, rd = (op >> 12) & 15, rs = (op >> 8) & 15, rm = op & 15;
    if (op >> 28 == 15) {
        return T_NOP;
    }
    switch ((op >> 25) & 7) {
    case 0:
        if ((op & 0x0FC000F0u) == 0x00000090u) {
            return rd == 15 || rn == 15 || rs == 15 || rm == 15 ? T_END : T_MUL;
        }
        if ((op & 0x0F8000F0u) == 0x00800090u) {
            return rd == 15 || rn == 15 || rs == 15 || rm == 15 ? T_END : T_MID;
        }
        if ((op & 0x0FB00FF0u) == 0x01000090u) {
            return rd == 15 || rn == 15 || rm == 15 ? T_END : T_MID;            /* SWP */
        }
        if ((op & 0x90u) == 0x90u) {
            return rn == 15 ? T_END : T_MID;                                    /* the halfword transfers */
        }
        if ((op & 0x01900000u) == 0x01000000u) {
            if ((op & 0x0FFF0FFFu) == 0x010F0000u && rd != 15) {
                return T_MRS;
            }
            return !(op & (1u << 21)) && (op & 0x0FBF0FFFu) == 0x010F0000u && rd != 15 ? T_MID : T_END;
        }
        return rd != 15 ? T_DP : op & (1u << 20) ? T_END : T_PC;
    case 1:
        if ((op & 0x01900000u) == 0x01000000u) {
            return T_END;                               /* MSR */
        }
        return rd != 15 ? T_DP : op & (1u << 20) ? T_END : T_PC;
    case 3:
        if (op & 0x10) {
            return T_END;
        }
        /* fall through */
    case 2: {
        int load = op >> 20 & 1, wb = !(op >> 24 & 1) || (op >> 21 & 1);
        if ((load && rd == 15) || (wb && rn == 15)) {
            return T_END;
        }
        return T_ST;
    }
    case 4:
        if ((op & (1u << 22)) || rn == 15) {
            return T_END;
        }
        return (op & 0xFFFF) ? T_BT : T_MID;
    case 5:
        return T_B;
    default:
        return T_END;
    }
}

static BLOCK* translate(uint32_t pc) {
    const uint8_t* ram = *st->ram;
    BLOCK* b;
    uint32_t start = pc, *words, n = 0;
    int last = 0;
#ifdef MVC2_PROF
    uint32_t t0 = prof_now();
#endif
    if (nblocks == MAX_BLOCKS || code_end - code_free < BLOCK_ROOM) {
        arm7_jit_reset();
        if (code_end - code_free < BLOCK_ROOM) {
            return &no_block;
        }
    }
    p = code_free;
    pool_n = pool_uses = pool_full = 0;
    in_reg = 1;
    in_cpsr = 0;
    pend = 0;
    block_pc = pc;
    block_code = p;
    loops = 0;
    seg_cmp = NULL;
    seg_budget = 0;
    for (;;) {
        uint32_t op;
        int t;
        memcpy(&op, ram + pc, 4);
        t = classify(op);
        if (n > 0 && (n == MAX_INSNS || p - code_free > 700 || pool_n > MAX_POOL - 8)) {
            leave_at(pc);
            break;
        }
        seg_budget += last;
        last = max_cycles(op);
        n++;
        switch (t) {
        case T_NOP: add_done(1); break;
        case T_DP: data_processing(op, pc); break;
        case T_MUL: multiply(op); break;
        case T_ST: single_transfer(op, pc); break;
        case T_BT: block_transfer(op, pc); break;
        case T_MRS: status_read(op); break;
        case T_PC: pc_write(op, pc); break;
        case T_MID:
            through_interpreter(op, pc);
            check_event(pc + 4);
            break;
        case T_END:
            through_interpreter(op, pc);
            ldr_imm(AL, 0, R_BASE, 15 * 4);
            branch_to(AL, 0, next_checked_code);
            break;
        default:
            branch(op, pc);
            if (op >> 28 != AL) {
                part_end(pc + 4);
                last = 0;
            }
            break;
        }
        pc += 4;
        if (t == T_END || (t == T_PC && op >> 28 == AL) || (t == T_B && op >> 28 == AL) || (t == T_BT && op >> 28 == AL && (op & 0x00108000u) == 0x00108000u)) {
            break;
        }
        if (pc >= ARM7_JIT_WORDS * 4) {
            leave_at(pc);
            break;
        }
    }
    if (pool_full) {
        return &no_block;
    }
    part_end(0);
    {
        int i;
        for (i = 0; i < loops; i++) {
            *loop_cmp[i] = cmp_budget(first_budget);    /* the same test as the way in: done + budget < cycles */
        }
    }
    place_pool();
    words = p;
    memcpy(words, ram + start, n * 4);
    p += n;
    host_code_flush(code_free, (uint32_t)(p - code_free) * 4);
    JIT_STAT(arm7_jit_stats.flushes++);
    JIT_STAT(arm7_jit_stats.made++);
    JIT_STAT(arm7_jit_stats.words += (uint32_t)(p - code_free));
    JIT_STAT(arm7_jit_stats.made_ticks += prof_now() - t0);
    b = &blocks[nblocks++];
    JIT_STAT(arm7_jit_stats.held = (uint32_t)nblocks);
    b->pc = start;
    b->n = n;
    b->epoch = epoch;
    b->budget = b->gate = first_budget;
    b->words = words;
    b->code = code_free;
    code_free = p;
    for (pc = start; pc < start + n * 4; pc += 4) {
        covered[pc >> 7] |= 1u << (pc >> 2 & 31);
    }
    if (arm7_jit_range[1] == 0) {
        arm7_jit_range[0] = start;
        arm7_jit_range[1] = n * 4;
    } else {
        uint32_t lo = arm7_jit_range[0] < start ? arm7_jit_range[0] : start;
        uint32_t hi = arm7_jit_range[0] + arm7_jit_range[1];
        hi = hi > start + n * 4 ? hi : start + n * 4;
        arm7_jit_range[0] = lo;
        arm7_jit_range[1] = hi - lo;
    }
    return b;
}

/* the entry: r0 the block, r1 the cycles done, r2 the cycles to run; the exit: r0 the next PC */
static void make_entry(void) {
    uint32_t* out[5];
    int i;
    p = code_free;
    pool_n = pool_uses = pool_full = 0;
    enter_code = p;
    emit(0xE92D4FF4u);                                  /* push {r2, r4-r11, lr} */
    mov(R_DONE, 1);
    ldr_pool(R_BASE, (uint32_t)(uintptr_t)st->r);
    ldr_pool(12, (uint32_t)(uintptr_t)st->ram);
    ldr_imm(AL, R_RAM, 12, 0);
    ldr_pool(12, (uint32_t)(uintptr_t)st->ram_mask);
    ldr_imm(AL, R_MASK, 12, 0);
    dp_imm(AL, BIC, 0, R_MASKW, R_MASK, 3);
    ldr_pool(12, (uint32_t)(uintptr_t)st->cpsr);
    ldr_imm(AL, R_FLAGS, 12, 0);
    emit(0xE12FFF10u);                                  /* bx r0 */
    next_checked_code = p;                              /* after the interpreter: out if it raised an event */
    ldr_pool(12, (uint32_t)(uintptr_t)st->event);
    ldr_imm(AL, 12, 12, 0);
    dp_imm(AL, CMP, 1, 0, 12, 0);
    out[0] = branch_later(1);                           /* bne */
    next_code = p;
    ldr_pool(2, (uint32_t)(uintptr_t)&no_link);
    link_code = p;
    dp_imm(AL, CMP, 1, 0, 0, imm12(ARM7_JIT_WORDS * 4));
    out[1] = branch_later(2);                           /* bhs */
    ldr_pool(12, (uint32_t)(uintptr_t)map);
    emit(0xE79CC000u);                                  /* ldr r12, [r12, r0] */
    dp_imm(AL, CMP, 1, 0, 12, 0);
    out[2] = branch_later(0);                           /* beq */
    ldr_imm(AL, 1, 12, offsetof(BLOCK, gate));
    ldr_imm(AL, 3, 13, 0);
    dp_reg(AL, SUB, 0, 3, 3, R_DONE, 0, 0);
    dp_reg(AL, CMP, 1, 0, 3, 1, 0, 0);                  /* done + budget < cycles */
    out[3] = branch_later(13);                          /* ble */
    str_imm(AL, 12, 2, 0);
    ldr_imm(AL, 1, 12, offsetof(BLOCK, code));
    emit(0xE12FFF11u);                                  /* bx r1 */
    linked_code = p;
    ldr_imm(AL, 12, 2, 0);
    ldr_imm(AL, 1, 12, offsetof(BLOCK, gate));
    ldr_imm(AL, 3, 13, 0);
    dp_reg(AL, SUB, 0, 3, 3, R_DONE, 0, 0);
    dp_reg(AL, CMP, 1, 0, 3, 1, 0, 0);
    ldr_imm(12, 1, 12, offsetof(BLOCK, code));
    emit(0xC12FFF11u);                                  /* bxgt r1 */
    branch_to(AL, 0, link_code);
    leave_code = p;
    for (i = 0; i < 4; i++) {
        land(out[i]);
    }
    str_imm(AL, 0, R_BASE, 15 * 4);
    ldr_pool(12, (uint32_t)(uintptr_t)st->cpsr);
    ldr_imm(AL, 1, 12, 0);
    dp_imm(AL, BIC, 0, 1, 1, imm12(0xF0000000u));
    dp_imm(AL, AND, 0, 2, R_FLAGS, imm12(0xF0000000u));
    dp_reg(AL, ORR, 0, 1, 1, 2, 0, 0);
    str_imm(AL, 1, 12, 0);
    mov(0, R_DONE);
    emit(0xE8BD8FF4u);                                  /* pop {r2, r4-r11, pc} */
    stub_ldr = p;
    mov(0, 4);
    jump((const void*)arm7_jit_ldr);
    stub_ldrb = p;
    mov(0, 4);
    jump((const void*)arm7_jit_ldrb);
    stub_str = p;
    emit(0xE92D4010u);                                  /* push {r4, lr} */
    mov(0, 4);
    mov(1, 3);
    call((const void*)arm7_jit_str);
    out[0] = branch_later(AL);
    stub_strb = p;
    emit(0xE92D4010u);
    mov(0, 4);
    mov(1, 3);
    call((const void*)arm7_jit_strb);
    land(out[0]);
    ldr_pool(12, (uint32_t)(uintptr_t)st->event);
    ldr_imm(AL, 12, 12, 0);
    dp_imm(AL, CMP, 1, 0, 12, 0);
    emit(0xE8BD8010u);                                  /* pop {r4, pc} */
    place_pool();
    host_code_flush(code_free, (uint32_t)(p - code_free) * 4);
    JIT_STAT(arm7_jit_stats.flushes++);
    code_free = p;
}

int arm7_jit_init(ARM7_JIT_STATE* state) {
    uint32_t size;
    void* mem = host_code(&size);
    st = state;
    if (mem == NULL) {
        return 0;
    }
    code_start = code_free = mem;
    code_end = code_start + size / 4;
    make_entry();
    return 1;
}

/* every block let go; the code memory is used on from where it was, and from the start once full */
void arm7_jit_reset(void) {
    JIT_STAT(arm7_jit_stats.resets++);
    memset(map, 0, sizeof map);
    memset(covered, 0, sizeof covered);
    nblocks = 0;
    JIT_STAT(arm7_jit_stats.held = 0);
    nlinks = 0;
    arm7_jit_range[0] = arm7_jit_range[1] = 0;
    if (code_end - code_free < BLOCK_ROOM * 4) {
        code_free = code_start;
        make_entry();
    }
}

void arm7_jit_check(void) {
    int i;
    JIT_STAT(arm7_jit_stats.checks++);
    epoch++;
    for (i = 0; i < nblocks; i++) {
        blocks[i].gate = INT_MAX;
    }
}

#ifdef MVC2_PROF
/* the blocks (pc, instructions, code offset) and the code memory used, for a look on the PC */
void arm7_jit_dump(void (*out)(const void* p, uint32_t size)) {
    uint32_t h[4], i;
    h[0] = 0x4A495431u;
    h[1] = (uint32_t)nblocks;
    h[2] = (uint32_t)(uintptr_t)code_start;
    h[3] = (uint32_t)((code_free - code_start) * 4);
    out(h, sizeof h);
    for (i = 0; i < (uint32_t)nblocks; i++) {
        uint32_t e[3] = { blocks[i].pc, blocks[i].n, (uint32_t)(blocks[i].code - code_start) * 4 };
        out(e, sizeof e);
    }
    out(code_start, h[3]);
}
#endif

int arm7_jit_covers(uint32_t addr) {
    return addr < ARM7_JIT_WORDS * 4 && (covered[addr >> 7] >> (addr >> 2 & 31) & 1);
}

int arm7_jit_run(uint32_t pc, int done, int cycles) {
    BLOCK* b = map[pc >> 2];
    if (b == NULL) {
        b = map[pc >> 2] = translate(pc);
    }
    if (b->code == NULL) {
        return -1;
    }
    if (b->epoch != epoch) {
        JIT_STAT(arm7_jit_stats.rechecked++);
        if (memcmp(b->words, *st->ram + pc, b->n * 4) != 0) {
            JIT_STAT(arm7_jit_stats.remade++);
            b = map[pc >> 2] = translate(pc);
            if (b->code == NULL) {
                return -1;
            }
        }
        b->epoch = epoch;
        b->gate = b->budget;
    }
    if (done + b->budget >= cycles) {
        JIT_STAT(arm7_jit_stats.refused++);
        return -1;
    }
    JIT_STAT(arm7_jit_stats.entries++);
    return ((int (*)(const uint32_t*, int, int))(void*)enter_code)(b->code, done, cycles);
}
