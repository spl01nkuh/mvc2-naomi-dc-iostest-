/*
 * arm7_jit2.c  The sound CPU's hot code translated to the host's ARM code, its commonest registers and its flags
 * kept in the host's. A block runs exactly as the interpreter would; what it can't do goes through the interpreter.
 */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "arm7_jit.h"
#include "prof.h"

/* in a block: r0-r7 eight of the guest's registers, r8 its flags (in the host's too while it uses them), r9 the
   guest's registers in memory, r10 the cycles left, r11 the sound RAM; r12 and lr scratch */
enum { R_FLAGS = 8, R_ST = 9, R_LEFT = 10, R_RAM = 11, IP = 12, SP = 13, LR = 14 };
enum { AL = 14 };
enum { AND = 0, EOR = 1, SUB = 2, ADD = 4, TST = 8, CMP = 10, ORR = 12, MOV = 13, BIC = 14 };

static uint8_t kept[2][8] = { { 0, 1, 3, 8, 9, 10, 12, 13 }, { 0, 1, 3, 5, 8, 11, 12, 13 } };   /* in host r0-r7 */
static int8_t host_of[2][16];

#define HOT 8                       /* entries before a block is made */
#define MAX_INSNS 24
#define MAX_BLOCKS 2048
#define MAX_SITES 8192
#define POOL_N 64
#define QUIET 30000                 /* entries from C with no block made, before the hot ones are made again */

typedef struct {
    uint32_t pc, end;
    int budget;                     /* the most cycles the instructions before the last can take */
    const uint32_t* code;
    uint32_t count;                 /* its runs, while it counts them; after, its runs before it was made again */
    int counted, mode, n;
    uint16_t at[MAX_INSNS];         /* its instructions' words, in the order it runs them */
} BLOCK2;

typedef struct {
    uint32_t op, pc;
    int interp;                     /* its cycles are the interpreter's, not counted in the block */
    int mode;
} SITE;

typedef struct {
    uint32_t *at, *start;
    uint32_t pool[POOL_N];
    uint32_t* use[POOL_N * 4];
    int n, uses;
} AREA;

static ARM7_JIT_STATE* st;
static uint32_t *hot_start, *hot_end, *cold_start, *cold_end, *hot_free, *cold_free;
static uint32_t *warm_start, *warm_free;    /* the hot area's second half: blocks made on demand, the less run */
static uint32_t hot_min;            /* blocks made again with fewer runs go to the warm area */
static uint32_t *enter_code[2], *leave_code[2], *slow_code[2], *disp_code[2], *miss_code[2];
static uint32_t *push_code[2], *pop_code[2];   /* stmfd sp!,{pc} and ldmfd sp!,{pc}, where sp is kept */
#define SITE_PUSH 0x80000000u       /* slow's site: a push of the value in the low bits */
#define SITE_POP 0x40000000u
static BLOCK2* blocks;
static int nblocks;
static BLOCK2 none, soon;           /* soon: to be made again, in order */
static BLOCK2* (*map)[ARM7_JIT_WORDS];
static const uint32_t* (*jmap)[ARM7_JIT_WORDS];
static uint8_t (*heat)[ARM7_JIT_WORDS];
static SITE* sites;
static int nsites;
static uint32_t* covered;
uint32_t arm7_jit2_range[2];
static int ready, dirty, kinds = 15, checking;
static int counting = 1, made_since;    /* blocks made since the last compaction */
static int tracing;                 /* blocks go on through the branches they mostly take */
static uint32_t (*br)[ARM7_JIT_WORDS][2]; /* each branch's runs and times taken, in the blocks that count */
static uint32_t (*ent)[ARM7_JIT_WORDS];    /* each block's runs, at the last compaction */
static uint32_t (*into)[ARM7_JIT_WORDS / 32];  /* where blocks made again went on through a branch */
static uint32_t (*slowed)[ARM7_JIT_WORDS];     /* each load's and store's runs through the interpreter */
typedef struct {
    uint32_t pc, count;
    int mode;
} KEEP;
typedef struct {                    /* the tables, on the heap only while the translator is on */
    BLOCK2 blocks[MAX_BLOCKS];
    BLOCK2* map[2][ARM7_JIT_WORDS];
    const uint32_t* jmap[2][ARM7_JIT_WORDS];
    uint8_t heat[2][ARM7_JIT_WORDS];
    SITE sites[MAX_SITES];
    uint32_t covered[ARM7_JIT_WORDS / 32];
    uint32_t br[2][ARM7_JIT_WORDS][2];
    uint32_t ent[2][ARM7_JIT_WORDS];
    uint32_t into[2][ARM7_JIT_WORDS / 32];
    uint32_t slowed[2][ARM7_JIT_WORDS];
    uint16_t order[MAX_BLOCKS];     /* compaction's */
    KEEP keep[MAX_BLOCKS];
    uint32_t runs[MAX_BLOCKS];
} TABLES;
static TABLES* tb;
const uint32_t arm7_jit2_tables = sizeof (TABLES);
static uint32_t entry;              /* the block being made: its start */
static const BLOCK2* checked;
static uint32_t quiet;
static uint32_t* shared_start;
static uint32_t* link_site;         /* the leave code's: the exit's branch, to point at the block it reached */
static uint32_t exit_pc = ~0u;
static int exit_mode;
static uint32_t lim;                /* loads below it read the RAM directly */

/* ---- the host's instructions, into the hot or the cold area ---- */

static AREA hot, cold, *cur = &hot;
static int full;

static void emit(uint32_t w) {
    *cur->at++ = w;
}

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

static void dp_reg(int cond, int opc, int s, int rd, int rn, int rm, int type, int amount) {
    emit((uint32_t)cond << 28 | (uint32_t)opc << 21 | (uint32_t)s << 20 | (uint32_t)rn << 16 | (uint32_t)rd << 12 |
         (uint32_t)amount << 7 | (uint32_t)type << 5 | (uint32_t)rm);
}

static void mov(int rd, int rm) {
    dp_reg(AL, MOV, 0, rd, 0, rm, 0, 0);
}

static void ldr_imm(int cond, int rd, int rn, int off) {
    emit((uint32_t)cond << 28 | 0x05900000u | (uint32_t)rn << 16 | (uint32_t)rd << 12 | (uint32_t)off);
}

static void str_imm(int cond, int rd, int rn, int off) {
    emit((uint32_t)cond << 28 | 0x05800000u | (uint32_t)rn << 16 | (uint32_t)rd << 12 | (uint32_t)off);
}

/* a word from the area's pool, placed after its code */
static void lit(int cond, int rd, uint32_t v) {
    int i;
    for (i = 0; i < cur->n && cur->pool[i] != v; i++) {
    }
    if (i == cur->n) {
        if (cur->n == POOL_N) {
            full = 1;
            i = 0;
        } else {
            cur->pool[cur->n++] = v;
        }
    }
    if (cur->uses == POOL_N * 4) {
        full = 1;
    } else {
        cur->use[cur->uses++] = cur->at;
    }
    emit((uint32_t)cond << 28 | 0x059F0000u | (uint32_t)rd << 12 | (uint32_t)i);
}

static void place_pool(AREA* a) {
    int i;
    uint32_t* at = a->at;
    for (i = 0; i < a->n; i++) {
        *a->at++ = a->pool[i];
    }
    for (i = 0; i < a->uses; i++) {
        uint32_t* u = a->use[i];
        *u = (*u & ~0xFFFu) | (uint32_t)((at + (*u & 0xFFF)) - (u + 2)) * 4;
    }
    a->n = a->uses = 0;
}

static void const_to(int cond, int rd, uint32_t v) {
    int e = imm12(v);
    if (e >= 0) {
        dp_imm(cond, MOV, 0, rd, 0, e);
    } else if ((e = imm12(~v)) >= 0) {
        dp_imm(cond, 15, 0, rd, 0, e);
    } else if (v < 0x10000) {
        dp_imm(cond, MOV, 0, rd, 0, imm12(v & 0xFF00));
        dp_imm(cond, ORR, 0, rd, rd, imm12(v & 0xFF));
    } else {
        lit(cond, rd, v);
    }
}

static void b_to(int cond, const uint32_t* to) {
    emit((uint32_t)cond << 28 | 0x0A000000u | ((uint32_t)(to - (cur->at + 2)) & 0xFFFFFFu));
}

static void bl_to(const uint32_t* to) {
    emit(0xEB000000u | ((uint32_t)(to - (cur->at + 2)) & 0xFFFFFFu));
}

static void point(uint32_t* b, const uint32_t* to) {
    *b = (*b & 0xFF000000u) | ((uint32_t)(to - (b + 2)) & 0xFFFFFFu);
}

static uint32_t* b_later(int cond) {
    uint32_t* at = cur->at;
    emit((uint32_t)cond << 28 | 0x0A000000u);
    return at;
}

/* r10 -= n */
static void sub_left(int cond, int n) {
    while (n > 0) {
        int k = n > 255 ? 255 : n;
        dp_imm(cond, SUB, 0, R_LEFT, R_LEFT, k);
        n -= k;
    }
}

/* r0-r7 to and from the guest's registers in memory */
static void store_kept(int m) {
    int i;
    for (i = 0; i < 8; i++) {
        str_imm(AL, i, R_ST, kept[m][i] * 4);
    }
}

static void load_kept(int m) {
    int i;
    for (i = 0; i < 8; i++) {
        ldr_imm(AL, i, R_ST, kept[m][i] * 4);
    }
}

/* the flags in r8, or the host's, into the guest's CPSR, by r0-r2 */
static void flags_out(int from_r8) {
    if (from_r8) {
        mov(0, R_FLAGS);
    } else {
        emit(0xE10F0000u);                              /* mrs r0, cpsr */
    }
    lit(AL, 1, (uint32_t)(uintptr_t)st->cpsr);
    ldr_imm(AL, 2, 1, 0);
    dp_imm(AL, BIC, 0, 2, 2, imm12(0xF0000000u));
    dp_imm(AL, AND, 0, 0, 0, imm12(0xF0000000u));
    dp_reg(AL, ORR, 0, 2, 2, 0, 0, 0);
    str_imm(AL, 2, 1, 0);
}

static void flags_in(void) {
    lit(AL, 0, (uint32_t)(uintptr_t)st->cpsr);
    ldr_imm(AL, 0, 0, 0);
    emit(0xE128F000u);                                  /* msr cpsr_f, r0 */
}

/* ---- the code every block shares, one copy for each mode ---- */

static void make_shared(int m) {
    uint32_t* hi;
    /* enter: r0 the block's code, r1 the cycles left; gives the cycles left */
    enter_code[m] = cur->at;
    emit(0xE92D4FF8u);                                  /* push {r3-r11, lr} */
    mov(R_LEFT, 1);
    mov(IP, 0);
    lit(AL, R_ST, (uint32_t)(uintptr_t)st->r);
    lit(AL, R_RAM, (uint32_t)(uintptr_t)st->ram);
    ldr_imm(AL, R_RAM, R_RAM, 0);
    lit(AL, R_FLAGS, (uint32_t)(uintptr_t)st->cpsr);
    ldr_imm(AL, R_FLAGS, R_FLAGS, 0);
    load_kept(m);
    emit(0xE12FFF1Cu);                                  /* bx ip */
    /* leave: ip the next PC, lr the exit's branch or 0 */
    leave_code[m] = cur->at;
    str_imm(AL, IP, R_ST, 15 * 4);
    lit(AL, IP, (uint32_t)(uintptr_t)&link_site);
    str_imm(AL, LR, IP, 0);
    store_kept(m);
    flags_out(1);
    mov(0, R_LEFT);
    emit(0xE8BD8FF8u);                                  /* pop {r3-r11, pc} */
    /* slow: ip the site; back to lr, or lr + 8 if an event is due */
    slow_code[m] = cur->at;
    emit(0xE92D5000u);                                  /* push {ip, lr} */
    store_kept(m);
    flags_out(0);
    ldr_imm(AL, 0, SP, 0);
    lit(AL, IP, (uint32_t)(uintptr_t)arm7_jit2_slow);
    emit(0xE12FFF3Cu);                                  /* blx ip */
    dp_reg(AL, SUB, 0, R_LEFT, R_LEFT, 0, 0, 0);
    lit(AL, 1, (uint32_t)(uintptr_t)st->event);
    ldr_imm(AL, 1, 1, 0);
    emit(0xE8BD5000u);                                  /* pop {ip, lr} */
    dp_imm(AL, CMP, 1, 0, 1, 0);
    dp_imm(1, ADD, 0, LR, LR, 8);                       /* addne lr, lr, #8 */
    flags_in();
    load_kept(m);
    emit(0xE12FFF1Eu);                                  /* bx lr */
    /* the PC in ip: on to its block */
    disp_code[m] = cur->at;
    dp_imm(AL, CMP, 1, 0, IP, imm12(ARM7_JIT_WORDS * 4));
    hi = b_later(2);                                    /* bhs */
    lit(AL, LR, (uint32_t)(uintptr_t)jmap[m]);
    emit(0xE79EE00Cu);                                  /* ldr lr, [lr, ip] */
    emit(0xE12FFF1Eu);                                  /* bx lr */
    point(hi, cur->at);
    miss_code[m] = cur->at;                             /* each PC without a block */
    dp_imm(AL, MOV, 0, LR, 0, 0);
    b_to(AL, leave_code[m]);
    push_code[m] = pop_code[m] = NULL;
    if (host_of[m][13] >= 0) {
        int sp = host_of[m][13];
        uint32_t* slow[3];
        /* the push: the value in the word at lr; the cycles taken, the flags in r8; back after the word */
        push_code[m] = cur->at;
        emit(0xE49EC004u);                              /* ldr ip, [lr], #4 */
        emit(0xE92D4100u);                              /* push {r8, lr} */
        dp_imm(AL, SUB, 0, R_FLAGS, sp, 4);
        dp_imm(AL, CMP, 1, 0, R_FLAGS, imm12(ARM7_JIT_WORDS * 4));
        slow[0] = b_later(3);                           /* blo */
        dp_imm(AL, CMP, 1, 0, R_FLAGS, imm12(lim));
        slow[1] = b_later(2);                           /* bhs */
        dp_imm(AL, TST, 1, 0, R_FLAGS, 3);
        slow[2] = b_later(1);                           /* bne */
        emit(0xE7800000u | (uint32_t)R_RAM << 16 | (uint32_t)IP << 12 | R_FLAGS);     /* str ip, [r11, r8] */
        mov(sp, R_FLAGS);
        emit(0xE8BD8100u);                              /* pop {r8, pc} */
        point(slow[0], cur->at);
        point(slow[1], cur->at);
        point(slow[2], cur->at);
        ldr_imm(AL, R_FLAGS, SP, 0);
        emit(0xE128F000u | R_FLAGS);                    /* msr cpsr_f, r8 */
        dp_imm(AL, ORR, 0, IP, IP, imm12(SITE_PUSH));
        bl_to(slow_code[m]);
        slow[0] = b_later(AL);
        emit(0xE1A00000u);
        dp_imm(AL, BIC, 0, IP, IP, imm12(SITE_PUSH));   /* an event: out after the push */
        dp_imm(AL, SUB, 0, IP, IP, 8);
        dp_imm(AL, ADD, 0, SP, SP, 8);
        dp_imm(AL, MOV, 0, LR, 0, 0);
        b_to(AL, leave_code[m]);
        point(slow[0], cur->at);
        emit(0xE8BD8100u);
        /* the pop: the cycles taken, the flags in r8; on to the block at the PC popped */
        pop_code[m] = cur->at;
        dp_reg(AL, MOV, 0, LR, 0, sp, 3, 2);            /* mov lr, sp, ror #2 */
        dp_imm(AL, CMP, 1, 0, LR, imm12(lim >> 2));
        slow[0] = b_later(2);
        emit(0xE7900000u | (uint32_t)R_RAM << 16 | (uint32_t)IP << 12 | (uint32_t)sp);   /* ldr ip, [r11, sp] */
        dp_imm(AL, ADD, 0, sp, sp, 4);
        dp_imm(AL, BIC, 0, IP, IP, 3);
        b_to(AL, disp_code[m]);
        point(slow[0], cur->at);
        emit(0xE128F000u | R_FLAGS);
        const_to(AL, IP, SITE_POP);
        bl_to(slow_code[m]);
        slow[0] = b_later(AL);
        emit(0xE1A00000u);
        ldr_imm(AL, IP, R_ST, 15 * 4);                  /* an event */
        dp_imm(AL, MOV, 0, LR, 0, 0);
        b_to(AL, leave_code[m]);
        point(slow[0], cur->at);
        ldr_imm(AL, IP, R_ST, 15 * 4);
        dp_imm(AL, BIC, 0, IP, IP, 3);
        b_to(AL, disp_code[m]);
    }
    place_pool(cur);
}

/* ---- the guest's instructions ---- */

static int M;                       /* the block's mode: 1 FIQ */
static int pend;                    /* cycles not yet taken from r10 */
static int f_cpsr, f_reg;           /* the guest's flags right in the host's, in r8: N 8, Z 4, C 2, V 1 */
static const uint8_t cond_reads[16] = { 4, 4, 2, 2, 8, 8, 1, 1, 6, 6, 9, 9, 13, 13, 0, 0 };
static uint32_t lits[MAX_INSNS];    /* literal words the block was made from */
static int nlits;

static int host(int g) {
    return g == 15 ? -1 : host_of[M][g];
}

/* the guest's register g in a host register: its own, or loaded into s */
static int src(int g, int s, uint32_t pc_value) {
    if (g == 15) {
        const_to(AL, s, pc_value);
        return s;
    }
    if (host_of[M][g] >= 0) {
        return host_of[M][g];
    }
    ldr_imm(AL, s, R_ST, g * 4);
    return s;
}

/* the flags in `mask` that only the host's have, into r8; live: those still wanted */
static void to_reg(int mask, int live) {
    mask &= ~f_reg & 15;
    if (!mask) {
        return;
    }
    if (!(f_reg & ~f_cpsr & live)) {
        emit(0xE10F0000u | R_FLAGS << 12);              /* mrs r8, cpsr */
        f_reg = f_cpsr;
    } else {
        emit(0xE10FE000u);                              /* mrs lr, cpsr */
        dp_reg(AL, EOR, 0, LR, LR, R_FLAGS, 0, 0);
        dp_imm(AL, AND, 0, LR, LR, imm12((uint32_t)mask << 28));
        dp_reg(AL, EOR, 0, R_FLAGS, R_FLAGS, LR, 0, 0);
        f_reg |= mask;
    }
}

/* the flags in r8, as every exit wants them */
static void need_reg(void) {
    to_reg(15, 15);
}

static void need_cpsr(int mask) {
    if (mask & ~f_cpsr) {
        to_reg(f_cpsr, 15);
        emit(0xE128F000u | R_FLAGS);                    /* msr cpsr_f, r8 */
        f_cpsr = f_reg;
    }
}

/* before an instruction of the host's own that sets its flags */
static void clobber(int live) {
    to_reg(live & f_cpsr, live);
    f_reg = 15;                                         /* the rest are dead */
    f_cpsr = 0;
}

static int new_site(uint32_t op, uint32_t pc, int interp) {
    if (nsites == MAX_SITES) {
        full = 1;
        return 0;
    }
    sites[nsites].op = op;
    sites[nsites].pc = pc;
    sites[nsites].interp = interp;
    sites[nsites].mode = M;
    return nsites++;
}

/* out to C with the PC next */
static void leave_to(uint32_t pc) {
    const_to(AL, IP, pc);
    dp_imm(AL, MOV, 0, LR, 0, 0);
    b_to(AL, leave_code[M]);
}

/* a direct exit, the cycles taken: to the block there, or out through a stub that has it linked */
static void exit_to(int cond, uint32_t to) {
    BLOCK2* t = to < ARM7_JIT_WORDS * 4 ? map[M][to >> 2] : NULL;
    uint32_t* b;
    if (t != NULL && t->code != NULL && !checking) {
        b_to(cond, t->code);
        return;
    }
    b = b_later(cond);
    cur = &cold;
    point(b, cur->at);
    const_to(AL, IP, to);
    if (to < ARM7_JIT_WORDS * 4 && !checking) {
        lit(AL, LR, (uint32_t)(uintptr_t)b);
    } else {
        dp_imm(AL, MOV, 0, LR, 0, 0);
    }
    b_to(AL, leave_code[M]);
    cur = &hot;
}

/* the slow path's stub: the interpreter runs the instruction; back to `back`, or out on an event */
static void slow_stub(uint32_t* from, uint32_t op, uint32_t pc, int interp, int end, uint32_t* back) {
    int k = new_site(op, pc, interp);
    uint32_t* ev;
    int fc = f_cpsr, fr = f_reg;
    cur = &cold;
    point(from, cur->at);
    need_cpsr(15);
    f_cpsr = fc;
    f_reg = fr;
    const_to(AL, IP, (uint32_t)k);
    bl_to(slow_code[M]);
    if (end) {
        ev = b_later(AL);
        emit(0xE1A00000u);                              /* nop */
        point(ev, cur->at);
        emit(0xE10F0000u | R_FLAGS << 12);
    } else {
        if (interp) {
            emit(0xE10F0000u | R_FLAGS << 12);          /* mrs r8, cpsr */
        } else {
            emit(0xE1A00000u);
        }
        b_to(AL, back);
        if (interp) {
            emit(0xE10F0000u | R_FLAGS << 12);
        }
    }
    sub_left(AL, pend);
    if (end) {
        ldr_imm(AL, IP, R_ST, 15 * 4);
        dp_imm(AL, MOV, 0, LR, 0, 0);
        b_to(AL, leave_code[M]);
    } else {
        leave_to(pc + 4);
    }
    cur = &hot;
}

/* an instruction through the interpreter; end: then out, at the PC it leaves */
static void through(uint32_t op, uint32_t pc, int end) {
    uint32_t* b = b_later(AL);
    if (end) {
        slow_stub(b, op, pc, 1, 1, NULL);
    } else {
        slow_stub(b, op, pc, 1, 0, cur->at);
        f_cpsr = f_reg = 15;
    }
}

static int is_arith(int opc) {
    return opc >= 2 && opc <= 7 ? 1 : opc == 10 || opc == 11;
}

/* the flags an instruction reads and those it sets whatever they were (N 8, Z 4, C 2, V 1) */
static int reads(uint32_t op) {
    int t = (op >> 25) & 7, opc = (op >> 21) & 15, f = op >> 28 != AL ? 15 : 0;
    if (t <= 1 && (op & 0x01900000u) == 0x01000000u) {
        return 15;                                      /* MRS, MSR */
    }
    if (t <= 1 && !(t == 0 && (op & 0x90u) == 0x90u)) {
        if (opc >= 5 && opc <= 7) {
            f |= 2;
        }
        if (t == 0 && (op & 0xFF0u) == 0x060u) {
            f |= 2;                                     /* RRX */
        }
    }
    if ((t == 3 && (op & 0xFF0u) == 0x060u) || t >= 6 || (t == 0 && (op & 0x90u) == 0x90u)) {
        f = 15;
    }
    return f;
}

static int kills(uint32_t op) {
    int t = (op >> 25) & 7, opc = (op >> 21) & 15;
    if (op >> 28 != AL || t > 1 || !(op & (1u << 20)) || (op & 0x01900000u) == 0x01000000u) {
        return 0;
    }
    if (t == 0 && (op & 0x90u) == 0x90u) {
        return 0;
    }
    return is_arith(opc) ? 15 : 12;
}

/* the most cycles the interpreter gives an instruction */
static int max_cycles(uint32_t op) {
    int rd = (op >> 12) & 15;
    switch ((op >> 25) & 7) {
    case 0:
        if ((op & 0x0FC000F0u) == 0x00000090u) {
            return 3;
        }
        if ((op & 0x0F8000F0u) == 0x00800090u || (op & 0x0FB00FF0u) == 0x01000090u) {
            return 4;
        }
        if ((op & 0x90u) == 0x90u || (op & 0x0FFFFFF0u) == 0x012FFF10u) {
            return 3;
        }
        /* fall through */
    case 1:
        if ((op & 0x01900000u) == 0x01000000u) {
            return 1;
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

enum { T_NOP, T_DP, T_PC, T_B, T_LS, T_LSM, T_MID, T_END };

/* how an instruction is translated: T_MID through the interpreter and on, T_END through it and out */
static int classify(uint32_t op) {
    int rn = (op >> 16) & 15, rd = (op >> 12) & 15, rs = (op >> 8) & 15, rm = op & 15;
    if (op >> 28 == 15) {
        return T_NOP;
    }
    switch ((op >> 25) & 7) {
    case 0:
        if ((op & 0x0FC000F0u) == 0x00000090u) {
            return rd == 15 || rn == 15 || rs == 15 || rm == 15 ? T_END : T_MID;
        }
        if ((op & 0x0F8000F0u) == 0x00800090u) {
            return rd == 15 || rn == 15 || rs == 15 || rm == 15 ? T_END : T_MID;
        }
        if ((op & 0x0FB00FF0u) == 0x01000090u) {
            return rd == 15 || rn == 15 || rm == 15 ? T_END : T_MID;
        }
        if ((op & 0x90u) == 0x90u) {
            return rn == 15 || rd == 15 ? T_END : T_MID;
        }
        if ((op & 0x01900000u) == 0x01000000u) {
            return !(op & (1u << 21)) && rd != 15 ? T_MID : T_END;    /* MRS on, MSR and BX out */
        }
        if ((op & 0x10) && (rd == 15 || rn == 15 || rs == 15 || rm == 15)) {
            return T_END;
        }
        break;
    case 1:
        if ((op & 0x01900000u) == 0x01000000u) {
            return T_END;
        }
        break;
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
        return T_LS;
    }
    case 4:
        if ((op & (1u << 22)) || rn == 15 || !(op & 0xFFFF)) {
            return T_END;
        }
        return T_LSM;
    case 5:
        return T_B;
    default:
        return T_END;
    }
    if (rd != 15) {
        return (op & 0x0FFFFFFFu) == ((op & 0xF000u) | (op & 0xF000u) >> 12 | 0x01A00000u) ? T_NOP : T_DP;
    }
    return op & (1u << 20) ? T_END : T_PC;
}

/* a data processing instruction; to: the result's host register when the guest's is the PC */
/* whether a logical op with S sets C: a shift that has a carry out (by a register: perhaps) */
static int sets_c(uint32_t op) {
    if (op >> 25 & 1) {
        return (op & 0xF00) != 0;
    }
    return (op & 0x10) || (op & 0xFF0) != 0;
}

/* partial: a logical op with S may leave C and V in r8 alone */
static int data_processing(uint32_t op, uint32_t pc, int live, int to_pc, int partial) {
    int cond = op >> 28, opc = (op >> 21) & 15, s = op >> 20 & 1, imm = op >> 25 & 1;
    int rn = (op >> 16) & 15, rd = (op >> 12) & 15, rm = op & 15, rs = (op >> 8) & 15;
    int regshift = !imm && (op & 0x10), test = opc >= 8 && opc <= 11, hn = 0, hm = 0, hs = 0, hd;
    uint32_t pcv = pc + (regshift ? 12 : 8), w, *skip = NULL;
    int need = cond_reads[cond] | (reads(op) & 2), wrote = 0;
    if (regshift && host(rs) < 0 && opc != MOV && opc != 15 && host(rn) < 0 && host(rm) < 0) {
        return 0;                                       /* three registers from memory */
    }
    if (s) {
        wrote = is_arith(opc) ? 15 : sets_c(op) ? 14 : 12;
        if (cond != AL || (!is_arith(opc) && regshift)) {
            need |= cond != AL ? wrote : 2;             /* as they were where it doesn't run, or set nothing */
        }
        if (!is_arith(opc) && !partial && (live & 3)) {
            need = 15;
        }
    }
    need_cpsr(need);
    if (opc != MOV && opc != 15) {
        hn = src(rn, IP, pcv);
    }
    if (!imm) {
        hm = src(rm, LR, pcv);
        if (regshift) {
            hs = src(rs, hn == IP ? LR : IP, pcv);
        }
    }
    hd = to_pc ? IP : test ? 0 : host(rd) >= 0 ? host(rd) : IP;
    if (cond != AL && s && !test && host(rd) < 0) {
        skip = b_later(cond ^ 1);
        cond = AL;
    }
    w = (uint32_t)cond << 28 | (op & 0x03F00000u) | (uint32_t)hn << 16 | (uint32_t)hd << 12;
    if (imm) {
        w |= op & 0xFFF;
    } else if (regshift) {
        w |= (uint32_t)hs << 8 | (op & 0xF0) | (uint32_t)hm;
    } else {
        w |= (op & 0xFF0) | (uint32_t)hm;
    }
    emit(w);
    if (s) {
        f_cpsr |= wrote;
        f_reg &= ~wrote;
    }
    if (!test && !to_pc && host(rd) < 0) {
        str_imm(cond, IP, R_ST, rd * 4);
    }
    if (skip) {
        point(skip, cur->at);
    }
    return 1;
}

static void post_add(int h, int up, uint32_t off) {
    if (imm12(off) >= 0) {
        dp_imm(AL, up ? ADD : SUB, 0, h, h, imm12(off));
    } else {
        dp_imm(AL, up ? ADD : SUB, 0, h, h, imm12(off & 0xF00));
        dp_imm(AL, up ? ADD : SUB, 0, h, h, imm12(off & 0xFF));
    }
}

/* LDR, LDRB, STR, STRB: the RAM directly, the rest through the interpreter */
static int single_transfer(uint32_t op, uint32_t pc, int live) {
    int cond = op >> 28, reg = op >> 25 & 1, pre = op >> 24 & 1, up = op >> 23 & 1, byte = op >> 22 & 1;
    int wb = !pre || (op >> 21 & 1), load = op >> 20 & 1, k = load ? 3 : 2;
    int rn = (op >> 16) & 15, rd = (op >> 12) & 15, rm = op & 15, hb, a, hv;
    uint32_t off = op & 0xFFF, *skip = NULL, *slow, *back;
    if (!(kinds & (load ? 2 : 4)) || (reg && (rm == 15 || (op & 0xFF0) == 0x060)) || (wb && rd == rn) ||
        (wb && host(rn) < 0)) {
        return 0;
    }
    if (cond != AL) {
        live = 15;                                      /* the slow path tests the condition again */
    }
    if (load && rn == 15 && !reg && !wb) {              /* a literal: the word from the code */
        uint32_t at = pc + 8 + (up ? off : 0u - off), v;
        if (at >= ARM7_JIT_WORDS * 4 || nlits == MAX_INSNS) {
            return 0;
        }
        memcpy(&v, *st->ram + (at & ~3u), 4);
        v = byte ? v >> (at & 3) * 8 & 0xFF : (at & 3) ? v >> (at & 3) * 8 | v << (32 - (at & 3) * 8) : v;
        lits[nlits++] = at & ~3u;
        need_cpsr(cond_reads[cond]);
        hv = host(rd) >= 0 ? host(rd) : IP;
        lit(cond, hv, v);
        if (host(rd) < 0) {
            str_imm(cond, IP, R_ST, rd * 4);
        }
        pend += cond != AL ? 1 : k;
        if (cond != AL) {
            sub_left(cond, k - 1);
        }
        return 1;
    }
    if (cond == AL && tracing && slowed[M][pc >> 2] >= 64 && slowed[M][pc >> 2] >= ent[M][entry >> 2] / 4) {
        pend += k;
        slow = b_later(AL);                             /* the sound chip's registers: to the interpreter */
        slow_stub(slow, op, pc, 0, 0, cur->at);
        f_cpsr = 15;
        return 1;
    }
    if (cond != AL) {
        int fc;
        need_cpsr(cond_reads[cond]);
        fc = f_cpsr;
        clobber(live);
        f_cpsr = fc;
        skip = b_later(cond ^ 1);
        pend += 1;
        sub_left(AL, k - 1);
    } else {
        pend += k;
    }
    hb = src(rn, IP, pc + 8);
    a = hb;
    if (pre && (reg || off)) {
        if (reg) {
            int hm = src(rm, LR, 0);
            dp_reg(AL, up ? ADD : SUB, 0, IP, hb, hm, (op >> 5) & 3, (op >> 7) & 31);
        } else if (imm12(off) >= 0) {
            dp_imm(AL, up ? ADD : SUB, 0, IP, hb, imm12(off));
        } else {
            dp_imm(AL, up ? ADD : SUB, 0, IP, hb, imm12(off & 0xF00));
            dp_imm(AL, up ? ADD : SUB, 0, IP, IP, imm12(off & 0xFF));
        }
        a = IP;
    }
    clobber(live);
    if (load && byte) {
        dp_imm(AL, CMP, 1, 0, a, imm12(lim));
    } else if (load) {
        dp_reg(AL, MOV, 0, LR, 0, a, 3, 2);             /* mov lr, a, ror #2: not on a word, past the limit */
        dp_imm(AL, CMP, 1, 0, LR, imm12(lim >> 2));
    } else {
        dp_reg(AL, MOV, 1, LR, 0, a, 1, 15);            /* movs lr, a, lsr #15 */
        dp_imm(1, 3, 1, LR, LR, 0xFE);                  /* rsbnes lr, lr, #0xFE */
    }
    slow = b_later(load ? 2 : 9);                       /* bhs, or bls */
    if (load) {
        hv = host(rd) >= 0 ? host(rd) : LR;
        emit((byte ? 0xE7D00000u : 0xE7900000u) | (uint32_t)R_RAM << 16 | (uint32_t)hv << 12 | (uint32_t)a);
        if (host(rd) < 0) {
            str_imm(AL, LR, R_ST, rd * 4);
        }
        if (wb) {
            if (pre) {
                mov(host(rn), IP);
            } else {
                post_add(host(rn), up, off);
            }
        }
    } else {
        if (rd == 15) {
            const_to(AL, LR, pc + 12);
            hv = LR;
        } else {
            hv = src(rd, LR, 0);
        }
        if (wb && pre) {
            mov(host(rn), IP);
        }
        if (!byte) {
            dp_imm(AL, BIC, 0, IP, a, 3);
            a = IP;
        }
        emit((byte ? 0xE7C00000u : 0xE7800000u) | (uint32_t)R_RAM << 16 | (uint32_t)hv << 12 | (uint32_t)a);
        if (wb && !pre) {
            post_add(host(rn), up, off);
        }
    }
    back = cur->at;
    slow_stub(slow, op, pc, 0, 0, back);
    if (skip) {
        point(skip, cur->at);
    }
    f_cpsr = 0;
    f_reg = 15;
    return 1;
}

/* LDM and STM without the S bit: the RAM directly, the rest through the interpreter; a load of the PC goes on to
   its block */
static int block_transfer(uint32_t op, uint32_t pc, int live) {
    int cond = op >> 28, pre = op >> 24 & 1, up = op >> 23 & 1, wb = op >> 21 & 1, load = op >> 20 & 1;
    int rn = (op >> 16) & 15, list = op & 0xFFFF, n = 0, i, j, k, low, hb;
    uint32_t *skip = NULL, *slow, *back;
    for (i = 0; i < 16; i++) {
        n += list >> i & 1;
    }
    if (!(kinds & 8) || (wb && host(rn) < 0) || (load && wb && (list >> rn & 1))) {
        return 0;
    }
    if (op == 0xE92D8000u && push_code[M] != NULL) {    /* the driver's call */
        need_reg();
        sub_left(AL, pend + 3);
        pend = 0;
        bl_to(push_code[M]);
        emit(pc + 12);
        f_cpsr = 0;
        return 1;
    }
    if (op == 0xE8BD8000u && pop_code[M] != NULL) {     /* its return */
        need_reg();
        sub_left(AL, pend + 5);
        b_to(AL, pop_code[M]);
        return 1;
    }
    k = n + 2 + (load && (list & 0x8000) ? 2 : 0);
    if (cond != AL) {
        int fc;
        live = 15;
        need_cpsr(cond_reads[cond]);
        fc = f_cpsr;
        clobber(live);
        f_cpsr = fc;
        skip = b_later(cond ^ 1);
        pend += 1;
        sub_left(AL, k - 1);
    } else {
        pend += k;
    }
    hb = src(rn, IP, 0);
    low = up ? (pre ? 4 : 0) : -n * 4 + (pre ? 0 : 4);
    if (low > 0) {
        dp_imm(AL, ADD, 0, IP, hb, imm12((uint32_t)low));
    } else if (low < 0) {
        dp_imm(AL, SUB, 0, IP, hb, imm12((uint32_t)-low));
    }
    clobber(live);
    if (load) {
        dp_reg(AL, MOV, 0, LR, 0, low ? IP : hb, 3, 2);
        dp_imm(AL, CMP, 1, 0, LR, imm12(lim >> 2));
        slow = b_later(2);
    } else {
        if (!low && hb != IP) {
            mov(IP, hb);
        }
        dp_reg(AL, MOV, 1, LR, 0, IP, 1, 15);
        dp_imm(1, 3, 1, LR, LR, 0xFE);
        slow = b_later(9);
        dp_imm(AL, BIC, 0, IP, IP, 3);
    }
    dp_reg(AL, ADD, 0, IP, load && !low ? hb : IP, R_RAM, 0, 0);
    for (i = 0, j = 0; i < 16; i++) {
        if (!(list >> i & 1)) {
            continue;
        }
        if (load) {
            if (i == 15) {
                ldr_imm(AL, IP, IP, j * 4);
            } else if (host(i) >= 0) {
                ldr_imm(AL, host(i), IP, j * 4);
            } else {
                ldr_imm(AL, LR, IP, j * 4);
                str_imm(AL, LR, R_ST, i * 4);
            }
        } else {
            int h = i == 15 ? LR : host(i);
            if (i == 15) {
                const_to(AL, LR, pc + 12);
            } else if (h < 0) {
                ldr_imm(AL, LR, R_ST, i * 4);
                h = LR;
            }
            str_imm(AL, h, IP, j * 4);
        }
        j++;
    }
    if (wb) {
        dp_imm(AL, up ? ADD : SUB, 0, host(rn), host(rn), imm12((uint32_t)n * 4));
    }
    back = cur->at;
    if (load && (list & 0x8000)) {
        slow_stub(slow, op, pc, 0, 1, NULL);
        dp_imm(AL, BIC, 0, IP, IP, 3);
        sub_left(AL, pend);
        b_to(AL, disp_code[M]);
    } else {
        slow_stub(slow, op, pc, 0, 0, back);
    }
    if (skip) {
        point(skip, cur->at);
    }
    f_cpsr = 0;
    f_reg = 15;
    return 1;
}

static void set_lr(uint32_t v) {
    if (host(14) >= 0) {
        const_to(AL, host(14), v);
    } else {
        const_to(AL, IP, v);
        str_imm(AL, IP, R_ST, 14 * 4);
    }
}

/* an exit seldom taken: out through a stub in the cold area, which takes the flags into r8 */
static void cold_exit(int cond, int cycles, uint32_t to) {
    uint32_t* b = b_later(cond);
    int fc = f_cpsr, fr = f_reg;
    cur = &cold;
    point(b, cur->at);
    need_reg();
    f_cpsr = fc;
    f_reg = fr;
    sub_left(AL, cycles);
    exit_to(AL, to);
    cur = &hot;
}

/* a conditional branch taken at most half the times it ran, in a block made again */
static int seldom(uint32_t op, uint32_t pc, int m) {
    return op >> 28 != AL && !(op & (1u << 24)) && tracing && br[m][pc >> 2][0] >= 16 &&
           br[m][pc >> 2][1] <= br[m][pc >> 2][0] / 2;
}

/* B and BL; a branch not taken goes on in the block, and one followed goes on at its target */
static void branch(uint32_t op, uint32_t pc, int follow) {
    int cond = op >> 28;
    uint32_t to = pc + 8 + (uint32_t)((int32_t)(op << 8) >> 6), *skip = NULL;
    need_cpsr(cond_reads[cond]);
    if (counting) {
        lit(AL, IP, (uint32_t)(uintptr_t)br[M][pc >> 2]);
        ldr_imm(AL, LR, IP, 0);
        dp_imm(AL, ADD, 0, LR, LR, 1);
        str_imm(AL, LR, IP, 0);
        if (cond != AL) {
            ldr_imm(cond, LR, IP, 4);
            dp_imm(cond, ADD, 0, LR, LR, 1);
            str_imm(cond, LR, IP, 4);
        }
    }
    if (follow) {
        if (cond != AL) {
            cold_exit(cond ^ 1, pend + 1, pc + 4);
        }
        if (op & (1u << 24)) {
            set_lr(pc + 4);
        }
        pend += 3;
        return;
    }
    if (seldom(op, pc, M)) {
        cold_exit(cond, pend + 3, to);
        pend += 1;
        return;
    }
    need_reg();
    if (op & (1u << 24)) {
        if (cond != AL) {
            skip = b_later(cond ^ 1);
            cond = AL;
        }
        set_lr(pc + 4);
    }
    sub_left(cond, pend + 3);
    exit_to(cond, to);
    if (skip) {
        point(skip, cur->at);
    }
    pend += 1;
}

/* a data processing instruction to the PC without S: on to the block there */
static int pc_write(uint32_t op, uint32_t pc, int live) {
    int cond = op >> 28;
    uint32_t* skip = NULL;
    int rs = (op >> 8) & 15, rn = (op >> 16) & 15, rm = op & 15, opc = (op >> 21) & 15;
    if (!(op >> 25 & 1) && (op & 0x10) && host(rs) < 0 && opc != MOV && opc != 15 && host(rn) < 0 && host(rm) < 0) {
        return 0;
    }
    need_cpsr(cond_reads[cond]);
    need_reg();
    if (cond != AL) {
        skip = b_later(cond ^ 1);
    }
    if (!data_processing((op & 0x0FFFFFFFu) | (uint32_t)AL << 28, pc, live, 1, 0)) {
        return 0;
    }
    dp_imm(AL, BIC, 0, IP, IP, 3);
    sub_left(AL, pend + 3);
    b_to(AL, disp_code[M]);
    if (skip) {
        point(skip, cur->at);
    }
    pend += 1;
    return 1;
}

static int pc_load(uint32_t op) {
    return ((op >> 25) & 7) == 4 && (op & 0x00108000u) == 0x00108000u;
}

static int ends(uint32_t op) {
    int t = classify(op);
    if (op >> 28 != AL) {
        return t == T_END;
    }
    return t == T_END || t == T_PC || t == T_B || (t == T_LSM && (op & 0x00108000u) == 0x00108000u);
}

/* a branch the block goes on through, when blocks are made again: taken most times, into code entered mostly
   from it */
static int follows(uint32_t op, uint32_t pc, int m) {
    uint32_t to = pc + 8 + (uint32_t)((int32_t)(op << 8) >> 6), runs = br[m][pc >> 2][0];
    uint32_t taken = op >> 28 == AL ? runs : br[m][pc >> 2][1];
    return tracing && to < ARM7_JIT_WORDS * 4 && taken >= 16 && taken > runs / 2 && taken >= ent[m][to >> 2] / 2;
}

static void cover(uint32_t a) {
    covered[a >> 7] |= 1u << (a >> 2 & 31);
    if (arm7_jit2_range[1] == 0) {
        arm7_jit2_range[0] = a;
        arm7_jit2_range[1] = 4;
    } else {
        uint32_t lo = arm7_jit2_range[0] < a ? arm7_jit2_range[0] : a;
        uint32_t hi = arm7_jit2_range[0] + arm7_jit2_range[1];
        hi = hi > a + 4 ? hi : a + 4;
        arm7_jit2_range[0] = lo;
        arm7_jit2_range[1] = hi - lo;
    }
}

static BLOCK2* translate(uint32_t start, int m) {
    const uint8_t* ram = *st->ram;
    uint32_t ops[MAX_INSNS], pcs[MAX_INSNS], pc, *code;
    uint8_t live[MAX_INSNS + 1], fol[MAX_INSNS], partial[MAX_INSNS];
    int n = 0, i, budget = 0, warm;
    BLOCK2* b;
#ifdef MVC2_PROF
    uint32_t t0 = prof_now();
#endif
    for (pc = start;;) {
        uint32_t op, next = pc + 4;
        const BLOCK2* t;
        memcpy(&op, ram + pc, 4);
        pcs[n] = pc;
        fol[n] = (kinds & 1) && classify(op) == T_B && follows(op, pc, m);
        if (fol[n]) {
            next = pc + 8 + (uint32_t)((int32_t)(op << 8) >> 6);
        }
        ops[n++] = op;
        t = next < ARM7_JIT_WORDS * 4 ? map[m][next >> 2] : NULL;
        if ((ends(op) && !fol[n - 1]) || n == MAX_INSNS || next >= ARM7_JIT_WORDS * 4 ||
            (t != NULL && t != &none && (t != &soon || !fol[n - 1]))) {
            fol[n - 1] = 0;
            break;
        }
        for (i = 0; i < n && pcs[i] != next; i++) {
        }
        if (i < n) {
            fol[n - 1] = 0;
            break;
        }
        if (fol[n - 1]) {
            into[m][next >> 7] |= 1u << (next >> 2 & 31);
        }
        pc = next;
    }
    live[n] = 15;                                       /* every exit needs them */
    for (i = n - 1; i >= 0; i--) {
        int t = classify(ops[i]), out = (t == T_B && !(fol[i] && ops[i] >> 28 == AL)) || t == T_PC || t == T_END ||
                                        t == T_MID || pc_load(ops[i]);
        live[i] = (uint8_t)(reads(ops[i]) | (out ? 15 : 0) | (live[i + 1] & ~kills(ops[i])));
        if (i < n - 1) {
            budget += max_cycles(ops[i]);
        }
    }
    for (i = 0; i < n; i++) {                           /* logical ops with S whose C and V can stay in r8 */
        int j;
        partial[i] = 0;
        if (classify(ops[i]) != T_DP || !(ops[i] >> 20 & 1) || is_arith((ops[i] >> 21) & 15)) {
            continue;
        }
        for (j = i + 1; j < n; j++) {
            uint32_t o = ops[j];
            int t = classify(o), c = o >> 28;
            if (t == T_NOP || (t == T_DP && !(o >> 20 & 1) && !(cond_reads[c] & 3) && !(reads(o) & 2))) {
                continue;
            }
            if (t == T_DP && c == AL && !(reads(o) & 2)) {
                if (is_arith((o >> 21) & 15)) {
                    partial[i] = 1;
                    break;
                }
                continue;
            }
            if (t == T_B && c != AL && !(cond_reads[c] & 3) && (fol[j] || seldom(o, pcs[j], m))) {
                continue;
            }
            break;
        }
    }
    if (nblocks == MAX_BLOCKS || warm_start - hot_free < 4096 || hot_end - warm_free < 4096 ||
        cold_end - cold_free < 8192 ||
        nsites + n > MAX_SITES) {
        arm7_jit2_reset();
    }
    warm = !tracing || ent[m][start >> 2] < hot_min;
    hot.at = warm ? warm_free : hot_free;
    cold.at = cold_free;
    hot.n = hot.uses = cold.n = cold.uses = 0;
    cur = &hot;
    full = 0;
    M = m;
    entry = start;
    pend = 0;
    nlits = 0;
    code = hot.at;
    {                                                   /* the gate: on only while the cycles allow it all */
        uint32_t* out;
        f_cpsr = 0;
        f_reg = 15;
        while (imm12((uint32_t)budget) < 0) {
            budget++;
        }
        dp_imm(AL, CMP, 1, 0, R_LEFT, imm12((uint32_t)budget));
        out = b_later(13);                              /* ble */
        cur = &cold;
        point(out, cur->at);
        leave_to(start);
        cur = &hot;
    }
    b = &blocks[nblocks];
    if (counting) {
        lit(AL, IP, (uint32_t)(uintptr_t)&b->count);
        ldr_imm(AL, LR, IP, 0);
        dp_imm(AL, ADD, 0, LR, LR, 1);
        str_imm(AL, LR, IP, 0);
    }
    for (i = 0; i < n; i++) {
        uint32_t op = ops[i];
        int t = classify(op), ok = 1;
        pc = pcs[i];
        switch (t) {
        case T_NOP:
            pend += 1;
            break;
        case T_DP:
            ok = (kinds & 1) && data_processing(op, pc, live[i + 1], 0, partial[i]);
            if (ok) {
                pend += 1;
            }
            break;
        case T_PC:
            ok = (kinds & 1) && pc_write(op, pc, live[i + 1]);
            break;
        case T_B:
            if (kinds & 1) {
                branch(op, pc, fol[i]);
            } else {
                ok = 0;
            }
            break;
        case T_LS:
            ok = single_transfer(op, pc, live[i + 1]);
            break;
        case T_LSM:
            ok = block_transfer(op, pc, pc_load(op) ? 15 : live[i + 1]);
            break;
        default:
            ok = 0;
            break;
        }
        if (!ok) {
            if (t == T_PC || t == T_B || t == T_END || pc_load(op)) {
                through(op, pc, 1);                     /* out after it, wherever it went */
                n = i + 1;
                break;
            }
            through(op, pc, 0);
        }
        f_cpsr |= ~live[i + 1] & 15;                    /* dead flags are right anywhere */
        f_reg |= ~live[i + 1] & 15;
        if (hot.at - code > 900 || full) {
            break;
        }
    }
    if (i == n && !ends(ops[n - 1])) {
        need_reg();
        sub_left(AL, pend);
        exit_to(AL, pcs[n - 1] + 4);
    }
    if (full || hot.at - code > 900) {
        return &none;
    }
    place_pool(&hot);
    place_pool(&cold);
    if (!tracing) {                                     /* compaction flushes it all at the end */
        host_code_flush(code, (uint32_t)(hot.at - code) * 4);
        host_code_flush(cold_free, (uint32_t)(cold.at - cold_free) * 4);
        JIT_STAT(arm7_jit_stats.flushes += 2);
    }
    JIT_STAT(arm7_jit_stats.made++);
    JIT_STAT(arm7_jit_stats.words += (uint32_t)(hot.at - code));
    JIT_STAT(arm7_jit_stats.made_ticks += prof_now() - t0);
    JIT_STAT(arm7_jit_stats.hot_held += (uint32_t)(hot.at - code));
    JIT_STAT(arm7_jit_stats.cold_held += (uint32_t)(cold.at - cold_free));
    JIT_STAT(arm7_jit_stats.guest_held += (uint32_t)n);
    nblocks++;
    JIT_STAT(arm7_jit_stats.held = (uint32_t)nblocks);
    b->count = 0;
    b->counted = counting;
    b->mode = m;
    b->pc = start;
    b->end = pcs[n - 1] + 4;
    b->budget = budget;
    b->code = code;
    b->n = n;
    if (warm) {
        warm_free = hot.at;
    } else {
        hot_free = hot.at;
    }
    cold_free = cold.at;
    for (i = 0; i < n; i++) {
        b->at[i] = (uint16_t)(pcs[i] >> 2);
        cover(pcs[i]);
    }
    for (i = 0; i < nlits; i++) {
        cover(lits[i]);
    }
    if (!checking) {
        jmap[m][start >> 2] = code;
    }
    return b;
}

static void clear(void) {
    int m, i;
    memset(map, 0, sizeof tb->map);
    memset(covered, 0, sizeof tb->covered);
    for (m = 0; m < 2; m++) {
        for (i = 0; i < ARM7_JIT_WORDS; i++) {
            jmap[m][i] = miss_code[m];
        }
    }
    nblocks = nsites = 0;
    arm7_jit2_range[0] = arm7_jit2_range[1] = 0;
    link_site = NULL;
    exit_pc = ~0u;
    JIT_STAT(arm7_jit_stats.held = arm7_jit_stats.hot_held = arm7_jit_stats.cold_held = 0);
    JIT_STAT(arm7_jit_stats.guest_held = 0);
}

int arm7_jit2_init(ARM7_JIT_STATE* state, int mode) {
    uint32_t size;
    uint32_t* mem;
    int m, g;
    if (ready) {
        return ready > 0;
    }
    mem = host_code(&size);
    st = state;
    kinds = mode & 15 ? mode & 15 : 15;
    checking = mode >> 4 & 1;
    ready = -1;
    if (mem == NULL) {
        return 0;
    }
    lim = (*st->ram_mask + 1) - 0x10000;
    if (imm12(lim) < 0 || imm12(lim >> 2) < 0 || (tb = calloc(1, sizeof *tb)) == NULL) {
        return 0;
    }
    blocks = tb->blocks;
    map = tb->map;
    jmap = tb->jmap;
    heat = tb->heat;
    sites = tb->sites;
    covered = tb->covered;
    br = tb->br;
    ent = tb->ent;
    into = tb->into;
    slowed = tb->slowed;
    for (m = 0; m < 2; m++) {
        for (g = 0; g < 16; g++) {
            host_of[m][g] = -1;
        }
        for (g = 0; g < 8; g++) {
            host_of[m][kept[m][g]] = (int8_t)g;
        }
    }
    hot_start = shared_start = mem;
    hot_end = cold_start = mem + size / 8;
    cold_end = mem + size / 4;
    hot.at = hot_start;
    cur = &hot;
    make_shared(0);
    make_shared(1);
    host_code_flush(hot_start, (uint32_t)(hot.at - hot_start) * 4);
    hot_start = hot_free = hot.at;
    warm_start = warm_free = mem + size / 16;
    cold_free = cold_start;
    clear();
    ready = 1;
    return 1;
}

/* every block let go, and the code memory used again from the start */
void arm7_jit2_reset(void) {
    JIT_STAT(arm7_jit_stats.resets++);
    hot_free = hot_start;
    warm_free = warm_start;
    cold_free = cold_start;
    clear();
    dirty = 0;
}

void arm7_jit2_check(void) {
    JIT_STAT(arm7_jit_stats.checks++);
    dirty = 1;
}

/* the registers an instruction names, as bits */
static uint32_t regs_of(uint32_t op) {
    uint32_t rn = 1u << ((op >> 16) & 15), rd = 1u << ((op >> 12) & 15), rs = 1u << ((op >> 8) & 15), rm = 1u << (op & 15);
    switch ((op >> 25) & 7) {
    case 0:
        if ((op & 0x0FC000F0u) == 0x00000090u || (op & 0x0F8000F0u) == 0x00800090u) {
            return rn | rd | rs | rm;
        }
        if ((op & 0x90u) == 0x90u) {
            return rn | rd | rm;
        }
        return rn | rd | rm | ((op & 0x10) ? rs : 0);
    case 1:
        return rn | rd;
    case 2:
        return rn | rd;
    case 3:
        return rn | rd | rm;
    case 4:
        return rn | (op & 0xFFFF);
    case 5:
        return op & (1u << 24) ? 1u << 14 : 0;
    default:
        return 0;
    }
}

/* the hot blocks made again, in the guest's order after one another, with the eight registers they use most kept;
   the rest are let go, to be made again if they are wanted */
static void compact(void) {
    uint16_t* order = tb->order;
    KEEP* keep = tb->keep;
    uint32_t* c = tb->runs;
    uint64_t all = 0, run = 0;
    uint64_t total = 0, acc = 0, use[2][16];
    int n = 0, i, j, m, g, nkeep = 0;
    JIT_STAT(arm7_jit_stats.compactions++);
    quiet = 0;
    made_since = 0;
    for (i = 0; i < nblocks; i++) {
        if (blocks[i].counted) {
            order[n++] = (uint16_t)i;
            total += blocks[i].count;
        }
    }
    for (i = 1; i < n; i++) {                           /* the most run first */
        uint16_t k = order[i];
        for (j = i; j > 0 && blocks[order[j - 1]].count < blocks[k].count; j--) {
            order[j] = order[j - 1];
        }
        order[j] = k;
    }
    for (i = 0; i < n && acc * 1000 < total * 995 && blocks[order[i]].count >= 16; i++) {
        acc += blocks[order[i]].count;
    }
    n = i;
    for (i = 0; i < nblocks; i++) {                     /* those made last time, and the hot ones */
        blocks[i].counted = !blocks[i].counted;
    }
    for (i = 0; i < n; i++) {
        blocks[order[i]].counted = 1;
    }
    memset(use, 0, sizeof use);
    memset(ent, 0, sizeof tb->ent);
    memset(into, 0, sizeof tb->into);
    for (i = 0; i < nblocks; i++) {
        const BLOCK2* b = &blocks[i];
        if (!b->counted) {
            continue;
        }
        for (j = 0; j < b->n; j++) {
            uint32_t op, bits;
            memcpy(&op, *st->ram + b->at[j] * 4u, 4);
            for (bits = regs_of(op) & 0x7FFF; bits; bits &= bits - 1) {
                use[b->mode][__builtin_ctz(bits)] += b->count;
            }
        }
        ent[b->mode][b->pc >> 2] = b->count;
        keep[nkeep].pc = b->pc;
        keep[nkeep].count = b->count;
        keep[nkeep++].mode = b->mode;
    }
    for (i = 0; i < nkeep; i++) {                       /* the blocks with 95% of the runs: the hot area */
        uint32_t v = keep[i].count;
        for (j = i; j > 0 && c[j - 1] < v; j--) {
            c[j] = c[j - 1];
        }
        c[j] = v;
        all += v;
    }
    hot_min = 0;
    for (i = 0; i < nkeep && run * 100 < all * 95; i++) {
        run += c[i];
        hot_min = c[i];
    }
    for (m = 0; m < 2; m++) {
        int taken[16] = { 0 }, k;
        for (k = 0; k < 8; k++) {
            int best = -1;
            for (g = 0; g < 15; g++) {
                if (!taken[g] && (best < 0 || use[m][g] > use[m][best])) {
                    best = g;
                }
            }
            taken[best] = 1;
        }
        for (g = 0, k = 0; g < 15; g++) {
            if (taken[g]) {
                kept[m][k++] = (uint8_t)g;
            }
        }
        for (g = 0; g < 16; g++) {
            host_of[m][g] = -1;
        }
        for (g = 0; g < 8; g++) {
            host_of[m][kept[m][g]] = (int8_t)g;
        }
    }
    arm7_jit2_reset();
    hot.at = shared_start;
    cur = &hot;
    make_shared(0);
    make_shared(1);
    hot_start = hot_free = hot.at;
    clear();                                            /* the PCs without blocks to the new miss code */
    for (i = 0; i < nkeep; i++) {
        map[keep[i].mode][keep[i].pc >> 2] = &soon;
    }
    counting = 0;
    tracing = 1;
    for (m = 0; m < 2; m++) {
        uint32_t pc;
        for (pc = 0; pc < ARM7_JIT_WORDS * 4; pc += 4) {
            if (map[m][pc >> 2] == &soon) {
                BLOCK2* b;
                if (into[m][pc >> 7] >> (pc >> 2 & 31) & 1) {
                    map[m][pc >> 2] = NULL;             /* in a block that went on through a branch to it */
                    continue;
                }
                b = map[m][pc >> 2] = translate(pc, m);
                if (b != &none) {
                    b->count = ent[m][pc >> 2];
                }
            }
        }
    }
    tracing = 0;
    counting = 1;
    host_code_flush(shared_start, (uint32_t)(hot_free - shared_start) * 4);
    host_code_flush(warm_start, (uint32_t)(warm_free - warm_start) * 4);
    host_code_flush(cold_start, (uint32_t)(cold_free - cold_start) * 4);
    JIT_STAT(arm7_jit_stats.flushes += 3);
}

/* in the block being checked: whether it goes on from the instruction at p to `to` */
int arm7_jit2_goes_on(uint32_t p, uint32_t to) {
    int i;
    for (i = 0; i + 1 < checked->n; i++) {
        if (checked->at[i] * 4u == p) {
            return checked->at[i + 1] * 4u == to;
        }
    }
    return 0;
}

int arm7_jit2_covers(uint32_t addr) {
    return addr < ARM7_JIT_WORDS * 4 && (covered[addr >> 7] >> (addr >> 2 & 31) & 1);
}

int arm7_jit2_slow(uint32_t k) {
    const SITE* s;
    int n;
    if (k & SITE_PUSH) {
        arm7_jit_step(0xE92D8000u, (k & ~SITE_PUSH) - 12);
        return 0;
    }
    if (k == SITE_POP) {
        arm7_jit_step(0xE8BD8000u, 0);
        return 0;
    }
    s = &sites[k];
    if (!s->interp) {
        slowed[s->mode][s->pc >> 2]++;
    }
    n = arm7_jit_step(s->op, s->pc);
    return s->interp ? n : 0;
}

int arm7_jit2_run(uint32_t pc, int fiq, int done, int cycles, uint32_t* end) {
    BLOCK2* b;
    int target = pc == exit_pc && fiq == exit_mode, left;
    if (dirty) {
        arm7_jit2_reset();
    }
    exit_pc = ~0u;
    if (++quiet > QUIET && made_since >= 16 && (end != NULL || !checking)) {
        compact();
    }
    b = map[fiq][pc >> 2];
    if (b == NULL || b == &soon) {
        if (!target && arm7_jit2_covers(pc)) {
            return -1;                                  /* inside a block: on to the next one's start */
        }
        if (heat[fiq][pc >> 2] < HOT) {
            heat[fiq][pc >> 2]++;
            return -1;
        }
        b = map[fiq][pc >> 2] = translate(pc, fiq);
        quiet = 0;
        made_since++;
    }
    if (b == &none) {
        return -1;
    }
    if (cycles - done <= b->budget) {
        JIT_STAT(arm7_jit_stats.refused++);
        return -1;
    }
    if (end) {
        *end = b->end;
        checked = b;
        return 0;
    }
    if (target && link_site != NULL && !checking) {
        point(link_site, b->code);
        host_code_flush(link_site, 4);
        JIT_STAT(arm7_jit_stats.flushes++);
        JIT_STAT(arm7_jit_stats.links++);
    }
    link_site = NULL;
    JIT_STAT(arm7_jit_stats.entries++);
    left = ((int (*)(const uint32_t*, int))(void*)enter_code[fiq])(b->code, cycles - done);
    exit_pc = st->r[15];
    exit_mode = fiq;
    return cycles - left;
}
