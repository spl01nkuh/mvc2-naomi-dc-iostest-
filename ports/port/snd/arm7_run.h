/*
 * arm7_run.h  The interpreter's loop, built in arm7.c once for each decode table: RUN, TABLE (of DEC_T),
 * WIDE (the fields kept in it, or taken from the word) and the field macros.
 */

static int RUN(int cycles) {
    static const void* const go[K_COUNT] = {
        &&k_step, &&k_b, &&k_bl, &&k_addi, &&k_subi, &&k_subsi, &&k_cmpi, &&k_tsti, &&k_tsti_c0, &&k_movi, &&k_movr,
        &&k_movsr, &&k_ldrb, &&k_ldr, &&k_ldr_pc, &&k_strb, &&k_str, &&k_push_pc, &&k_pop_pc,
        &&k_andi, &&k_orri, &&k_bici, &&k_add_pc, &&k_addr, &&k_cmpr, &&k_psr
    };
    const uint8_t* ram = aica_ram;
    uint32_t code_mask = aica_ram_mask & ~3u;
    int done = 0;
#ifdef ARM7_JIT
    const int direct = !(jit_wanted && jit_side);   /* no translated code to look for first */
    const int trust = code_track && !jit_wanted;    /* translated code stores past code_written */
#else
    const int direct = 1;
    const int trust = code_track;
#endif
    run_left = 1;
    event = fiq_line && !(cpsr & F_F);
    while (done < cycles) {
        uint32_t pc, op;
        const DEC_T* e;
        int branched = 0;
        if (event) {
            if (!run_left) {
                break;
            }
            event = 0;
            if (fiq_line && !(cpsr & F_F)) {
                exception(M_FIQ, 0x1C, r[15] + 4, F_I | F_F);
                done += 3;
                if (fiq_fast && direct) {
                    int k = fiq_native(&done, cycles, trust);
                    if (k == NAT_BREAK) {
                        break;
                    }
                    if (k == NAT_TOP) {
                        continue;
                    }
                    pc = r[15];
                    goto fetch;
                }
            }
        }
        pc = r[15];
#ifdef ARM7_JIT
        if (jit2 && jit_side && pc < ARM7_JIT_WORDS * 4) {
            int d = jit2_go(pc, done, cycles);
            if (d >= 0) {
                JIT_STAT(arm7_jit_stats.jit_cycles += (uint32_t)(d - done));
                done = d;
                continue;
            }
        }
        if (jit_wanted && jit_side && pc < ARM7_JIT_WORDS * 4) {
            if (!jit_tried) {
                jit_tried = 1;                  /* on the thread that runs it, and only if wanted */
                jit_on = arm7_jit_init(&jit_state);
            }
            if (jit_on) {
                int d = arm7_jit_run(pc, done, cycles);
                if (d >= 0) {
                    JIT_STAT(arm7_jit_stats.jit_cycles += (uint32_t)(d - done));
                    done = d;
                    continue;
                }
            }
        }
#endif
    fetch:
        if (pc < DEC_N * 4) {
            DEC_T* d = &TABLE[pc >> 2];
            if (trust && d->go != NULL) {
                op = d->op;             /* a write to the word would have dropped it */
                goto decoded;
            }
            memcpy(&op, ram + pc, 4);
            if (d->go == NULL || d->op != op) {
                int32_t v = 0;
                int k = decode(op, &v), i;
                d->op = op;
                d->go = go[k];
                dec_live[pc >> 7] |= 1u << (pc >> 2 & 31);
#if WIDE
                d->v = v;
                d->rd = (op >> 12) & 15;
                d->rn = (op >> 16) & 15;
                d->rm = op & 15;
                d->cond = op >> 28;
#endif
                if (fast_on && (k == K_LDRB || k == K_ADDI) && (i = fast_find(pc)) >= 0) {
                    fast[i].plain[WIDE] = go[k];
                    d->go = &&k_fast;
#if WIDE
                    d->rm = (uint8_t)i;     /* not used by either kind */
#endif
                }
            }
        decoded:
            e = d;
            if (COND != 14 && !(cond_ok[COND] >> (cpsr >> 28) & 1)) {
                NEXT(1, pc + 4);
            }
            goto *e->go;
        }
        if ((pc & 0xFF800000u) == 0) {
            memcpy(&op, ram + (pc & code_mask), 4);
        } else {
            op = aica_arm_read32(pc & ~3u);
        }
        if ((op & 0xF0000000u) != 0xE0000000u && !(cond_ok[op >> 28] >> (cpsr >> 28) & 1)) {
            r[15] = pc + 4;
            done++;
            continue;
        }
    k_step:
        r[15] = pc + 8;             /* as the instruction reads it */
        if (step_small == 2) {
            switch ((op >> 20) & 0xFF) {
            STEP_HOT
            default: done += step_one(op, pc, &branched); break;
            }
        } else if (step_small) {
            done += step_one(op, pc, &branched);
        } else {
            switch ((op >> 20) & 0xFF) {
            STEP64(0) STEP64(64) STEP64(128) STEP64(192)
            }
        }
        /* no write to the PC: on to the next instruction */
        if (!branched) {
            r[15] = pc + 4;
        }
        continue;
    k_b:
        NEXT(3, pc + 8 + V_B);
    k_bl:
        r[14] = pc + 4;
        NEXT(3, pc + 8 + V_B);
    k_addi:
        r[RD] = r[RN] + V_IMM;
        NEXT(1, pc + 4);
    k_subi:
        r[RD] = r[RN] - V_IMM;
        NEXT(1, pc + 4);
    k_subsi: {
        uint32_t a = r[RN], b = V_IMM, res = a - b;
        r[RD] = res;
        set_nzcv(res, a >= b, ((a ^ b) & (a ^ res)) >> 31);
        NEXT(1, pc + 4);
    }
    k_cmpi: {
        uint32_t a = r[RN], b = V_IMM, res = a - b;
        set_nzcv(res, a >= b, ((a ^ b) & (a ^ res)) >> 31);
        NEXT(1, pc + 4);
    }
    k_tsti:
        set_nz(r[RN] & V_IMM);
        NEXT(1, pc + 4);
    k_tsti_c0:                      /* a rotated immediate: the carry is its top bit */
        set_nz(r[RN] & V_IMM);
        cpsr &= ~F_C;
        NEXT(1, pc + 4);
    k_andi:
        r[RD] = r[RN] & V_IMM;
        NEXT(1, pc + 4);
    k_orri:
        r[RD] = r[RN] | V_IMM;
        NEXT(1, pc + 4);
    k_bici:
        r[RD] = r[RN] & ~V_IMM;
        NEXT(1, pc + 4);
    k_add_pc:
        r[RD] = pc + 8 + V_IMM;
        NEXT(1, pc + 4);
    k_addr:
        r[RD] = r[RN] + r[RM];
        NEXT(1, pc + 4);
    k_cmpr: {
        uint32_t a = r[RN], b = r[RM], res = a - b;
        set_nzcv(res, a >= b, ((a ^ b) & (a ^ res)) >> 31);
        NEXT(1, pc + 4);
    }
    k_psr:                          /* MRS and MSR */
        r[15] = pc + 8;
        psr_transfer(e->op);
        r[15] = pc + 4;
        done += 1;
        continue;
    k_movi:
        r[RD] = V_IMM;
        NEXT(1, pc + 4);
    k_movr:
        r[RD] = r[RM];
        NEXT(1, pc + 4);
    k_movsr:
        r[RD] = r[RM];
        set_nz(r[RD]);
        NEXT(1, pc + 4);
    k_ldrb:
        r[RD] = rd8(r[RN] + V_OFF);
        r[15] = pc + 4;
        done += 3;
        continue;
    k_ldr:
        r[RD] = ldr_word(r[RN] + V_OFF);
        r[15] = pc + 4;
        done += 3;
        continue;
    k_ldr_pc:
        r[RD] = ldr_word(pc + 8 + V_OFF);
        r[15] = pc + 4;
        done += 3;
        continue;
    k_strb:
        wr8(r[RN] + V_OFF, r[RD]);
        r[15] = pc + 4;
        done += 2;
        continue;
    k_str:
        wr32(r[RN] + V_OFF, r[RD]);
        r[15] = pc + 4;
        done += 2;
        continue;
    k_push_pc:
        wr32(r[RN] - 4, pc + 12);
        r[RN] -= 4;
        if (fast_on && direct && !event && done + 3 + 1 < cycles) {   /* the driver's call: stmfd sp!,{pc}; nop; b */
            uint32_t nop, b;
            memcpy(&nop, ram + pc + 4, 4);
            memcpy(&b, ram + pc + 8, 4);
            if (nop == 0xE1A00000u && (b & 0xFF000000u) == 0xEA000000u) {
                NEXT(3 + 1 + 3, b_to(b, pc + 8));
            }
        }
        r[15] = pc + 4;
        done += 3;
        continue;
    k_fast: {
        const FAST* f = &fast[FAST_I];
        int d = event ? done : fast_run(f, done, cycles);
        if (d == done) {
            goto *f->plain[WIDE];
        }
        done = d;
        continue;
    }
    k_pop_pc: {
        uint32_t a = r[RN];
        r[RN] = a + 4;
        r[15] = rd32(a) & ~3u;
        done += 5;
        continue;
    }
    }
    cycles_run += (uint64_t)done;
    return done;
}
