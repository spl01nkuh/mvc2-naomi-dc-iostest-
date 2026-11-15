/*
 * arm7_fiq.h  The sound driver's FIQ handler (its timer tick) run natively, built into arm7.c: each word as
 * arm7_run would run it, with the same cycles, events and stops. Used only while the words are the driver's.
 */

/* the words run here; any other address goes back to the interpreter */
#define FIQ_PCS(X)                                                                                            \
    X(001C) X(0C40) X(0C44) X(0C48) X(0C4C) X(0C50) X(0C54) X(0C58) X(0C5C) X(0C60) X(0C64) X(0C68) X(0C6C)   \
    X(0C70) X(0C74) X(0C78) X(0C7C) X(0C80) X(0C84) X(0C88) X(0C8C) X(0C90) X(0C94) X(0C98) X(0C9C) X(0CA0)   \
    X(0CA4) X(0CA8) X(0CAC) X(0CB0) X(0CB4) X(0CB8) X(0CBC) X(0CC0) X(0CC4) X(0CC8) X(0CCC) X(0CD0) X(0CD4)   \
    X(0CD8) X(0CDC) X(0CE0) X(0CE4) X(0CE8) X(0CEC) X(0CF0) X(0CF4) X(0CF8) X(0CFC) X(0D00) X(0D04) X(0D08)   \
    X(0D0C) X(0D10) X(0D14) X(0D18) X(0D1C) X(0D20) X(0D24) X(0D28) X(0D2C) X(0D30) X(0D34) X(0D38) X(0D3C)   \
    X(0D40) X(0D44) X(0D48) X(0D4C) X(0D50) X(0D54) X(0D58) X(0D5C) X(0D60) X(6810) X(6814) X(6818) X(681C)   \
    X(6820) X(6824) X(6828) X(682C) X(6830) X(6834) X(6838) X(683C) X(6840) X(6844) X(6848) X(684C) X(6850)   \
    X(6854) X(6858) X(685C) X(6860) X(6864) X(6868) X(6888) X(688C) X(6890) X(6894) X(6898) X(68B4)
#define FIQ_SUM 0xBC0C10C8u         /* those words, each xored in after a turn of 5 */

enum { NAT_TOP, NAT_FETCH, NAT_BREAK };     /* back to the loop's top, to the fetch of r[15], or out of the run */
static int fiq_fast;

void arm7_fiq_fast(int on) {
    fiq_fast = on;
}

static int fiq_words_ok(void) {
#define FIQ_WORD(a) h = (h << 5 | h >> 27) ^ word_at(0x##a##u);
    uint32_t h = 0;
    FIQ_PCS(FIQ_WORD)
    return h == FIQ_SUM;
#undef FIQ_WORD
}

/* the memory and the flags out of line, so that each word's code stays short */
static __attribute__((noinline)) uint32_t n_ld(uint32_t a) {
    return ldr_word(a);
}

static __attribute__((noinline)) uint32_t n_ldb(uint32_t a) {
    return rd8(a);
}

static __attribute__((noinline)) void n_st(uint32_t a, uint32_t v) {
    wr32(a, v);
}

static __attribute__((noinline)) void n_stb(uint32_t a, uint32_t v) {
    wr8(a, v);
}

static __attribute__((noinline)) void n_cmp(uint32_t a, uint32_t b) {
    uint32_t res = a - b;
    set_nzcv(res, a >= b, ((a ^ b) & (a ^ res)) >> 31);
}

static __attribute__((noinline)) void n_nz(uint32_t v) {
    set_nz(v);
}

#define N_PASS(c) (cond_ok[c] >> (cpsr >> 28) & 1)
/* NEXT's kinds: on, and straight to the next fetch while there are cycles. r[15] is set only where the
   interpreter would look at it: on the way out, and before what reads it */
#define N_GO(n, to)                                                                                           \
    do {                                                                                                      \
        done += (n);                                                                                          \
        if (done >= cycles) {                                                                                 \
            r[15] = 0x##to##u;                                                                                \
            goto top;                                                                                         \
        }                                                                                                     \
        goto L_##to;                                                                                          \
    } while (0)
/* the same to a word not run here */
#define N_OUT(n, to)                                                                                          \
    do {                                                                                                      \
        done += (n);                                                                                          \
        r[15] = 0x##to##u;                                                                                    \
        goto out;                                                                                             \
    } while (0)
/* the kinds that go back to the loop's top, where an event is looked at */
#define N_CT(n, to)                                                                                           \
    do {                                                                                                      \
        done += (n);                                                                                          \
        if (done >= cycles || event) {                                                                        \
            r[15] = 0x##to##u;                                                                                \
            goto ct;                                                                                          \
        }                                                                                                     \
        goto L_##to;                                                                                          \
    } while (0)
#define N_B(c, to, next)                                                                                      \
    do {                                                                                                      \
        if (N_PASS(c)) N_GO(3, to);                                                                           \
        N_GO(1, next);                                                                                        \
    } while (0)
/* a branch to a word not run here */
#define N_B_OUT(c, to, next)                                                                                  \
    do {                                                                                                      \
        if (N_PASS(c)) N_OUT(3, to);                                                                          \
        N_GO(1, next);                                                                                        \
    } while (0)
/* a branch whose fall-through is not run here */
#define N_B_ELSE_OUT(c, to, next)                                                                             \
    do {                                                                                                      \
        if (N_PASS(c)) N_GO(3, to);                                                                           \
        N_OUT(1, next);                                                                                       \
    } while (0)
/* K_STEP and K_PSR: through the interpreter's own code */
#define N_STEP(a)                                                                                             \
    do {                                                                                                      \
        int br_ = 0;                                                                                          \
        r[15] = 0x##a##u + 8;                                                                                 \
        done += step_one(word_at(0x##a##u), 0x##a##u, &br_);                                                  \
        if (!br_) r[15] = 0x##a##u + 4;                                                                       \
        goto ct;                                                                                              \
    } while (0)
#define N_PSR(a, next)                                                                                        \
    do {                                                                                                      \
        psr_transfer(word_at(0x##a##u));                                                                      \
        N_CT(1, next);                                                                                        \
    } while (0)
/* stmfd sp!, {r0-r8, lr} and ldmfd sp!, {r0-r8, lr}: 12 cycles, as step() counts them */
static __attribute__((noinline)) void n_save(void) {
    uint32_t a = r[13] - 40;
    int i;
    for (i = 0; i < 9; i++) {
        wr32(a + i * 4, r[i]);
    }
    wr32(a + 36, r[14]);
    r[13] = a;
}

static __attribute__((noinline)) void n_load(void) {
    uint32_t a = r[13];
    int i;
    r[13] = a + 40;
    for (i = 0; i < 9; i++) {
        r[i] = rd32(a + i * 4);
    }
    r[14] = rd32(a + 36);
}

#define N_PUSH_PC(a, next)                                                                                    \
    do {                                                                                                      \
        n_st(r[13] - 4, 0x##a##u + 12);                                                                       \
        r[13] -= 4;                                                                                           \
        N_CT(3, next);                                                                                        \
    } while (0)
#define N_POP_PC()                                                                                            \
    do {                                                                                                      \
        uint32_t a_ = r[13];                                                                                  \
        r[13] = a_ + 4;                                                                                       \
        r[15] = n_ld(a_ & ~3u) & ~3u;                                                                         \
        done += 5;                                                                                            \
        goto ct;                                                                                              \
    } while (0)

/* from the FIQ's vector, just taken, to where the interpreter goes on: the run's `done` kept */
static int fiq_native(int* pdone, int cycles, int trust) {
    static uint32_t gen;
    static int checked;
    int done = *pdone;
    if (!(trust && checked && gen == code_gen)) {
        if (!fiq_words_ok()) {
            return NAT_FETCH;
        }
#define FIQ_LIVE(a) dec_live[0x##a##u >> 7] |= 1u << (0x##a##u >> 2 & 31);
        FIQ_PCS(FIQ_LIVE)           /* so that a write to one counts in code_gen */
#undef FIQ_LIVE
        gen = code_gen;
        checked = 1;
    }
    goto L_001C;
ct:                                 /* r[15] set by the instruction */
    if (done >= cycles) goto top;
    if (event) goto ev;
    goto dispatch;
ev:
    if (!run_left) {
        *pdone = done;
        return NAT_BREAK;
    }
    event = 0;
    if (fiq_line && !(cpsr & F_F)) {
        exception(M_FIQ, 0x1C, r[15] + 4, F_I | F_F);
        done += 3;
        goto L_001C;
    }
dispatch:
    switch (r[15]) {
#define FIQ_CASE(a) case 0x##a##u: goto L_##a;
    FIQ_PCS(FIQ_CASE)
#undef FIQ_CASE
    default: break;
    }
    *pdone = done;
    return NAT_FETCH;
out:
    if (done < cycles) {
        *pdone = done;
        return NAT_FETCH;
    }
top:
    *pdone = done;
    return NAT_TOP;

L_001C: N_GO(3, 0C40);                                  /* b */
L_0C40: r[10] = n_ld(0xC48 + 0x4E0); N_CT(3, 0C44); /* the handler's bases */
L_0C44: r[11] = n_ld(0xC4C + 0x4E0); N_CT(3, 0C48);
L_0C48: r[12] = n_ld(0xC50 + 0x4E0); N_CT(3, 0C4C);
L_0C4C: r[8] = n_ldb(r[12] + 8); N_CT(3, 0C50);
L_0C50: n_cmp(r[8], 1); N_GO(1, 0C54);
L_0C54: N_B(0, 0C64, 0C58);                          /* its stack is set up */
L_0C58: r[13] = n_ld(0xC60 + 0x4D4); N_CT(3, 0C5C);
L_0C5C: r[8] = 1; N_GO(1, 0C60);
L_0C60: n_stb(r[12] + 8, r[8]); N_CT(2, 0C64);
L_0C64: r[8] = spsr; N_CT(1, 0C68);                       /* mrs r8, spsr */
L_0C68: n_save(); N_CT(12, 0C6C);
L_0C6C: r[0] = n_ldb(r[11] + 0x500); N_CT(3, 0C70);       /* the interrupt's kind */
L_0C70: n_cmp(r[0], 2); N_GO(1, 0C74);
L_0C74: N_B_OUT(0, 0D64, 0C78);
L_0C78: n_cmp(r[0], 1); N_GO(1, 0C7C);
L_0C7C: N_B(0, 0CA4, 0C80);                          /* the timer */
L_0C80: r[0] = 1; N_GO(1, 0C84);
L_0C84: n_stb(r[11] + 0x504, r[0]); N_CT(2, 0C88);        /* cleared */
L_0C88: n_stb(r[11] + 0x504, r[0]); N_CT(2, 0C8C);
L_0C8C: n_stb(r[11] + 0x504, r[0]); N_CT(2, 0C90);
L_0C90: n_stb(r[11] + 0x504, r[0]); N_CT(2, 0C94);
L_0C94: n_load(); N_CT(12, 0C98);
L_0C98: N_PSR(0C98, 0C9C);                              /* msr spsr, r8 */
L_0C9C: r[14] -= 4; N_GO(1, 0CA0);
L_0CA0: N_STEP(0CA0);                                   /* movs pc, lr */
L_0CA4: r[0] = 0xD4; N_GO(1, 0CA8);
L_0CA8: n_st(r[11] + 0x94, r[0]); N_CT(2, 0CAC);        /* the timer reloaded */
L_0CAC: r[0] = n_ld(r[12] + 0x20); N_CT(3, 0CB0);   /* the ticks counted */
L_0CB0: r[0] += 1; N_GO(1, 0CB4);
L_0CB4: n_st(r[12] + 0x20, r[0]); N_CT(2, 0CB8);
L_0CB8: r[0] = n_ld(r[12] + 0x38); N_CT(3, 0CBC);
L_0CBC: r[0] += 1; N_GO(1, 0CC0);
L_0CC0: n_st(r[12] + 0x38, r[0]); N_CT(2, 0CC4);
L_0CC4: r[0] = n_ldb(r[12] + 3); N_CT(3, 0CC8);           /* already in the tick */
L_0CC8: n_cmp(r[0], 1); N_GO(1, 0CCC);
L_0CCC: N_B(0, 0D10, 0CD0);
L_0CD0: r[0] = 1; N_GO(1, 0CD4);
L_0CD4: n_stb(r[12] + 3, r[0]); N_CT(2, 0CD8);
L_0CD8: r[0] = cpsr; N_CT(1, 0CDC);                      /* the interrupts on */
L_0CDC: r[0] &= ~0xC0u; N_GO(1, 0CE0);
L_0CE0: N_PSR(0CE0, 0CE4);
L_0CE4: N_PUSH_PC(0CE4, 0CE8);                          /* stmfd sp!, {pc}; nop; b: a call */
L_0CE8: N_GO(1, 0CEC);
L_0CEC: N_GO(3, 6810);
L_0CF0: N_PUSH_PC(0CF0, 0CF4);
L_0CF4: N_GO(1, 0CF8);
L_0CF8: N_GO(3, 0D1C);
L_0CFC: r[0] = cpsr; N_CT(1, 0D00);                      /* and off */
L_0D00: r[0] |= 0xC0u; N_GO(1, 0D04);
L_0D04: N_PSR(0D04, 0D08);
L_0D08: r[0] = 0; N_GO(1, 0D0C);
L_0D0C: n_stb(r[12] + 3, r[0]); N_CT(2, 0D10);
L_0D10: r[0] = 0x80; N_GO(1, 0D14);
L_0D14: n_st(r[11] + 0xA4, r[0]); N_CT(2, 0D18);        /* the timer's interrupt cleared */
L_0D18: N_GO(3, 0C80);
L_0D1C: r[0] = n_ld(r[12] + 0x2C); N_CT(3, 0D20);
L_0D20: r[0] += 1; N_GO(1, 0D24);
L_0D24: n_st(r[12] + 0x2C, r[0]); N_CT(2, 0D28);
L_0D28: r[0] = n_ld(r[12] + 0x180); N_CT(3, 0D2C);
L_0D2C: r[1] = n_ld(r[10] + 0xAC); N_CT(3, 0D30);
L_0D30: n_cmp(r[0], r[1]); N_GO(1, 0D34);
L_0D34: if (!N_PASS(8)) N_GO(1, 0D38);   /* strhi */
        n_st(r[10] + 0xAC, r[0]); N_CT(2, 0D38);
L_0D38: r[0] = n_ldb(r[12] + 0x26); N_CT(3, 0D3C);
L_0D3C: n_cmp(r[0], 1); r[0] -= 1; N_GO(1, 0D40);
L_0D40: n_stb(r[12] + 0x26, r[0]); N_CT(2, 0D44);
L_0D44: N_B(1, 0D60, 0D48);
L_0D48: r[0] = 0x32; N_GO(1, 0D4C);
L_0D4C: n_stb(r[12] + 0x26, r[0]); N_CT(2, 0D50);
L_0D50: r[0] = n_ld(r[12] + 0x28); N_CT(3, 0D54);
L_0D54: n_st(r[10] + 0xA8, r[0]); N_CT(2, 0D58);
L_0D58: r[0] = n_ld(r[12] + 0x180); N_CT(3, 0D5C);
L_0D5C: n_st(r[10] + 0xAC, r[0]); N_CT(2, 0D60);
L_0D60: N_POP_PC();                                     /* ldmfd sp!, {pc} */
L_6810: r[5] = 0x6818 + 0x7F00; N_GO(1, 6814);         /* the tracks' tick */
L_6814: r[5] += 0x38; N_GO(1, 6818);
L_6818: r[0] = n_ld(r[5] + 12); N_CT(3, 681C);
L_681C: r[0] += 1; N_GO(1, 6820);
L_6820: n_st(r[5] + 12, r[0]); N_CT(2, 6824);
L_6824: r[6] = n_ld(r[5] + 8); N_CT(3, 6828);
L_6828: n_cmp(r[6], 0); N_GO(1, 682C);
L_682C: N_B_OUT(12, 68AC, 6830);
L_6830: r[0] = 0x6838 + 0x7C00; N_GO(1, 6834);
L_6834: r[0] += 24; N_GO(1, 6838);
L_6838: r[3] = 16; N_GO(1, 683C);
L_683C: r[1] = n_ldb(r[0]); N_CT(3, 6840);                /* each of 16 */
L_6840: n_nz(r[1] & 0x80); N_GO(1, 6844);
L_6844: N_B(0, 6888, 6848);
L_6848: n_nz(r[1] & 1); N_GO(1, 684C);
L_684C: if (N_PASS(0)) n_nz(r[1] & 0x20);
        N_GO(1, 6850);
L_6850: N_B(1, 6888, 6854);
L_6854: r[4] = n_ld(r[0] + 4); N_CT(3, 6858);
L_6858: r[2] = n_ld(r[0] + 20); N_CT(3, 685C);
L_685C: N_STEP(685C);                                   /* sub r2, r2, r4 */
L_6860: n_st(r[0] + 20, r[2]); N_CT(2, 6864);
L_6864: n_nz(r[1] & 2); N_GO(1, 6868);
L_6868: N_B_ELSE_OUT(0, 6888, 686C);
L_6888: r[0] += 48; N_GO(1, 688C);
L_688C: n_cmp(r[3], 1); r[3] -= 1; N_GO(1, 6890);
L_6890: N_B(1, 683C, 6894);
L_6894: n_nz(r[6]); N_GO(1, 6898);
L_6898: N_B_ELSE_OUT(0, 68B4, 689C);
L_68B4: N_POP_PC();
}

#undef N_GO
#undef N_OUT
#undef N_CT
#undef N_B
#undef N_B_OUT
#undef N_B_ELSE_OUT
#undef N_STEP
#undef N_PSR
#undef N_PASS
#undef N_PUSH_PC
#undef N_POP_PC
