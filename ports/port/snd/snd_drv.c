/*
 * snd_drv.c  The sound CPU's driver (Sega AM2's AICA driver, 1.76) as C: its routines on the sound RAM and the
 * AICA's registers, run in place of the interpreter where the RAM holds that driver. MVC2_SNDNATIVE=1 the timer's
 * FIQ, exactly; =3 the main loop too, as at the CPU's real speed; =2 and =4 run each FIQ (and pass) both ways from
 * the same state and log any difference.
 */

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "snd_drv.h"
#include "aica.h"
#include "arm7.h"
#include "naomi_host.h"

/* the driver's areas */
#define SHARED 0x0040u              /* read by the SH4 */
#define WORK 0xB200u
#define FIQ_STACK 0xB200u
#define TRACKS 0xE450u              /* 16 of 48 bytes */
#define TRACK_SIZE 48
#define TRACK_TICK 0xE750u
#define TRACK_PARAMS 0xDB00u        /* 32 bytes for each track: its sound bank first */

/* a track's record */
#define T_OUT 1                     /* where its events go: 0x40 the queue, 0x20 the SH4; 0x80 a part's channel */
#define T_ID 2
#define T_STATUS 3                  /* the running status */
#define T_BASE 12
#define T_LIST 16                   /* its list of parts, or none */
#define T_TIME 20                   /* 16.16 to its next event */
#define T_DATA 24
#define T_VOL 28
#define T_VOL_SENT 36
#define T_GROUP 40
#define T_RANK 41
#define NO_LIST 0xFF000000u
#define VOLUME 0xA01C0000u          /* the command that sets a track's volume */

/* the registers, from 0x800000 */
#define MIDI_IN 0x2808u
#define TIMER_B 0x2894u
#define SCIRE 0x28A4u
#define INTREQ 0x2D00u
#define INTCLEAR 0x2D04u

/* the track's flags */
#define T_ON 0x80
#define T_SLIDE 0x02
#define T_HELD 0x21

#define HANDLER_CYCLES 1024         /* aica.c's HANDLER_MAX */

static int mode;

static uint32_t rd32(uint32_t a) {
    uint32_t v;
    memcpy(&v, aica_ram + (a & aica_ram_mask), 4);
    return v;
}

static uint32_t rd8(uint32_t a) {
    return aica_ram[a & aica_ram_mask];
}

/* while a command runs in C, what it changes is noted so the interpreter can have it instead: the RAM's old
   bytes, and its register writes held back to the end */
static int noting;
static jmp_buf not_c;               /* to give the command up */
static struct {
    uint32_t a;
    uint8_t v;
} old[2048];
static int old_n;
static struct {
    uint32_t off, v, size;
} held[256];
static int held_n;

static void give_up(void) {
    longjmp(not_c, 1);
}

static void note_old(uint32_t a, int n) {
    if (old_n + n > (int)(sizeof old / sizeof old[0])) {
        give_up();
    }
    while (n-- > 0) {
        old[old_n].a = a & aica_ram_mask;
        old[old_n++].v = aica_ram[a & aica_ram_mask];
        a++;
    }
}

/* what the channel watch reads (channels_watch), and its writes there of what it read */
#define RECORDS 0x0100u             /* 8 of 96 bytes */
#define SLOTS 0xC800u               /* 64 bytes for each channel */
#define WATCH (WORK + 0x110)        /* what channels_watch reads next, and from which slot and record */
static unsigned watch_ram;          /* writes there by anything else */
static int in_watch;

static inline void watched(uint32_t a, uint32_t n) {
    uint32_t o;
    a &= aica_ram_mask;
    if (in_watch || a >= SLOTS + 64 * 64 || (a >= WATCH + 3 && a + n <= SLOTS)) {
        return;
    }
    if (a + n > SLOTS) {
        o = (a - SLOTS) & 63;
        if (a < SLOTS || o == 0 || (o <= 19 && o + n > 19) || o + n > 64) {
            watch_ram++;            /* a slot's first byte or its 19th */
        }
    } else if (a < 0x400 ? a + n > RECORDS : (a <= WORK + 6 && a + n > WORK + 6) || a + n > WATCH) {
        watch_ram++;
    }
}

static void wr32(uint32_t a, uint32_t v) {
    if (noting) {
        note_old(a, 4);
    }
    watched(a, 4);
    memcpy(aica_ram + (a & aica_ram_mask), &v, 4);
}

static void wr8(uint32_t a, uint32_t v) {
    if (noting) {
        note_old(a, 1);
    }
    watched(a, 1);
    aica_ram[a & aica_ram_mask] = (uint8_t)v;
}

/* the ARM's ldr: a word from anywhere, turned as the CPU turns an unaligned one */
static uint32_t arm_ld32(uint32_t a) {
    uint32_t v, r = (a & 3) * 8;
    memcpy(&v, aica_ram + (a & ~3u & aica_ram_mask), 4);
    return r ? v >> r | v << (32 - r) : v;
}

static uint32_t rd16(uint32_t a) {
    uint16_t v;
    memcpy(&v, aica_ram + (a & aica_ram_mask), 2);
    return v;
}

static void add32(uint32_t a, uint32_t n) {
    wr32(a, rd32(a) + n);
}

static void chip_hold(uint32_t off, uint32_t v, uint32_t size) {
    if (held_n == (int)(sizeof held / sizeof held[0])) {
        give_up();
    }
    held[held_n].off = off;
    held[held_n].v = v;
    held[held_n++].size = size;
}

/* a register read, with the writes held back over it (the low two bytes of each word hold the register) */
static uint32_t chip_merged(uint32_t off, uint32_t v, uint32_t size) {
    int i;
    uint32_t b, a;
    for (i = 0; i < held_n; i++) {
        for (b = 0; b < held[i].size; b++) {
            a = held[i].off + b;
            if (a >= off && a < off + size && !(a & 2)) {
                v = (v & ~(0xFFu << (a - off) * 8)) | (held[i].v >> b * 8 & 0xFF) << (a - off) * 8;
            }
        }
    }
    return v;
}

static uint32_t chip_rd32(uint32_t off) {
    uint32_t v = aica_arm_read32(0x800000u + off);
    return held_n ? chip_merged(off, v, 4) : v;
}

static uint32_t chip_rd8(uint32_t off) {
    uint32_t v = aica_arm_read8(0x800000u + off);
    return held_n ? chip_merged(off, v, 1) : v;
}

static void chip_wr32(uint32_t off, uint32_t v) {
    if (noting) {
        chip_hold(off, v, 4);
    } else {
        aica_arm_write32(0x800000u + off, v);
    }
}

static void chip_wr8(uint32_t off, uint32_t v) {
    if (noting) {
        chip_hold(off, v & 0xFF, 1);
    } else {
        aica_arm_write8(0x800000u + off, v);
    }
}

/* a write to where a pointer in the driver's data points */
static void bus_wr8(uint32_t a, uint32_t v) {
    if (a >= 0x800000u) {
        chip_wr8(a - 0x800000u, v);
    } else {
        wr8(a, v);
    }
}

static void bus_wr32(uint32_t a, uint32_t v) {
    if (a >= 0x800000u) {
        chip_wr32(a - 0x800000u, v);
    } else {
        wr32(a, v);
    }
}

/* ---- the routines ---- */

/* the cycles the ARM code takes for what is done here, as the interpreter counts them */
static int cyc;

/* 0x6810: each tick, the playing tracks' time and volume slides; a tick owed is made up at once */
static void tracks_tick(void) {
    int32_t lag;
    add32(TRACK_TICK + 12, 1);
    lag = (int32_t)rd32(TRACK_TICK + 8);
    cyc += 13;
    if (lag > 0) {
        wr32(TRACK_TICK + 8, (uint32_t)(lag - 1));
        cyc += 10;
        return;
    }
    for (;;) {
        uint32_t t;
        cyc += 3 + 15 * 5 + 3;
        for (t = TRACKS; t < TRACKS + 16 * TRACK_SIZE; t += TRACK_SIZE) {
            uint32_t f = rd8(t);
            if (!(f & T_ON)) {
                cyc += 7;
                continue;
            }
            if (f & T_HELD) {
                cyc += 10;
                continue;
            }
            wr32(t + 20, rd32(t + 20) - rd32(t + 4));
            cyc += 21;
            if (f & T_SLIDE) {
                int32_t v = (int32_t)(rd32(t + 28) + rd32(t + 32));
                if (v < 0) {
                    v = 0;
                }
                if (v > 0x7F0000) {
                    v = 0x7F0000;
                }
                wr32(t + 28, (uint32_t)v);
                cyc += 10;
            }
        }
        cyc += 9;
        if (lag == 0) {
            return;
        }
        wr32(TRACK_TICK + 8, (uint32_t)(lag + 1));
        lag = 0;
    }
}

/* 0x0D1C: the ticks counted for the SH4, the level's peak kept, and every 50 ticks both handed over */
static void status_tick(void) {
    uint32_t level = rd32(WORK + 0x180), n;
    add32(WORK + 0x2C, 1);
    cyc += 20;
    if (level > rd32(SHARED + 0xAC)) {
        wr32(SHARED + 0xAC, level);
        cyc++;
    }
    n = rd8(WORK + 0x26);
    wr8(WORK + 0x26, n - 1);
    if (n != 1) {
        cyc += 8;
        return;
    }
    wr8(WORK + 0x26, 50);
    wr32(SHARED + 0xA8, rd32(WORK + 0x28));
    wr32(SHARED + 0xAC, rd32(WORK + 0x180));
    cyc += 19;
}

/* 0x0C40: the FIQ, from its vector to its last instruction. The timer's reloaded and counted; outside a tick
   already running, the tick runs with the interrupts open, so the FIQ still raised comes in again at once (and
   finds the tick running). The first sets up its stack */
static void fiq(int depth) {
    uint32_t kind;
    int sp_set = rd8(WORK + 8) != 1;
    if (sp_set) {
        wr8(WORK + 8, 1);
    }
    kind = chip_rd8(INTREQ);
    cyc += 6 + 13 + (sp_set ? 7 : 3) + 17;
    if (kind == 2) {
        chip_wr32(SCIRE, 8);        /* MIDI in, nothing there */
        cyc += 16;
    } else if (kind == 1) {
        chip_wr32(TIMER_B, 0xD4);
        add32(WORK + 0x20, 1);
        add32(WORK + 0x38, 1);
        cyc += 5 + 19;
        if (rd8(WORK + 3) != 1) {
            wr8(WORK + 3, 1);
            cyc += 7;
            while (arm7_fiq_line() && depth < 4) {
                fiq(depth + 1);
                cyc += 3;           /* its return */
            }
            cyc += 7;
            tracks_tick();
            cyc += 7;
            status_tick();
            wr8(WORK + 3, 0);
            cyc += 6;
        } else {
            cyc += 3;
        }
        chip_wr32(SCIRE, 0x80);
        cyc += 6;
    } else {
        cyc += 3;
    }
    chip_wr8(INTCLEAR, 1);
    chip_wr8(INTCLEAR, 1);
    chip_wr8(INTCLEAR, 1);
    chip_wr8(INTCLEAR, 1);
    cyc += 23;
}

/* the FIQ's handler but its last instruction here, that one and what the interpreter runs after it there */
static int fiq_run(void) {
    int sp_set = rd8(WORK + 8) != 1;
    cyc = 0;
    fiq(0);
    if (mode == 3) {
        arm7_fiq_return(SHARED, 0x800000u + 0x2800u, WORK, sp_set, FIQ_STACK);
        return cyc + 3;
    }
    return arm7_fiq_finish(0x0CA0, cyc, HANDLER_CYCLES, SHARED, 0x800000u + 0x2800u, WORK, sp_set, FIQ_STACK);
}

/* ---- where the RAM holds the driver ---- */

static const struct {
    uint16_t from, to;
} replaced[] = { { 0x001C, 0x0020 }, { 0x0C40, 0x0D64 }, { 0x1128, 0x1138 }, { 0x6810, 0x68B8 } };
#define REPLACED_SUM 0x8A4E841Du    /* FNV-1a of those words */

static int driver_ok(void) {
    static uint32_t seen = ~0u;
    static int ok;
    uint32_t now = arm7_code_changes(), h = 2166136261u, a;
    size_t i;
    if (now == seen) {
        return ok;
    }
    seen = now;
    for (i = 0; i < sizeof replaced / sizeof replaced[0]; i++) {
        for (a = replaced[i].from; a < replaced[i].to; a += 4) {
            h = (h ^ rd32(a)) * 16777619u;
        }
    }
    ok = h == REPLACED_SUM;
    return ok;
}

/* ---- the check: the interpreter, then this from the same state ---- */

#define CHECK_RAM 0x10000u          /* the driver's code and areas */
#define STACKS_FROM 0xB000u         /* its stacks, left out: what is pushed there is dead once popped */
#define STACKS_TO 0xB200u
#define LOG_N 256

typedef struct {
    uint32_t off, v, mask;
} WRITE;

static WRITE* log_now;
static int log_n;
static WRITE log_i[LOG_N], log_c[LOG_N];
static int n_i, n_c;
static uint8_t *ram0, *ram_i;
static AICA_STATE *chip0, *chip_i;
static unsigned long checked, wrong;

static void logged(uint32_t off, uint32_t v, uint32_t mask) {
    if (log_n < LOG_N) {
        log_now[log_n].off = off;
        log_now[log_n].v = v & mask;
        log_now[log_n].mask = mask;
    }
    log_n++;
}

static void report(const char* what, unsigned long a, unsigned long x, unsigned long y) {
    wrong++;
    if (wrong <= 20) {
        host_log("sound driver check: FIQ %lu wrong: %s %lx, interpreter %lx, C %lx", checked, what, a, x, y);
    }
}

static int check_ready(void) {
    if (ram0 == NULL) {
        ram0 = malloc(CHECK_RAM);
        ram_i = malloc(CHECK_RAM);
        chip0 = aica_state_new();
        chip_i = aica_state_new();
    }
    return ram0 != NULL && ram_i != NULL && chip0 != NULL && chip_i != NULL;
}

/* the first byte of the driver's RAM the two runs left apart, or CHECK_RAM */
static uint32_t ram_differs(void) {
    uint32_t a;
    for (a = 0; a < CHECK_RAM; a++) {
        if ((a < STACKS_FROM || a >= STACKS_TO) && ram_i[a] != aica_ram[a]) {
            break;
        }
    }
    return a;
}

/* the first register write the two runs made apart, or -1 */
static int writes_differ(void) {
    int i;
    if (n_i != n_c) {
        return n_i < n_c ? n_i : n_c;
    }
    for (i = 0; i < n_i && i < LOG_N; i++) {
        if (memcmp(&log_i[i], &log_c[i], sizeof log_i[i])) {
            return i;
        }
    }
    return -1;
}

static void fiq_checked(void) {
    ARM7_REGS before, after_i, after_c;
    uint32_t a;
    int i, ran_i, ran_c;
    if (!check_ready()) {
        (void)fiq_run();
        return;
    }
    arm7_get_regs(&before);
    aica_state_save(chip0);
    memcpy(ram0, aica_ram, CHECK_RAM);
    log_now = log_i;
    log_n = 0;
    aica_write_log = logged;
    ran_i = arm7_run_fiq(HANDLER_CYCLES);
    aica_write_log = NULL;
    n_i = log_n;
    arm7_get_regs(&after_i);
    aica_state_save(chip_i);
    memcpy(ram_i, aica_ram, CHECK_RAM);

    arm7_set_regs(&before);
    aica_state_load(chip0);
    memcpy(aica_ram, ram0, CHECK_RAM);
    log_now = log_c;
    log_n = 0;
    aica_write_log = logged;
    ran_c = fiq_run();
    aica_write_log = NULL;
    n_c = log_n;
    arm7_get_regs(&after_c);
    checked++;

    if ((after_i.cpsr & 0x1F) == 0x11) {
        report("the interpreter still in the handler, cpsr", 0, after_i.cpsr, after_c.cpsr);
    } else if (ran_i != ran_c) {
        report("cycles", 0, (unsigned long)ran_i, (unsigned long)ran_c);
    } else if (memcmp(&after_i, &after_c, sizeof after_i)) {
        const uint32_t *x = (const uint32_t*)&after_i, *y = (const uint32_t*)&after_c;
        for (i = 0; x[i] == y[i]; i++) {
        }
        report("CPU state word", (unsigned long)i, x[i], y[i]);
    } else if (n_i != n_c) {
        report("register writes, count", 0, (unsigned long)n_i, (unsigned long)n_c);
    } else {
        for (i = 0; i < n_i && i < LOG_N; i++) {
            if (memcmp(&log_i[i], &log_c[i], sizeof log_i[i])) {
                report("register write at", log_i[i].off, log_i[i].v, log_c[i].v);
                break;
            }
        }
        if (i == n_i || i == LOG_N) {
            for (a = 0; a < CHECK_RAM; a++) {
                if ((a < STACKS_FROM || a >= STACKS_TO) && ram_i[a] != aica_ram[a]) {
                    report("RAM byte at", a, ram_i[a], aica_ram[a]);
                    break;
                }
            }
            if (a == CHECK_RAM) {
                if ((checked & 4095) == 0) {
                    host_log("sound driver check: %lu FIQs checked, %lu wrong", checked, wrong);
                }
                return;
            }
        }
    }
    arm7_set_regs(&after_i);            /* the interpreter's kept */
    aica_state_load(chip_i);
    memcpy(aica_ram, ram_i, CHECK_RAM);
}

/* ---- the main loop ---- */

#define PASS 0x05ECu                /* its top: each pass calls the same routines in turn */
#define REAL_RATE 512               /* the sound CPU's cycles a sample */
#define IDLE_PASS 780               /* and a pass's with nothing to do (15 a tick, measured) */

static jmp_buf not_here;
static uint32_t pass_sp;            /* the main loop's stack */
static uint32_t missing;            /* the last routine a pass left to the interpreter */
static int pass_arm;                /* this pass has left some to it */

static void note_missing(void);
static unsigned long m3_calls, m3_passes, m3_idle, m3_arm;   /* mode 3's: what ran natively, what interpreted */
static unsigned long m3_capped;     /* segments that hit PASS_CAP with work still waiting */
static unsigned long m3_watched;    /* the channel watch's idle steps, and those that repeated */
static unsigned long m3_repeats;
#define PASS_CAP 64                 /* a segment's busy passes at most (about 190 a frame at chunk 256) */

/* the rest of the pass left to the interpreter, from the main loop's call at `site` */
static void hand_off(uint32_t site) {
    ARM7_REGS s;
    arm7_get_regs(&s);
    s.r[12] = WORK;
    s.r[13] = pass_sp;
    s.r[15] = site;
    arm7_set_regs(&s);
    missing = site;
    longjmp(not_here, 1);
}

/* a routine with no C yet run by the interpreter, called with r0 = arg and the stack at sp, as from the call
   that returns to `ret` */
static void arm_call(uint32_t at, uint32_t ret, uint32_t sp, uint32_t arg) {
    ARM7_REGS s;
    int n = 0;
    arm7_get_regs(&s);
    s.r[0] = arg;
    s.r[12] = WORK;
    s.r[13] = sp - 4;
    s.r[15] = at;
    wr32(sp - 4, ret);
    arm7_set_regs(&s);
    do {
        n += arm7_run(1);
    } while (arm7_pc() != ret && n < (1 << 20));
    m3_arm += (unsigned long)n;
    arm7_get_regs(&s);
    s.r[13] = pass_sp;
    s.r[15] = PASS;
    arm7_set_regs(&s);
    missing = at;
    note_missing();
    pass_arm = 1;
}

/* the commands, from the SH4's mailbox and from the sequences, go through a queue and a few run each pass */
#define QUEUE 0xB400u               /* 256 words */
#define Q_HEAD (WORK + 0x10)
#define Q_TAIL (WORK + 0x14)
#define Q_COUNT (WORK + 0x180)
#define MAILBOX 0x0400u             /* 64 words from the SH4, big-endian; SHARED + 4 the next */
#define REPLIES 0xDA00u             /* 63 words to the SH4 */
#define PATCHES 0xB800u             /* of 64 bytes */
#define PITCH_UP 0x7710u            /* an octave of pitches, from the note up and down */
#define PITCH_DOWN 0x7D10u

/* a channel's registers, from 0x800000 */
#define CH(c) ((uint32_t)(c) * 0x80)
#define C_LPCTL 0x200               /* in its first word: the sample loops */
#define C_LOOP_START 0x08
#define C_PITCH 0x18
#define MSLC 0x280D                 /* the channel the next two describe */
#define STATUS 0x2810               /* its envelope */
#define POSITION 0x2814

/* 0x0674: passes counted, for the SH4 */
static void pass_count(void) {
    add32(SHARED, 1);
}

/* 0x27F0: a command queued, unless the queue is full */
static void queue_put(uint32_t cmd) {
    uint32_t n = rd32(Q_COUNT), at;
    if (n == 256) {
        return;
    }
    at = rd32(Q_HEAD);
    wr32(QUEUE + at, cmd);
    wr32(Q_COUNT, n + 1);
    wr32(Q_HEAD, at + 4 == 1024 ? 0 : at + 4);
}

/* 0x27B8 */
static int queue_get(uint32_t* cmd) {
    uint32_t n = rd32(Q_COUNT), at;
    if (n == 0) {
        return 0;
    }
    at = rd32(Q_TAIL);
    *cmd = rd32(QUEUE + at);
    wr32(Q_COUNT, n - 1);
    wr32(Q_TAIL, at + 4 == 1024 ? 0 : at + 4);
    return 1;
}

/* 0x28C8: a word for the SH4, unless its queue is full */
static void reply(uint32_t v) {
    uint32_t at = rd8(WORK + 0xC8), n = rd8(WORK + 0xCA);
    if (n == 0xFC) {
        return;
    }
    wr32(REPLIES + at, v);
    wr8(WORK + 0xCA, n + 4);
    wr8(WORK + 0xC8, at + 4);
}

#define CMD (WORK + 0xE0)           /* the command running */

static void note_off(uint32_t cmd);
static void note_on(uint32_t cmd);
static void controller(uint32_t cmd);
static void program_change(uint32_t cmd);
static void pitch_bend(void);
static void driver_command(uint32_t cmd);
static uint32_t pitch_reg(int32_t p);
static uint32_t pitch_slide(uint32_t slot, uint32_t ch, uint32_t reg);
static void record_level(uint32_t cmd, int second);

/* fn(arg) in C, its chip writes held to the end; or, if it gives up, all it did undone and -1 */
static int journaled(int (*fn)(uint32_t), uint32_t arg) {
    int i, r;
    noting = 1;
    old_n = held_n = 0;
    if (setjmp(not_c) != 0) {
        noting = 0;
        for (i = old_n; i-- > 0;) {
            aica_ram[old[i].a] = old[i].v;
        }
        held_n = 0;
        return -1;
    }
    r = fn(arg);
    noting = 0;
    for (i = 0; i < held_n; i++) {
        if (held[i].size == 4) {
            aica_arm_write32(0x800000u + held[i].off, held[i].v);
        } else {
            aica_arm_write8(0x800000u + held[i].off, held[i].v);
        }
    }
    held_n = 0;
    return r;
}

static int command_c(uint32_t cmd) {
    switch (cmd >> 28) {
    case 0x8:
        note_off(cmd);
        break;
    case 0x9:
        note_on(cmd);
        break;
    case 0xA:
        driver_command(cmd);
        break;
    case 0xB:
        controller(cmd);
        break;
    case 0xC:
        program_change(cmd);
        break;
    case 0xD:
        break;
    case 0xE:
        pitch_bend();
        break;
    default:
        give_up();
    }
    return 1;
}

/* 0x41C4: a command, by its top byte: 0x80-0xEF as MIDI's (the low nibble the channel, the low byte the
   track), 0xA2 a record's level, the others the driver's own. What has no C yet runs in the interpreter */
static void command(uint32_t cmd, int left) {
    ARM7_REGS s;
    wr32(CMD, cmd);
    if (cmd >> 28 < 8 || journaled(command_c, cmd) >= 0) {
        return;
    }
    arm7_get_regs(&s);
    s.r[8] = (uint32_t)left;
    arm7_set_regs(&s);
    wr32(pass_sp - 4, 0x0604);      /* the stack as the ARM code has it */
    wr32(pass_sp - 8, (uint32_t)left);
    arm_call(0x41C4, 0x06C4, pass_sp - 8, cmd);
}

/* 0x0688: up to WORK[2] + 1 commands */
static void run_commands(void) {
    int n = (int)rd8(WORK + 2);
    uint32_t cmd;
    do {
        if (!queue_get(&cmd)) {
            return;
        }
        command(cmd, n);
    } while (--n >= 0);
}

#define SOUNDS 44                   /* in a bank: where its table of sounds is */

/* 0x71CC: a sound (by w's top 16 bits) looked for in each track's bank, and its variation (bits 8-15) started on
   a free track record; 1 if it was */
static int sound_start(uint32_t w) {
    uint32_t t, bank, table, n, k, e, sub, v = w >> 8 & 0xFF, off, data, f, rec;
    for (t = 0; t < 8; t++) {
        bank = rd32(TRACK_PARAMS + t * 32);
        if (bank == 0) {
            continue;
        }
        table = arm_ld32(bank + SOUNDS) + bank;
        n = rd8(table);
        for (k = 0; k <= n; k++) {
            e = arm_ld32(table + 4 + k * 4);
            if (e >> 16 == w >> 16) {
                goto found;
            }
        }
    }
    return 0;
found:
    sub = table + (e & 0xFFFF);
    if (rd8(sub) < v || (off = arm_ld32(sub + 4 + v * 4)) == 0) {
        return 0;
    }
    data = table + off;
    f = rd8(data);
    if (!(f & 0x80) || !(rd8(data + 1) & 0x80) || (f & 0x10)) {
        give_up();                  /* 0x7354, 0x7278, 0x7324 */
    }
    for (rec = TRACKS; rd8(rec) != 0; rec += TRACK_SIZE) {
        if (rec == TRACKS + 15 * TRACK_SIZE) {
            give_up();              /* 0x7418 */
        }
    }
    wr8(rec + T_OUT, f);
    wr8(rec + T_ID, t);
    wr32(rec + 4, 0x10000);
    wr32(rec + 8, w & 0x0FFFFFFF);
    wr32(rec + T_TIME, 0);
    wr8(rec + T_GROUP, 0);
    wr8(rec + T_RANK, 0);
    wr8(rec, 0x80);
    wr32(rec + T_LIST, NO_LIST);
    wr32(rec + T_DATA, data + 1);
    return 1;
}

/* 0x6DBC: track records stopped, by the second byte; 0x80 in the low byte narrows it to the track (the low
   three bits) */
static void tracks_stop(uint32_t w) {
    uint32_t rec, op = w >> 8 & 0xFF, mine = w & 0x80, part, same, stop;
    for (rec = TRACKS; rec < TRACKS + 16 * TRACK_SIZE; rec += TRACK_SIZE) {
        part = rd8(rec + T_OUT) & 0x80;
        same = rd8(rec + T_ID) == (w & 7);
        switch (op) {
        case 0x01:                  /* 0x6E3C: all, or all but the track's own */
            if (!mine) {
                wr32(rec, 0);
                continue;
            }
            stop = !same || part;
            break;
        case 0x02:                  /* 0x6E74 */
            stop = !part && (!mine || !same);
            break;
        case 0x03:                  /* 0x6EBC */
            stop = part;
            break;
        case 0x11:                  /* 0x6ED8 */
            stop = same && (!mine || part);
            break;
        case 0x12:                  /* 0x6F20 */
            stop = !mine && same && !part;
            break;
        default:                    /* 0x6F48 */
            stop = same && part;
            break;
        }
        if (stop) {
            wr8(rec, 0);
        }
    }
}

/* 0x6D6C with 0xA0: the driver's own messages; most do nothing here */
static int driver_message(uint32_t w) {
    uint32_t sub = w >> 16 & 0xFF, op = w >> 8 & 0xFF;
    if (sub > 10 || sub == 1 || sub == 4 || sub == 5 || sub == 7 || sub == 8) {
        return 0;
    }
    if (sub != 0) {
        give_up();                  /* 0x7028, 0x7078, 0x70C8, 0x6F70, 0x6FA8 */
    }
    if (op == 0x18 || op == 0x19) {
        give_up();                  /* 0x70C8 */
    }
    if ((op >= 1 && op <= 3) || (op >= 0x11 && op <= 0x13)) {
        tracks_stop(w);
    }
    return 0;
}

static void tracks(void);

/* 0x6D6C: a message of the driver's own (0xA0), or a sound started (0xA8-0xAD) and the tracks run at once */
static void long_message(uint32_t w, uint32_t at) {
    ARM7_REGS s;
    int r = journaled(w >> 24 == 0xA0 ? driver_message : sound_start, w);
    if (r >= 0) {
        if (r) {
            tracks();
        }
        return;
    }
    arm7_get_regs(&s);
    s.r[5] = at;
    s.r[6] = MAILBOX;
    s.r[7] = SHARED;
    arm7_set_regs(&s);
    wr32(pass_sp - 4, 0x0610);
    arm_call(0x6D6C, 0x07C8, pass_sp - 20, w);
}

/* 0x07E0: the SH4's messages echoed to it */
static void echo(void) {
    uint32_t b = rd8(SHARED + 12);
    if (rd8(SHARED + 15) == 0xA0 && b != rd8(WORK + 9)) {
        wr8(WORK + 9, b);
        reply(0xA01A0000u | b << 8);
    }
    reply(rd32(SHARED + 12));
}

/* 0x0710: the mailbox emptied into the queue while there is room */
static void mailbox(void) {
    uint32_t at = rd32(SHARED + 4), w, status;
    while (rd32(Q_COUNT) != 256 && (w = rd32(MAILBOX + at)) != 0) {
        wr32(SHARED + 8, w);
        wr32(MAILBOX + at, 0);
        at = at + 4 == 256 ? 0 : at + 4;
        wr32(SHARED + 4, at);
        wr8(SHARED + 15, rd8(SHARED + 8));
        wr8(SHARED + 14, rd8(SHARED + 9));
        wr8(SHARED + 13, rd8(SHARED + 10));
        wr8(SHARED + 12, rd8(SHARED + 11));
        w = rd32(SHARED + 12);
        status = rd8(SHARED + 15);
        if (status == 0xA0) {
            queue_put(w | 0x40);
            long_message(w, at);
        } else if (status >= 0xA8 && status <= 0xAD) {
            long_message(w, at);
        } else {
            queue_put(w);
        }
        if (rd8(WORK + 1) == 1) {
            echo();
        }
    }
}

/* 0x0838 and 0x08BC: two jobs the SH4 can start */
static void jobs(void) {
    uint32_t state = rd8(WORK + 10);
    if ((state == 1 && rd32(SHARED + 0x70) == 0) || (state == 2 && rd32(SHARED + 0x70) != 0)) {
        hand_off(0x0610);
    }
    state = rd8(WORK + 11);
    if (state == 1 || state == 2) {
        hand_off(0x061C);
    }
}

/* 0x0974: the SH4 asking the driver to wait */
static void sh4_wait(void) {
    if (rd8(0xDD00) != 0) {
        hand_off(0x0628);
    }
}

/* 0x2A64 */
static void key_off(uint32_t ch) {
    chip_wr8(ch + 1, (chip_rd8(ch + 1) & 7) | 0x80);
}

#define LISTS 0xB304u              /* the slots in use, as three lists: a count, the first and the last */
#define FREE 0xDDC0u                /* a ring of the free slots */

/* 0x2A94: a channel's registers set to silence */
static void channel_clear(uint32_t ch) {
    chip_wr8(ch + 41, 0xFF);
    chip_wr8(ch + 40, 0x20);
    chip_wr32(ch + 64, 0x1F1F);
    chip_wr32(ch + 68, 0x1F1F);
    chip_wr32(ch + 20, 0x3C1F);
    chip_wr8(ch + 1, 0x80);
    chip_wr32(ch + 0, 0);
    chip_wr32(ch + 4, 0);
    chip_wr32(ch + 8, 0);
    chip_wr32(ch + 12, 0);
    chip_wr32(ch + 24, 0);
    chip_wr32(ch + 28, 0);
    chip_wr32(ch + 32, 0);
    chip_wr32(ch + 36, 0);
}

/* 0x2CC8: the list a slot is on */
static uint32_t slot_list(uint32_t slot) {
    uint32_t f = rd8(slot);
    return f & 0x40 ? LISTS + 4 : f & 0x20 ? LISTS + 8 : LISTS;
}

/* 0x2348: a slot taken off its list; each slot has the one before it and the one after (0xFF none) */
static void list_remove(uint32_t list, uint32_t s) {
    uint32_t n = rd8(list) - 1, before, after;
    wr8(list, n);
    if ((n & 0xFF) == 0) {
        return;
    }
    before = rd8(SLOTS + s * 64 + 16);
    after = rd8(SLOTS + s * 64 + 17);
    if (rd8(list + 1) == s) {
        wr8(SLOTS + after * 64 + 16, 0xFF);
        wr8(list + 1, after);
    } else if (rd8(list + 2) == s) {
        wr8(SLOTS + before * 64 + 17, 0xFF);
        wr8(list + 2, before);
    } else {
        wr8(SLOTS + before * 64 + 17, after);
        wr8(SLOTS + after * 64 + 16, before);
    }
}

/* 0x2BDC */
static void slot_free(uint32_t s) {
    uint32_t at = rd8(WORK + 0x101);
    wr8(FREE + at, s);
    wr8(WORK + 0x101, (at + 1) & 63);
    wr8(WORK + 0x100, rd8(WORK + 0x100) - 1);
}

/* 0x1B74: a slot whose sound has ended let go */
static void channel_end(uint32_t slot, uint32_t s) {
    uint32_t f = rd8(slot), p;
    if (!(f & 0x80)) {
        return;
    }
    if ((f & 12) == 12 && (p = rd32(slot + 60)) != 0) {
        bus_wr32(p, 0xFFFF0000u);
    }
    channel_clear(CH(s));
    list_remove(slot_list(slot), s);
    wr8(slot, 0);
    slot_free(s);
}

static void channel_ended(uint32_t s) {
    channel_end(SLOTS + s * 64, s);
}

/* 0x22D8: a slot put last on a list */
static void list_add(uint32_t list, uint32_t s) {
    uint32_t n = rd8(list), last, me = SLOTS + s * 64;
    wr8(list, n + 1);
    if (n == 0) {
        wr8(me + 16, 0xFF);
        wr8(me + 17, 0xFF);
        wr8(list + 1, s);
        wr8(list + 2, s);
        return;
    }
    last = rd8(list + 2);
    wr8(me + 17, 0xFF);
    wr8(me + 16, last);
    wr8(SLOTS + last * 64 + 17, s);
    wr8(list + 2, s);
}

#define EACH (WORK + 0xE8)          /* the ARM routine an iterator calls */

/* 0x2664: fn (at in the ARM code) for each playing slot (not 0x04 or 0x10) whose note matches the command where
   mask is set */
static void for_slots(void (*fn)(uint32_t s), uint32_t at, uint32_t mask) {
    uint32_t s, f;
    wr32(EACH, at);
    for (s = 0; s < 64; s++) {
        f = rd8(SLOTS + s * 64);
        if ((f & 0x80) && !(f & 0x14) && (mask == 0 || (rd32(SLOTS + s * 64 + 4) & mask) == (rd32(CMD) & mask))) {
            fn(s);
        }
    }
}

#define LAYERS 0xD800u              /* for each track (8) and channel (16): up to 4 patches, 0xFF the end */

/* 0x25A0 with 0x2C3C: fn for each patch the command's track and channel play */
static void for_layers(void (*fn)(uint32_t cmd, uint32_t patch), uint32_t fn_at) {
    uint32_t cmd = rd32(CMD), at = LAYERS + (cmd & 7) * 64 + (cmd >> 24 & 15) * 4, k, p;
    wr32(EACH, fn_at);
    for (k = 0; k < 4 && (p = rd8(at + k)) != 0xFF; k++) {
        fn(rd32(CMD), p);
    }
}

/* 0x32D4: a slot's note let go: keyed off and moved to the released list, unless the pedal holds it (0x01) */
static void slot_note_off(uint32_t s) {
    uint32_t slot = SLOTS + s * 64, f = rd8(slot);
    if (f & 8) {
        return;
    }
    if (f & 1) {
        wr8(slot, f | 2);
        return;
    }
    if (f & 0x20) {
        return;
    }
    wr8(slot, f | 0x20);
    key_off(CH(s));
    if (!(rd8(slot) & 0x40)) {      /* 0x21CC */
        list_remove(LISTS, s);
        list_add(LISTS + 8, s);
    }
}

/* 0x340C: a note taken off a patch's held notes (up to 4, at 40-43; 36 their count) */
static void held_remove(uint32_t patch, uint32_t note) {
    uint32_t b0 = rd8(patch + 40), b1 = rd8(patch + 41), b2 = rd8(patch + 42), b3 = rd8(patch + 43);
    if (b3 != note) {
        if (b2 == note) {
            wr8(patch + 42, b3);
        } else if (b1 == note) {
            wr8(patch + 41, b2);
            wr8(patch + 42, b3);
        } else if (b0 == note) {
            wr8(patch + 40, b1);
            wr8(patch + 41, b2);
            wr8(patch + 42, b3);
        } else {
            return;
        }
    }
    wr8(patch + 43, 0xFF);
    b0 = rd8(patch + 36);
    wr8(patch + 36, b0 != 0 ? b0 - 1 : 0);
}

/* 0x3324: for a patch played one note at a time (0xC0C0 at 32), the note let go and the one still held
   played again */
static void patch_note_off(uint32_t cmd, uint32_t p) {
    uint32_t patch = PATCHES + p * 64, note = cmd >> 16 & 0xFF, again;
    if (!(rd32(patch + 32) & 0xC0C0)) {
        return;
    }
    held_remove(patch, note);       /* 0x3360 */
    if (rd8(patch + 37) != note || rd8(patch + 36) == 0) {
        return;
    }
    again = rd8(patch + 40);
    queue_put(0xA0003800u);
    queue_put((cmd & 0x0F0000FFu) | 0x90000000u | rd8(patch + 39) << 8 | again << 16);
}

/* 0x32AC: note off */
static void note_off(uint32_t cmd) {
    (void)cmd;
    for_slots(slot_note_off, 0x32D4, 0x0F7F00F7u);
    for_layers(patch_note_off, 0x3324);
}

#define LEVELS 0x750Cu              /* 128 levels' attenuations, inverted */

/* 0x3130: v + d - 64, kept to 0-max */
static uint32_t offset_level(uint32_t v, uint32_t d, uint32_t max) {
    int32_t x = (int32_t)(v + d - 64);
    if (x < 0) {
        x = 0;
    }
    return (uint32_t)x > max ? max : (uint32_t)x;
}

/* 0x2FD8 */
static uint32_t attenuation(uint32_t level) {
    return rd8(LEVELS + level) ^ 0xFF;
}

/* 0x5244 and 0x52B8 (0xA2, from 0x532C): a record's level, or its second's, and its channel's total level */
static uint32_t record_atten(uint32_t rec, uint32_t v);

static void record_level(uint32_t cmd, int second) {
    uint32_t rec = RECORDS + (cmd >> 20 & 7) * 96, v = cmd >> 8 & 0xFF, ch = rd32(rec + (second ? 8 : 4)), tl;
    wr8(rec + (second ? 0x55 : 0x51), v);
    tl = record_atten(rec, v);
    if (ch != 0 && ch + 0x100000 != 0) {
        bus_wr8(ch + 0x100000 + 41, tl);
    }
}

/* 0x5558: a record's level v with its offset (at 1), as an attenuation */
static uint32_t record_atten(uint32_t rec, uint32_t v) {
    return attenuation(offset_level(v, rd8(rec + 1), 127));
}

/* 0x55DC: a record's loop end for its sample format (0 16-bit, 1 8-bit, 2 ADPCM) */
static uint32_t record_loop_end(uint32_t fmt) {
    uint32_t n = rd32(SHARED + 104) - 64;
    return fmt == 1 ? n : fmt == 0 ? n >> 1 : n << 1;
}

/* 0x557C with 0x3118: a record's pitch register, from its coarse (64) and fine (65) tuning and its
   frequency step (66) */
static uint32_t record_pitch(uint32_t rec) {
    int32_t p = ((int32_t)(rd8(rec + 64) << 24) >> 16) + ((int32_t)(rd8(rec + 65) << 24) >> 22);
    uint32_t r = pitch_reg(p) + (arm_ld32(rec + 66) & 0xFFFF);
    if (r & 0x400) {
        r = (r + 0x800) & 0x7BFF;
    }
    return r;
}

/* 0x54DC's part of 0x53B8: a record channel's pan, send and level, from the record at `at` */
static void record_pan(uint32_t rec, uint32_t ch, uint32_t at) {
    chip_wr8(ch + 37, rd8(rec + at));
    chip_wr8(ch + 36, rd8(rec + at + 2));
    chip_wr8(ch + 32, rd8(rec + at + 3));
    chip_wr8(ch + 41, record_atten(rec, rd8(rec + at + 1)));
}

/* 0x53B8: a record's channel set to play the sample at a, keyed on with the next key-on */
static void record_channel(uint32_t rec, uint32_t ch, uint32_t a) {
    uint32_t fmt = rd8(rec + 40) & 3, x = a | fmt << 23 | 0x2000000, k;
    chip_wr32(ch + 4, x & 0xFFFF);
    chip_wr32(ch + 0, x >> 16);
    chip_wr32(ch + 8, 0);
    chip_wr32(ch + 12, record_loop_end(fmt));
    chip_wr32(ch + 24, record_pitch(rec));
    chip_wr32(ch + 16, arm_ld32(rec + 68) & 0xFFFF);
    chip_wr32(ch + 20, arm_ld32(rec + 70) & 0xFFFF);
    chip_wr32(ch + 64, arm_ld32(rec + 58) & 0xFFFF);
    chip_wr32(ch + 68, arm_ld32(rec + 60) & 0xFFFF);
    if (!(rd8(rec + 63) & 1)) {
        for (k = 0; k < 5; k++) {
            chip_wr32(ch + 44 + k * 4, arm_ld32(rec + 48 + k * 2) & 0xFFFF);
        }
        chip_wr8(ch + 40, rd8(rec + 62));
    } else {
        chip_wr8(ch + 40, 0x20);
    }
    record_pan(rec, ch, 80);
    chip_wr8(ch + 1, chip_rd8(ch + 1) | 0x40);
}

static int slot_alloc(uint32_t flags, uint32_t* s);

/* 0x1BF0: a slot taken for a record's channel, cleared and put on the second list */
static int record_slot(uint32_t flags, uint32_t b3, uint32_t b18, uint32_t* s) {
    uint32_t slot, k;
    if (!slot_alloc(1, s)) {
        return 0;
    }
    slot = SLOTS + *s * 64;
    for (k = 0; k < 64; k += 4) {   /* 0x2790 */
        wr32(slot + k, 0);
    }
    list_add(LISTS + 4, *s);
    wr8(slot, flags | 0xC0);
    wr8(slot + 22, *s);
    wr8(slot + 3, b3);
    wr8(slot + 18, b18);
    return 1;
}

/* 0x4F70 (0xA1 x1): a record started on one channel, or two (41 & 3 == 1), keyed on together */
static void record_start(uint32_t cmd) {
    uint32_t rec = RECORDS + (cmd >> 20 & 7) * 96, s, ch;
    if (rd8(rec) & 0x80) {
        return;
    }
    wr8(rec, rd8(rec) | 0x80);
    if (!record_slot(16, rd8(rec + 43), rd8(rec + 24), &s)) {
        return;
    }
    wr8(rec + 2, s);
    ch = CH(s);
    wr32(rec + 4, 0x700000 + ch);
    record_channel(rec, ch, rd32(rec + 12));
    if ((rd8(rec + 41) & 3) != 1) {
        wr8(rec + 3, 0xFF);
        wr32(rec + 8, 0);
    } else {
        if (!record_slot(16, rd8(rec + 43), rd8(rec + 24), &s)) {
            return;
        }
        wr8(rec + 3, s);
        ch = CH(s);
        wr32(rec + 8, 0x700000 + ch);
        record_channel(rec, ch, rd32(rec + 16));
        record_pan(rec, ch, 84);    /* 0x54DC */
    }
    chip_wr8(ch + 1, chip_rd8(ch + 1) | 0xC0);
}

/* 0x5060 (0xA1 x2): a record stopped and its channels ended */
static void record_stop(uint32_t cmd) {
    uint32_t rec = RECORDS + (cmd >> 20 & 7) * 96, s;
    if (!(rd8(rec) & 0x80)) {
        return;
    }
    wr8(rec, rd8(rec) & 0x7F);
    if ((s = rd8(rec + 2)) != 0xFF) {   /* 0x50BC */
        channel_end(SLOTS + s * 64, s);
    }
    if ((s = rd8(rec + 3)) != 0xFF) {
        channel_end(SLOTS + s * 64, s);
    }
    wr32(rec + 4, 0);
    wr32(rec + 8, 0);
    wr32(rec + 20, 0);
    wr8(rec + 2, 0);
    wr8(rec + 3, 0);
}

/* 0x4F60 (0xA1 x0) with 0x5514 and 0x18A0: the records' buffer size from the SH4's settings, and the room left */
static void record_sizes(void) {
    uint32_t x;
    wr32(WORK + 140, (rd32(SHARED + 96) + rd32(SHARED + 100) * 2) * rd32(SHARED + 104));
    x = rd32(WORK + 132) - rd32(WORK + 128) - rd32(WORK + 144);
    wr32(SHARED + 76, x - rd32(WORK + 136));
    wr32(SHARED + 72, x - rd32(WORK + 140));
}

#define STARTED 0xE400u             /* the slots a note on started, keyed on together at its end */

/* what a note on is working with */
typedef struct {
    uint32_t patch, split, voice, trk;
} VOICE;

/* 0x2BA0: a free slot taken, if there is one */
static int slot_take(uint32_t* s) {
    uint32_t n = rd8(WORK + 0x100), at;
    if (n == 64) {
        return 0;
    }
    wr8(WORK + 0x100, n + 1);
    at = rd8(WORK + 0x102);
    *s = rd8(FREE + at);
    wr8(FREE + at, 0xFF);
    wr8(WORK + 0x102, (at + 1) & 63);
    return 1;
}

/* 0x1C74: a slot for a new voice: a free one, or the oldest released, or the oldest playing */
static int slot_alloc(uint32_t flags, uint32_t* s) {
    if ((flags & 1) && rd8(WORK + 0x108) >= rd8(WORK + 0x114)) {
        give_up();                  /* 0x1D68 */
    }
    if (slot_take(s)) {
        return 1;
    }
    if (rd8(LISTS + 8) != 0) {      /* 0x1D18 */
        *s = rd8(LISTS + 9);
        list_remove(LISTS + 8, *s);
    } else if (rd8(LISTS) != 0) {
        *s = rd8(LISTS + 1);
        list_remove(LISTS, *s);
    } else {
        return 0;
    }
    channel_clear(CH(*s));
    if (rd8(SLOTS + *s * 64) & 4) {
        give_up();                  /* 0x66D4 */
    }
    return 1;
}

/* 0x2F5C: v moved by amount (signed) scaled by depth, kept to 0-max */
static uint32_t modulated(uint32_t v, uint32_t amount, uint32_t depth, uint32_t max) {
    int32_t d;
    depth = depth ? depth + 1 : 0;
    if (amount & 0x80) {
        d = (int32_t)(int8_t)amount * (int32_t)max >> 7;
        d = d * (int32_t)depth >> 7;
        d += (int32_t)v;
        return d < 0 ? 0 : (uint32_t)d;
    }
    amount = amount ? amount + 1 : 0;
    v += (amount * max >> 7) * depth >> 7;
    return v > max ? max : v;
}

/* 0x4CB8: one of a split's modulations put on the channel's registers */
static void modulation(const VOICE* v, uint32_t ch, uint32_t kind, uint32_t amount, uint32_t depth) {
    uint32_t r;
    kind &= 31;
    if (kind == 0 || amount == 0) {
        return;
    }
    if (kind == 10 || kind == 16) {
        give_up();
    }
    if (kind == 15) {               /* 0x4DA0 */
        r = rd8(v->voice + 48);
        chip_wr8(ch + 28, (r & 31) | (modulated(r >> 5 & 7, amount, depth, 7) & 7) << 5);
    }
}

/* 0x4C54: the split's four modulations, two by each of the patch's depths */
static void modulations(const VOICE* v, uint32_t ch) {
    modulation(v, ch, rd8(v->split + 16), rd8(v->split + 17), rd8(v->patch + 20));
    modulation(v, ch, rd8(v->split + 18), rd8(v->split + 19), rd8(v->patch + 20));
    modulation(v, ch, rd8(v->split + 20), rd8(v->split + 21), rd8(v->patch + 21));
    modulation(v, ch, rd8(v->split + 22), rd8(v->split + 23), rd8(v->patch + 21));
}

/* 0x3194: the bend for the patch's setting (64 none), up and down ranges from the split */
static int32_t bend(uint32_t b, uint32_t up, uint32_t down) {
    int32_t d = (int32_t)b - 64;
    if (d == 0) {
        return 0;
    }
    if (d > 0) {
        return (d + 1) * (int32_t)(up << 8) >> 6;
    }
    return d * (int32_t)(down << 8) >> 6;
}

/* 0x3F18: the note's pitch (256 a semitone) with the patch's and the voice's tuning, and its register */
static uint32_t note_pitch(const VOICE* v, uint32_t note, int32_t* pitch) {
    int32_t p = 0;
    if (rd8(arm_ld32(v->patch + 4)) == 0) {
        p = (int32_t)note - 60;
    }
    p <<= 8;
    p += (int32_t)(rd8(v->patch + 12) << 25) >> 17;
    p += (int32_t)(rd8(v->voice + 10) << 24) >> 16;
    p += (int32_t)(rd8(v->patch + 13) << 25) >> 23;
    p += (int32_t)(rd8(v->voice + 11) << 24) >> 22;
    if (rd8(v->voice + 28) != 0) {
        give_up();                  /* 0x31B8 */
    }
    p += bend(rd8(v->patch + 18), rd8(v->split + 33), rd8(v->split + 34));
    *pitch = p;
    return pitch_reg(p);
}

/* x scaled by k/128, rounded up */
static uint32_t scaled(uint32_t x, uint32_t k) {
    x = x * (k << 1) >> 8;
    return x ? x + 1 : 0;
}

/* 0x3E74 with 0x2CA4: the voice's attenuation from the velocity through the bank's curve, then each level */
static uint32_t voice_level(const VOICE* v, uint32_t curve, uint32_t vel, uint32_t level) {
    uint32_t bank = arm_ld32(v->trk), at = arm_ld32(bank + 40) + bank, x;
    if (curve > arm_ld32(at)) {
        curve = 0;
    }
    x = rd8(at + 4 + curve * 128 + vel);
    x = scaled(x, level);
    x = scaled(x, rd8(v->patch + 10));
    x = scaled(x, rd8(v->patch + 16));
    x = scaled(x, rd8(WORK + 5));
    if (!(rd8(v->patch) & 1)) {
        x = scaled(x, rd8(v->trk + 6) & 127);
    }
    x ^= 255;
    return x != 255 ? x >> 1 : x;
}

/* 0x4034 with 0x411C and 0x40F0: the sample's registers (start, its top bits, loop start and end), from the
   bank, the start moved by `skip` */
static void sample_regs(uint32_t bank, uint32_t id, uint32_t skip, uint32_t r[4]) {
    uint32_t at, a, ls, le, k;
    if (arm_ld32(bank + 80) & 0x80000000u) {
        give_up();                  /* 0x4128 */
    }
    if (id < 256) {
        give_up();                  /* 0x4064 */
    }
    at = (id - 256) * 16 + 4 + arm_ld32(bank + 60) + bank;
    a = arm_ld32(at) + bank;
    wr32(WORK + 0x120, at);
    ls = arm_ld32(at + 4) & 0xFFFF;
    le = arm_ld32(at + 6) & 0xFFFF;
    if (skip != 0) {
        k = (a & 0x1800000) >> 23;
        a += k == 0 ? skip << 1 : k == 1 ? skip : skip >> 1;
        if ((a & 0x2000000) && ls != 0) {
            ls -= skip;
        }
        le -= skip;
    }
    r[0] = a & 0xFFFF;
    r[1] = a >> 16 & 0x7FF;
    r[2] = ls;
    r[3] = le;
}

/* 0x36EC: a voice started on a slot: its registers set, and the slot noted for keying on */
static void voice_on(const VOICE* v) {
    uint32_t s, ch, slot, f, r[4], x, list;
    int32_t pitch;
    if (rd8(v->patch + 1) & 1) {
        give_up();                  /* 0x39C4 */
    }
    if (!slot_alloc(rd8(v->patch), &s)) {
        return;
    }
    ch = CH(s);
    list = rd8(v->patch) & 1 ? LISTS + 4 : LISTS;
    list_add(list, s);
    wr8(WORK + 0x160, s);
    slot = SLOTS + s * 64;
    wr32(WORK + 0xB0, slot);
    wr32(slot + 4, rd32(CMD));
    wr8(slot + 9, rd8(WORK + 0xA0));
    wr32(slot + 28, v->voice);
    wr32(slot + 12, v->split);
    f = 0x80;
    if (rd8(v->patch) & 1) {
        f |= 0x40;
    }
    if (rd8(v->voice + 1) & 0x80) {
        f |= 8;
    }
    if (rd8(v->patch + 19)) {
        f |= 1;
    }
    wr8(slot, f);
    wr8(slot + 2, rd8(v->voice + 35));
    wr8(slot + 3, rd8(v->voice + 36) + 128);
    wr8(slot + 1, rd8(v->voice + 34) & 0x83);
    x = rd32(WORK + 0xB8);
    wr32(slot + 48, x);
    wr8(slot + 32, x != 0);
    wr8(slot + 11, 0);
    wr8(slot + 19, 0);
    wr32(slot + 60, 0);
    chip_wr32(ch + 16, arm_ld32(v->voice + 12) & 0xFFFF);
    chip_wr32(ch + 20, arm_ld32(v->voice + 14) & 0xFFFF);
    chip_wr32(ch + 28, arm_ld32(v->voice + 48) & 0xFFFF);
    if (arm_ld32(v->voice + 16) & 0x7FF) {
        give_up();                  /* 0x3824 */
    }
    chip_wr8(ch + 40, 0x20);
    x = rd8(v->voice + 6);          /* 0x3FC4 */
    if (x & 0x80) {
        x = rd8(v->patch + 11);
    } else {
        x &= 31;
        if (rd8(v->split + 40) & 31) {
            give_up();              /* 0x3FE0 */
        }
    }
    chip_wr8(ch + 36, x);
    wr8(WORK + 0x165, x);
    x = rd8(v->voice + 7);
    chip_wr8(ch + 37, (x & 0x80 ? rd8(v->patch + 9) : x) & 15);
    chip_wr8(ch + 32, rd8(v->voice + 30) & 0x80 ? rd8(v->patch + 8) : rd8(v->voice + 31));
    sample_regs(rd32(WORK + 0xA4), arm_ld32(v->voice + 2) & 0xFFFF, arm_ld32(v->voice + 4) & 0xFFFF, r);
    chip_wr32(ch + 4, r[0]);
    chip_wr32(ch + 0, r[1]);
    chip_wr32(ch + 8, r[2]);
    chip_wr32(ch + 12, r[3]);
    wr32(WORK + 0x170, rd32(WORK + 0x120));
    chip_wr32(ch + 24, note_pitch(v, rd8(CMD + 2), &pitch));    /* 0x3C9C */
    wr32(WORK + 0x168, (uint32_t)pitch);
    x = voice_level(v, rd8(v->split + 6), rd8(CMD + 1), rd8(v->voice + 8) >> 1);  /* 0x3BC0 */
    chip_wr8(ch + 41, x);
    wr8(WORK + 0x164, x ^ 0xFF);
    wr32(slot + 24, rd32(WORK + 0x168));
    wr8(slot + 21, rd8(WORK + 0x164));
    wr8(slot + 20, rd8(WORK + 0x165));
    wr8(slot + 23, 0xFF);
    modulations(v, ch);
    pitch_slide(rd32(WORK + 0xB0), ch, 0);
    x = rd8(WORK + 0xA2);           /* 0x22C0 */
    wr8(STARTED + x, rd8(WORK + 0x160));
    wr8(WORK + 0xA2, x + 1);
    if (rd8(rd32(WORK + 0x170) + 8) & 0x80) {
        give_up();                  /* 0x3CF4 */
    }
}

/* 0x20D8: the slots of the same exclusive group, track and channel let go */
static void group_cut(uint32_t group) {
    uint32_t slot, key = rd32(CMD) & 0x0F000007u;
    for (slot = SLOTS; slot != SLOTS + 64 * 64; slot += 64) {
        if ((rd32(slot) & 0x8380) == 0x8180 && (rd32(slot + 4) & 0x0F000007u) == key && rd8(slot + 2) == group) {
            channel_end(slot, rd8(slot + 22));
        }
    }
}

/* 0x1FF4: once a note on, what its first voice of a drum kit's kind asks */
static void first_voice(const VOICE* v) {
    uint32_t m;
    if (rd8(WORK + 0xA3) == 1) {
        return;
    }
    wr8(WORK + 0xA3, 1);
    m = rd8(v->voice + 34) & 0x83;
    if (m == 0) {
        return;
    }
    if (m != 0x81) {
        give_up();                  /* 0x205C, 0x2164 */
    }
    group_cut(rd8(v->voice + 35));
}

/* 0x3594: a patch's split (offset off in its table) started if the note and velocity fall in it */
static void split_on(VOICE* v, uint32_t vt, uint32_t off) {
    uint32_t note = rd8(CMD + 2), vel = rd8(CMD + 1), kind, n, d;
    int32_t left;
    off &= 0xFFFF;
    if (off == 0) {
        return;
    }
    v->split = vt + off;
    v->trk = rd32(WORK + 0xB4);
    wr32(WORK + 0xA8, v->split);
    kind = rd8(rd32(v->patch + 4));
    if (kind == 1) {                /* 0x366C: a voice for each note */
        v->voice = v->split + 64;
        if (note > rd8(v->voice + 1)) {
            return;
        }
        d = note - rd8(v->voice);
        if ((int32_t)d < 0) {
            return;
        }
        off = arm_ld32(v->voice + 2 + d * 2) & 0xFFFF;
        if (off == 0) {
            return;
        }
        v->voice = v->split + off;
        wr32(WORK + 0xAC, v->voice);
    } else if (kind == 2) {         /* 0x3630: by note and track */
        v->voice = v->split + 64;
        n = note | (rd8(CMD) & 0xF0) << 3;
        if (n > (arm_ld32(v->split) & 0xFFFF)) {
            return;
        }
        v->voice += n * 64;
        wr32(WORK + 0xAC, v->voice);
    } else {                        /* by key ranges */
        if (vel < rd8(v->split + 2) || vel > rd8(v->split + 3)) {
            return;
        }
        v->voice = v->split + 64;
        wr32(WORK + 0xAC, v->voice);
        for (left = (int32_t)rd8(v->split); note > rd8(v->voice); v->voice += 64) {
            if (--left < 0) {
                return;
            }
        }
        if (rd32(v->patch + 32) & 0xC0C0) {
            give_up();              /* 0x1E24 */
        }
        voice_on(v);
        return;
    }
    if (vel < rd8(v->voice + 32) || vel > rd8(v->voice + 33)) {
        return;
    }
    first_voice(v);
    voice_on(v);
}

/* 0x34DC: a patch the note's track and channel play, if the note and velocity are in its ranges */
static void patch_note_on(uint32_t cmd, uint32_t p) {
    VOICE v;
    uint32_t vt, range, note = rd8(CMD + 2), vel = rd8(CMD + 1), k;
    (void)cmd;
    wr8(WORK + 0xA0, p);
    v.patch = PATCHES + p * 64;
    vt = rd32(v.patch + 4);
    if (!(rd8(v.patch) & 0x80)) {
        return;
    }
    range = rd32(v.patch + 28);
    if (note < rd8(range + 4) || note > rd8(range + 5) || vel < rd8(range + 6) || vel > rd8(range + 7)) {
        return;
    }
    wr32(WORK + 0xB8, 0);           /* 0x3A04 */
    if (rd32(v.patch + 32) & 0x8000) {
        give_up();
    }
    for (k = 0; k < 4; k++) {
        split_on(&v, vt, arm_ld32(vt + 8 + k * 2));
    }
}

/* 0x2204: the slots started keyed on: each marked, the last register write keys them all */
static void key_started(void) {
    uint32_t n = rd8(WORK + 0xA2), at, s, ch = 0, x;
    if (n == 0) {
        return;
    }
    wr8(STARTED + n, 0xFF);
    for (at = STARTED; (s = rd8(at)) != 0xFF; at++) {
        ch = CH(s);
        wr8(SLOTS + s * 64 + 8, rd8(at + 1));
        chip_wr8(ch + 1, chip_rd8(ch + 1) | 0x40);
        x = chip_rd8(ch + 29);
        if (x & 0x80) {
            chip_wr8(ch + 29, x & 0x7F);
        }
    }
    chip_wr8(ch + 1, chip_rd8(ch + 1) | 0xC0);
}

/* 0x3488: note on (velocity 0 a note off) */
static void note_on(uint32_t cmd) {
    uint32_t t;
    if ((cmd >> 8 & 0xFF) == 0) {
        note_off(cmd);
        return;
    }
    t = TRACK_PARAMS + (cmd & 7) * 32;
    wr32(WORK + 0xA4, rd32(t));
    wr32(WORK + 0xB4, t);
    wr8(WORK + 0xA2, 0);
    wr8(WORK + 0xA3, 0);
    for_layers(patch_note_on, 0x34DC);
    key_started();
}

#define PANS 0x300Cu                /* 32 pans, for a controller's value / 4 */

/* 0x2FFC */
static uint32_t pan(uint32_t v) {
    return rd8(PANS + (v >> 2));
}

/* 0x3CB4: what a slot's voice was started with */
static void slot_voice(uint32_t slot, VOICE* v) {
    v->voice = rd32(slot + 28);
    v->split = rd32(slot + 12);
    v->patch = PATCHES + rd8(slot + 9) * 64;
    v->trk = TRACK_PARAMS + (rd8(slot + 4) & 7) * 32;
}

/* 0x4B78, 0x4B94, 0x4BB0, 0x4BCC, 0x4BE8 and 0x4C10: a controller's value kept in the patch */
static void patch_bank(uint32_t cmd, uint32_t p) {
    wr8(PATCHES + p * 64 + 2, cmd >> 8 & 0xFF);
}

static void patch_depth1(uint32_t cmd, uint32_t p) {
    wr8(PATCHES + p * 64 + 20, cmd >> 8 & 0xFF);
}

static void patch_depth2(uint32_t cmd, uint32_t p) {
    wr8(PATCHES + p * 64 + 21, cmd >> 8 & 0xFF);
}

static void patch_volume(uint32_t cmd, uint32_t p) {
    wr8(PATCHES + p * 64 + 10, cmd >> 8 & 0xFF);
}

static void patch_pan(uint32_t cmd, uint32_t p) {
    wr8(PATCHES + p * 64 + 11, pan(cmd >> 8 & 0xFF));
}

static void patch_pedal(uint32_t cmd, uint32_t p) {
    wr8(PATCHES + p * 64 + 19, cmd >> 8 & 0xFF);
}

/* 0x4C2C: a slot's modulations again */
static void slot_modulations(uint32_t s) {
    VOICE v;
    slot_voice(SLOTS + s * 64, &v);
    modulations(&v, CH(s));
}

/* 0x3B70 with 0x3BF8: a slot's level again (23 a paired channel, which has no C) */
static void slot_volume(uint32_t s) {
    uint32_t slot = SLOTS + s * 64, pair;
    VOICE v;
    slot_voice(slot, &v);
    wr8(slot + 21, voice_level(&v, rd8(v.split + 6), rd8(slot + 5), rd8(v.voice + 8) >> 1) ^ 0xFF);
    pair = rd8(slot + 23);
    if (pair == 0xFF) {
        chip_wr8(CH(s) + 41, rd8(slot + 21) ^ 0xFF);
    } else if (pair != 0xFE) {
        give_up();
    }
}

/* 0x4ADC with 0x3BE8: a slot's pan, if its voice follows the channel's */
static void slot_pan(uint32_t s) {
    uint32_t slot = SLOTS + s * 64, x, pair;
    if (!(rd8(rd32(slot + 28) + 6) & 0x80)) {
        return;
    }
    x = pan(rd8(CMD + 1));
    wr8(slot + 20, x);
    pair = rd8(slot + 23);
    if (pair == 0xFF) {
        chip_wr8(CH(s) + 36, x);
    } else if (pair != 0xFE) {
        give_up();
    }
}

/* 0x4B18: the pedal down (0x01), or up, letting go the notes it held (0x02) */
static void slot_pedal(uint32_t s) {
    uint32_t slot = SLOTS + s * 64, f = rd8(slot) & 0xFE;
    if (rd8(CMD + 1) != 0) {
        wr8(slot, f | 1);
        return;
    }
    wr8(slot, f);
    if (f & 2) {
        key_off(CH(s));
    }
}

/* 0x4B50: a slot of the command's channel ended */
static void slot_sound_off(uint32_t s) {
    uint32_t slot = SLOTS + s * 64;
    if ((rd32(CMD) >> 24 & 15) == (rd8(slot + 7) & 15)) {
        channel_end(slot, s);
    }
}

/* 0x3B50: the patch's bend */
static void patch_bend(uint32_t cmd, uint32_t p) {
    wr8(PATCHES + p * 64 + 18, cmd >> 8 & 0xFF);
}

/* 0x3C50: a slot's pitch again, after the bend changed */
static void slot_bend(uint32_t s) {
    uint32_t slot = SLOTS + s * 64, reg;
    int32_t pitch;
    VOICE v;
    slot_voice(slot, &v);
    reg = note_pitch(&v, rd8(slot + 6), &pitch);
    wr32(slot + 24, (uint32_t)pitch);
    chip_wr32(CH(s) + C_PITCH, pitch_slide(slot, CH(s), reg));
}

/* 0x3B28: pitch bend */
static void pitch_bend(void) {
    for_layers(patch_bend, 0x3B50);
    for_slots(slot_bend, 0x3C50, 0x0F000007u);
}

/* 0x2CF4: the voices for the patch's program: the bank's drum kit, or by its bank select (2) and program (3) */
static uint32_t program_voices(uint32_t trk, uint32_t patch) {
    uint32_t bank = rd32(trk), at, off;
    if (rd8(patch) & 2) {
        off = arm_ld32(bank + 48);
        return off ? off + bank : 0;
    }
    at = arm_ld32(bank + 36) + bank;
    if (rd8(patch + 2) > rd8(at)) {
        return 0;
    }
    off = arm_ld32(at + rd8(patch + 2) * 2 + 2) & 0xFFFF;
    if (off == 0) {
        return 0;
    }
    at += off;
    if (rd8(patch + 3) > rd8(at)) {
        return 0;
    }
    off = arm_ld32(at + rd8(patch + 3) * 2 + 2) & 0xFFFF;
    return off ? off + at : 0;
}

/* 0x3AF0 and 0x3B0C */
static void patch_program(uint32_t cmd, uint32_t p) {
    wr8(PATCHES + p * 64 + 3, cmd >> 16 & 0xFF);
}

static void patch_voices(uint32_t cmd, uint32_t p) {
    (void)cmd;
    wr32(PATCHES + p * 64 + 4, rd32(WORK + 0xEC));
}

/* 0x3A78: a program change: each patch's program, then the voices for the first patch's, if the bank has them */
static void program_change(uint32_t cmd) {
    uint32_t p, at;
    for_layers(patch_program, 0x3AF0);
    p = rd8(LAYERS + (cmd & 7) * 64 + (cmd >> 24 & 15) * 4);
    if (p == 0xFF) {
        return;
    }
    at = program_voices(TRACK_PARAMS + (cmd & 7) * 32, PATCHES + p * 64);
    if (at == 0) {
        return;
    }
    wr32(WORK + 0xEC, at);
    for_layers(patch_voices, 0x3B0C);
}

/* the command's track, or (with 0x08) each track that has a bank */
static int track_chosen(uint32_t cmd, uint32_t t) {
    return cmd & 8 ? rd32(TRACK_PARAMS + t * 32) != 0 : t == (cmd & 7);
}

/* 0x252C, or 0x2498 for each track: fn for each patch the track's channels play */
static void for_track_patches(void (*fn)(uint32_t trk, uint32_t p), uint32_t fn_at, uint32_t cmd) {
    uint32_t t, g, k, p;
    wr32(EACH, fn_at);
    for (t = 0; t < 8; t++) {
        if (!track_chosen(cmd, t)) {
            continue;
        }
        for (g = 0; g < 64; g += 4) {
            for (k = g; k < g + 4 && (p = rd8(LAYERS + t * 64 + k)) != 0xFF; k++) {
                fn(TRACK_PARAMS + t * 32, p);
            }
        }
    }
}

/* 0x3158 with 0x314C: a patch's level from its track's two (the second for a patch with 0x01) */
static void patch_level(uint32_t trk, uint32_t p) {
    uint32_t patch = PATCHES + p * 64, second = rd8(patch) & 1;
    wr8(patch + 16, offset_level(rd8(trk + 20 + second), rd8(trk + 22 + second) * 2 - 64, 127));
}

/* 0x4588 and 0x4590 (0xA0 0x10 and 0x11): a track's level or its second's, then its patches' and slots' again */
static void track_level(uint32_t cmd, uint32_t at) {
    uint32_t t;
    for (t = 0; t < 8; t++) {      /* 0x23DC with 0x47B0 */
        if (track_chosen(cmd, t)) {
            wr8(TRACK_PARAMS + t * 32 + at, cmd >> 8 & 0x7F);
        }
    }
    for_track_patches(patch_level, 0x4788, cmd);
    for_slots(slot_volume, 0x3B70, cmd & 8 ? 0 : 7);    /* 0x2644 */
}

/* 0x43F8: a command without 0x40 taken as a message from the SH4 too */
static void as_message(uint32_t cmd) {
    if (cmd & 0x40) {
        return;
    }
    if (cmd >> 24 == 0xA0) {
        driver_message(cmd);
    } else if (sound_start(cmd)) {
        tracks();
    }
}

/* 0x4730, and 0x4720 for a slot with 0x40 only */
static void slot_end(uint32_t s) {
    uint32_t slot = SLOTS + s * 64;
    if (!(rd8(slot) & 0x14)) {
        channel_end(slot, s);
    }
}

static void slot_end_second(uint32_t s) {
    if (rd8(SLOTS + s * 64) & 0x40) {
        slot_end(s);
    }
}

/* 0x4710 */
static void slot_end_first(uint32_t s) {
    if (!(rd8(SLOTS + s * 64) & 0x40)) {
        slot_end(s);
    }
}

#define KEPT 0xDE00u                /* 16 of 96 bytes; from 32, 16 entries: 0x80 on, and two slots at 2 and 3 */

/* 0x6454 with 0x64CC: a kept record stopped and its entries' slots ended (the first entry only if it was on) */
static void kept_stop(uint32_t i) {
    uint32_t rec = KEPT + i * 96, k = 0, e, s;
    if (rd8(rec) & 0x80) {
        wr8(rec, rd8(rec) & 0x7F);
    } else {
        k = 1;
    }
    for (; k < 16; k++) {
        e = rec + 32 + k * 4;
        if (!(rd8(e) & 0x80)) {
            continue;
        }
        if ((s = rd8(e + 2)) != 0xFF) {
            channel_end(SLOTS + s * 64, s);
        }
        if ((s = rd8(e + 3)) != 0xFF) {
            channel_end(SLOTS + s * 64, s);
        }
        wr32(e, 0xFFFF0000u);
    }
}

/* 0x474C with 0x2E40: a patch's settings back to their start, 11-13 from its range, its splits' 42-43 cleared */
static void patch_reset(uint32_t trk, uint32_t p) {
    uint32_t patch = PATCHES + p * 64, range = rd32(patch + 28), vt, k, off;
    (void)trk;
    wr8(patch + 19, 0);
    wr8(patch + 20, 0);
    wr8(patch + 21, 0);
    wr8(patch + 22, 0);
    wr8(patch + 36, 0);
    wr8(patch + 37, 0xFF);
    wr32(patch + 40, 0xFFFFFFFFu);
    wr8(patch + 18, 64);
    wr8(patch + 38, 60);
    vt = rd32(patch + 4);
    if (vt != 0) {
        for (k = 8; k <= 14; k += 2) {
            off = arm_ld32(vt + k) & 0xFFFF;
            if (off != 0) {
                wr8(vt + off + 42, 0);
                wr8(vt + off + 43, 0);
            }
        }
    }
    wr8(patch + 11, rd8(range + 11));
    wr8(patch + 12, rd8(range + 12));
    wr8(patch + 13, rd8(range + 13));
}

/* 0x4350 (0xA0 0x00): by the second byte */
static void driver_setting(uint32_t cmd) {
    uint32_t op = cmd >> 8 & 0xFF, i, first = op == 0x02 || op == 0x12, one = op >= 0x11;
    switch (op) {
    case 0x01:                      /* 0x4414: the kept records stopped, then all ended and reset */
    case 0x04:
        for (i = 0; i < 16; i++) {  /* 0x6428 */
            kept_stop(i);
        }
        if (op == 0x04) {
            break;
        }
        /* fall through */
    case 0x02:                      /* 0x442C, 0x4444, 0x4450: the first slots, or all, or the track's, ended */
    case 0x11:
    case 0x12:
        for_slots(first ? slot_end_first : slot_end, first ? 0x4710 : 0x4730, one ? 7 : 0);
        for_track_patches(patch_reset, 0x474C, one ? cmd & ~8u : cmd | 8);
        as_message(cmd);
        break;
    case 0x03:                      /* 0x4438: the second slots ended, or (0x13) the track's */
    case 0x13:
        for_slots(slot_end_second, 0x4720, op == 0x13 ? 7 : 0);
        as_message(cmd);
        break;
    case 0x08:                      /* 0x44C8 */
    case 0x09:
        chip_wr8(0x2801, (rd8(SHARED + 17) & 0x7F) | (op == 0x09 ? 0x80 : 0));
        break;
    case 0x0A:                      /* 0x44F0 */
        wr8(WORK + 1, rd8(WORK + 1) | 2);
        break;
    case 0x0B:
        wr8(WORK + 1, rd8(WORK + 1) & ~2u);
        break;
    case 0x0F:
        wr8(WORK + 1, rd8(WORK + 1) | 1);
        break;
    case 0x10:
        wr8(WORK + 1, rd8(WORK + 1) & ~1u);
        break;
    case 0x20:
        wr8(WORK + 11, 1);
        break;
    case 0x38:
        wr8(WORK + 190, 1);
        break;
    case 0x0C:
    case 0x0D:
    case 0x0E:
    case 0x18:
    case 0x19:
        as_message(cmd);
        break;
    }
}

/* 0x420C: the driver's own commands, by the low nibble of the top byte */
static void driver_command(uint32_t cmd) {
    uint32_t sub = cmd >> 16 & 0xFF;
    switch (cmd >> 24) {
    case 0xA0:
        switch (sub) {
        case 0x00:
            driver_setting(cmd);
            return;
        case 0x03:
        case 0x06:
        case 0x09:
        case 0x0A:
            as_message(cmd);
            return;
        case 0x04:
            track_level(cmd, 20);
            return;
        case 0x05:
            track_level(cmd, 22);
            return;
        case 0x10:
            track_level(cmd, 21);
            return;
        case 0x11:
            track_level(cmd, 23);
            return;
        case 0x1C:                  /* 0x4644 with 0x2C04: a track's volume, and its slots' levels again */
            wr8(TRACK_PARAMS + (cmd & 7) * 32 + 6, cmd >> 8 & 0x7F);
            for_slots(slot_volume, 0x3B70, 7);
            return;
        case 0x01:
        case 0x02:
        case 0x07:
        case 0x18:
        case 0x19:
        case 0x1A:
        case 0x1B:
        case 0x28:
        case 0x29:
        case 0x30:
        case 0x38:
        case 0x39:
            break;
        default:
            return;
        }
        break;
    case 0xA8:
    case 0xA9:
    case 0xAA:
    case 0xAB:
    case 0xAC:
    case 0xAD:
        as_message(cmd);
        return;
    case 0xAE:
    case 0xAF:
        return;
    case 0xA1:
        if (cmd & 0x8000) {
            break;
        }
        switch (cmd >> 16 & 15) {
        case 0:
            record_sizes();
            return;
        case 1:
            record_start(cmd);
            return;
        case 2:
            record_stop(cmd);
            return;
        case 8:
        case 9:
        case 10:
        case 11:
        case 12:
            break;                  /* 0x50FC, 0x5130, 0x5148, 0x519C, 0x51F8 */
        default:
            return;
        }
        break;
    case 0xA2:
        if ((cmd & 0x000F8000) == 0 || (cmd & 0x000F8000) == 0x00080000) {
            record_level(cmd, (cmd & 0x000F8000) != 0);
            return;
        }
        break;
    }
    give_up();
}

/* 0x48A4: a controller change; those not here do nothing */
static void controller(uint32_t cmd) {
    switch (cmd >> 16 & 0xFF) {
    case 0:
        for_layers(patch_bank, 0x4B78);
        break;
    case 1:
        for_layers(patch_depth1, 0x4B94);
        for_slots(slot_modulations, 0x4C2C, 0x0F000007u);
        break;
    case 2:
        for_layers(patch_depth2, 0x4BB0);
        for_slots(slot_modulations, 0x4C2C, 0x0F000007u);
        break;
    case 7:
        for_layers(patch_volume, 0x4BCC);
        for_slots(slot_volume, 0x3B70, 0x0F000007u);
        break;
    case 10:
        for_layers(patch_pan, 0x4BE8);
        for_slots(slot_pan, 0x4ADC, 0x0F000007u);
        break;
    case 39:
        for_layers(patch_volume, 0x4BCC);
        break;
    case 42:
        for_layers(patch_pan, 0x4BE8);
        break;
    case 64:
        for_layers(patch_pedal, 0x4C10);
        for_slots(slot_pedal, 0x4B18, 0x0F000007u);
        break;
    case 120:
        for_slots(slot_sound_off, 0x4B50, 7);
        break;
    }
}

static uint32_t envelope_phase(void) {
    return chip_rd32(STATUS) >> 13 & 3;
}

/* the envelope past its attack and quieter than WORK[6] */
static int faded(void) {
    uint32_t v = chip_rd32(STATUS);
    return (v >> 13 & 3) != 0 && (v & 0x1FFF) > rd8(WORK + 6) << 4;
}

static unsigned watch_changes;     /* what the watch has done besides moving on */
static uint32_t watch_chose;        /* its last write to MSLC, */
static int watch_chosen;            /* and whether there was one since this was cleared */
static unsigned watch_own;          /* its MSLC writes, which aica_watch_gen counts too */

static void watch_choose(uint32_t c) {
    chip_wr8(MSLC, watch_chose = c);
    watch_own += !noting;
}

/* the slot that channels_watch chose last pass: let go if its sound has ended, or keyed off once past its
   loop if it should play once */
static void slot_watch(uint32_t s) {
    uint32_t slot = SLOTS + s * 64, ch = CH(s), at;
    if (chip_rd32(ch) & C_LPCTL) {
        if (!(rd8(slot) & 8) || rd8(slot + 19) == 1) {
            if (faded()) {
                channel_ended(s);
                watch_changes++;
            }
        } else if ((at = chip_rd32(POSITION)) != 0) {
            if (at >= chip_rd32(ch + C_LOOP_START)) {
                key_off(ch);
                wr8(slot + 19, 1);
                watch_changes++;
            }
        } else if (envelope_phase() != 0 && faded()) {
            channel_ended(s);
            watch_changes++;
        }
    } else if ((chip_rd32(POSITION) & 0xFFFF) == 0 && envelope_phase() != 0) {
        channel_ended(s);
        watch_changes++;
    }
}

/* 0x09A4: a record's or a slot's channel chosen each pass, and looked at the next: the record's position
   kept, the slot's sound watched */
static void channels_watch(void) {
    uint32_t state = rd8(WATCH), rec, s, next, at;
    in_watch = 1;
    if (state == 2) {
        s = rd8(WATCH + 1);
        slot_watch(s);
        wr8(WATCH + 1, (s + 1) & 63);
    }
    rec = RECORDS + rd8(WATCH + 2) * 96;
    if (state == 1) {
        at = chip_rd32(POSITION) & 0xFFFF;
        if (rd32(rec + 20) != at) {
            watch_changes++;
        }
        wr32(rec + 20, at);
    } else if (rd8(rec) & 0x80) {
        watch_choose(rd8(rec + 2));
        watch_chosen = 1;
        wr8(WATCH, 1);
        in_watch = 0;
        return;
    }
    for (next = 0, rec += 96; rec != 0x400; rec += 96) {
        if (rd8(rec) & 0x80) {
            next = rd8(rec + 24);
            break;
        }
    }
    wr8(WATCH + 2, next);
    for (s = rd8(WATCH + 1); s != 64; s++) {
        if (rd8(SLOTS + s * 64) & 0x80) {
            wr8(WATCH + 1, s);
            watch_choose(s);
            watch_chosen = 1;
            wr8(WATCH, 2);
            in_watch = 0;
            return;
        }
    }
    wr8(WATCH + 1, 0);
    wr8(WATCH, 0);
    in_watch = 0;
}

/* 0x28F8: MIDI out */
static void midi_out(void) {
    if (chip_rd32(0x28A0) & 0x200) {
        hand_off(0x0640);
    }
}

/* 0x30B0: a pitch (0xC00 an octave) as the channel's octave and frequency */
static uint32_t pitch_reg(int32_t p) {
    int32_t oct = 0;
    uint32_t f;
    if (p >= 0) {
        while (p >= 0xC00) {
            p -= 0xC00;
            oct++;
        }
        f = rd16(PITCH_UP + (uint32_t)p / 2);
    } else {
        oct = -1;
        while (p <= -0xC00) {
            p += 0xC00;
            oct--;
        }
        f = rd16(PITCH_DOWN - (uint32_t)-p / 2);
    }
    return f + (((uint32_t)oct & 15) << 11);
}

/* 0x2280: a slot's pitch moved by its slide (at 48) while there is one; the register it sets, or reg */
static uint32_t pitch_slide(uint32_t slot, uint32_t ch, uint32_t reg) {
    uint32_t f = rd8(slot + 32);
    if (!(f & 1)) {
        return reg;
    }
    reg = pitch_reg((int32_t)((rd32(slot + 24) + rd32(slot + 48)) & ~3u));
    chip_wr32(ch + C_PITCH, reg);
    if (rd32(slot + 48) == 0) {
        wr8(slot + 32, f & 0xFE);
    }
    return reg;
}

/* 0x10A4: a playing slot's slide brought nearer its end by its patch's step */
static void slot_tick(uint32_t s) {
    uint32_t slot = SLOTS + s * 64, patch, v, w;
    if (!(rd8(slot) & 0x80)) {
        return;
    }
    patch = PATCHES + rd8(slot + 9) * 64;
    v = rd32(slot + 48);
    w = v - rd32(patch + 24);
    if ((w ^ v) & 0x80000000u) {
        w = 0;
    }
    wr32(slot + 48, w);
    pitch_slide(slot, CH(s), 0);
}

/* 0x0F14: for each tick, 8 of the slots in turn, then the countdowns to the SH4's reports */
static void slots(void) {
    uint32_t n = rd32(WORK + 0x20), first, s;
    if (n == 0) {
        return;
    }
    wr32(WORK + 0x20, n - 1);
    first = rd8(WORK + 0x25);
    wr8(WORK + 0x25, first + 8);
    for (s = first & 63; s < (first & 63) + 8; s++) {
        slot_tick(s);
    }
    if (rd8(SHARED + 0x78) == 1) {
        n = rd8(WORK + 0x24);
        wr8(WORK + 0x24, n - 1);
        if (n == 1) {
            wr8(WORK + 0x24, 250);
            reply(0xFE000000u);
        }
    }
    n = rd8(WORK + 0x30);
    wr8(WORK + 0x30, n - 1);
    if (n != 1) {
        return;
    }
    wr8(WORK + 0x30, 25);
    n = rd8(WORK + 0x31);
    wr8(WORK + 0x31, n - 1);
    if (n != 1) {
        if (rd8(WORK + 0x100) > rd8(WORK + 7)) {
            wr8(WORK + 7, rd8(WORK + 0x100));
        }
        return;
    }
    wr8(WORK + 0x31, 10);
    wr32(SHARED + 0xB8, rd8(WORK + 7));
    wr8(WORK + 7, 0);
}

/* 0x06D4: the ticks since the last pass, and their most */
static void load_meter(void) {
    uint32_t ticks = rd32(WORK + 0x2C);
    wr32(WORK + 0x28, ticks);
    wr32(WORK + 0x2C, 0);
    if (ticks > rd32(SHARED + 0xA8)) {
        wr32(SHARED + 0xA8, ticks);
    }
}


static void track_send(uint32_t out, uint32_t cmd) {
    if (out & 0x40) {
        queue_put(cmd);
    }
    if (out & 0x20) {
        reply(cmd);                 /* 0x2880, the same as 0x28C8 */
    }
}

/* 0x6D20: a track ended, and the tracks waiting in its group with it */
static void track_end(uint32_t t) {
    uint32_t g = rd8(t + T_GROUP), u, f;
    for (u = TRACKS; u < TRACKS + 16 * TRACK_SIZE; u += TRACK_SIZE) {
        f = rd8(u);
        if (u != t && (f & 0x80) && (f & 0x20) && rd8(u + T_GROUP) == g) {
            wr8(u, 0);
        }
    }
    wr8(t, 0);
}

/* 0x6BCC: a track's control event, 0xA0: where its events go, a value for the SH4 (0xB3B0), or the tracks
   waiting in its group freed. 1 when one of those plays the same part, and this track gives way */
static int track_control(uint32_t t, uint32_t* out, uint32_t* cmd) {
    static const uint8_t to[3] = { 0x40, 0x20, 0x60 };
    uint32_t c = *cmd |= 0x80, kind = c >> 16 & 0x7F, v, u, f, uo, g;
    int gives_way = 0;
    if (kind == 0) {
        v = (c >> 8 & 0x7F) - 12;
        if (v < 3) {
            *out |= to[v];
            wr8(t + T_OUT, *out);
        }
    } else if (kind == 0x24) {
        if (rd32(0xB3B0) == 0) {
            wr32(0xB3B0, c >> 8 & 0x7F);
        }
    } else if (kind == 0x20 && (v = c >> 8 & 0xFF) != 0) {
        g = rd8(t + T_GROUP);
        for (u = TRACKS; u < TRACKS + 16 * TRACK_SIZE; u += TRACK_SIZE) {
            f = rd8(u);
            if (u == t || !(f & 0x80) || !(f & 0x20) || rd8(u + T_GROUP) != g || v > rd8(u + T_RANK)) {
                continue;
            }
            wr8(u, f & ~0x20u);
            uo = rd8(u + T_OUT);
            if (*out & 0x80) {
                gives_way |= (uo & 0x80) && (uo & 31) == (*out & 31);
            } else {
                gives_way |= !(uo & 0x80) && rd8(u + T_ID) == rd8(t + T_ID);
            }
        }
    }
    return gives_way;
}

/* a track's part ended: the next from its list, with the list's markers sent on the way. 0 when the track
   has ended */
static int next_part(uint32_t t, uint32_t* out, uint32_t* p) {
    uint32_t q = rd32(t + T_LIST), w, kind, f, cmd;
    if (q == NO_LIST) {
        track_end(t);
        return 0;
    }
    for (;;) {
        w = rd32(q);
        q += 4;
        kind = w >> 24;
        w &= 0xFFFFFF;
        if (kind == 0) {
            wr32(t + T_LIST, q);
            *p = rd32(t + T_BASE) + w;
            return 1;
        } else if (kind == 0x80) {
            cmd = w << 8 | rd8(t + T_ID);
            if (cmd >> 24 == 0xA0 && track_control(t, out, &cmd)) {
                wr8(t, 0);
                return 0;
            }
            track_send(*out, cmd);
            f = rd8(t);
            if (f & 0x18) {         /* a fade starts: out from full, or in from silence */
                cmd = VOLUME;
                if (f & 8) {
                    f ^= 0x0A;
                } else {
                    f ^= 0x10;
                    cmd |= 0x7F00;
                }
                wr8(t, f);
                track_send(*out, cmd | rd8(t + T_ID));
            }
        } else if (kind == 0x90) {
            q = rd32(t + T_BASE) + w;
        } else if (w == 0) {
            track_end(t);
            return 0;
        } else {
            wr32(t + T_LIST, NO_LIST);
            *p = rd32(t + T_BASE) + w;
            return 1;
        }
    }
}

/* a track's events up to the next with time before it. Each is a MIDI-like status, kept running, and one or
   two bytes; a top bit set in its last byte says another follows at once */
static void track_read(uint32_t t) {
    uint32_t out = rd8(t + T_OUT), p = rd32(t + T_DATA), b, d, cmd, n;
    for (;;) {
        b = rd8(p);
        if (b == 0xFF) {
            if (!next_part(t, &out, &p)) {
                return;
            }
            continue;
        }
        if (b & 0x80) {
            p++;
            wr8(t + T_STATUS, b);
        } else {
            b = rd8(t + T_STATUS);
        }
        d = rd8(p++);
        cmd = b << 24 | (d & 0x7F) << 16;
        switch (b >> 4 & 7) {
        case 0:
        case 4:
            break;
        case 5:
            if (out & 0x80) {
                cmd &= ~0x40000000u;
                d = rd8(p++);
                cmd |= d << 8;
                d = rd8(p++);
                cmd |= (d & 0x7F) << 4;
            }
            break;
        case 6:
            cmd = (cmd & ~0xFF0000u) | (d & 0x7F) << 8;
            break;
        default:
            d = rd8(p++);
            cmd |= (d & 0x7F) << 8;
            break;
        }
        cmd |= rd8(t + T_ID);
        if (cmd >> 24 == 0xA0 && track_control(t, &out, &cmd)) {
            wr8(t, 0);
            return;
        }
        track_send(out, cmd);
        if (!(d & 0x80)) {
            break;
        }
    }
    n = rd8(p++);
    if (n & 0x80) {
        n = (rd8(p++) | n << 7) & ~0x4000u;
    }
    wr32(t + T_TIME, rd32(t + T_TIME) + (n << 16));
    wr32(t + T_DATA, p);
}

/* 0x68D8: each track's volume sent as it slides, the track ended at silence; and the events of each track
   that is due */
static void tracks(void) {
    uint32_t t, f, vol;
    for (t = TRACKS; t < TRACKS + 16 * TRACK_SIZE; t += TRACK_SIZE) {
        f = rd8(t);
        if (f == 0 || (f & T_HELD)) {
            continue;
        }
        if (f & T_SLIDE) {
            vol = rd32(t + T_VOL) & 0x7F0000;
            if (vol != rd32(t + T_VOL_SENT)) {
                wr32(t + T_VOL_SENT, vol);
                track_send(rd8(t + T_OUT), VOLUME | vol >> 8 | rd8(t + T_ID));
                if (vol == 0) {
                    track_end(t);
                    continue;
                }
                if (vol == 0x7F0000) {
                    wr8(t, f & ~6u);
                }
            }
        }
        if ((int32_t)rd32(t + T_TIME) >> 16 <= 0) {
            track_read(t);
        }
    }
}

static void main_pass(void) {
    pass_count();
    run_commands();
    mailbox();
    jobs();
    sh4_wait();
    channels_watch();
    midi_out();
    slots();
    load_meter();
    tracks();
}

/* whether a pass would do more than count itself, watch the channels and keep its meter */
static int work_waiting(void) {
    uint32_t t, f;
    if (rd32(WORK + 0x20) != 0 || rd32(Q_COUNT) != 0 || rd32(MAILBOX + rd32(SHARED + 4)) != 0) {
        return 1;
    }
    f = rd8(WORK + 10);
    if ((f == 1 && rd32(SHARED + 0x70) == 0) || (f == 2 && rd32(SHARED + 0x70) != 0)) {
        return 1;
    }
    f = rd8(WORK + 11);
    if (f == 1 || f == 2 || rd8(0xDD00) != 0 || (chip_rd32(0x28A0) & 0x200)) {
        return 1;
    }
    for (t = TRACKS; t < TRACKS + 16 * TRACK_SIZE; t += TRACK_SIZE) {
        f = rd8(t);
        if (f == 0 || (f & T_HELD)) {
            continue;
        }
        if ((f & T_SLIDE) && (rd32(t + T_VOL) & 0x7F0000) != rd32(t + T_VOL_SENT)) {
            return 1;
        }
        if ((int32_t)rd32(t + T_TIME) >> 16 <= 0) {
            return 1;
        }
    }
    return 0;
}

static int watch_max = 72;         /* MVC2_SNDWATCH: the channel watch's steps at most between ticks */
static int watch_fast = 1;          /* MVC2_WATCHFAST=0: each of them taken */
static int watch_memo = 1;          /* MVC2_WATCHMEMO=0: no round kept from one segment to the next */
static unsigned long m3_kept;       /* the steps taken along a kept round */

#define WATCH_STEPS 256

/* a round the watch went, kept while nothing it reads has changed: its places, the channel chosen before each
   step, and the one each step chose, if it chose */
static struct {
    int n;
    uint8_t place[WATCH_STEPS][3], before[WATCH_STEPS], chose[WATCH_STEPS], after[WATCH_STEPS];
    unsigned ram, chip, changes;
    uint64_t cycles;
} kept;

static void keep_round(const uint8_t (*place)[3], const uint8_t* chose, const int* chose_at, int from, int i, int last) {
    int k, n = i - from;
    for (k = 0; k < n; k++) {
        memcpy(kept.place[k], place[from + k], 3);
        kept.before[k] = chose[from + k];
        kept.chose[k] = (uint8_t)(k + 1 < n ? chose_at[from + k + 1] == from + k : last == i - 1);
        kept.after[k] = (uint8_t)(k + 1 < n ? chose[from + k + 1] : watch_chose);
    }
    kept.n = n;
    kept.ram = watch_ram;
    kept.chip = aica_watch_gen - watch_own;
    kept.changes = watch_changes;
    kept.cycles = arm7_cycles();
}

/* n steps along the kept round, if nothing it read has changed and the watch is on it: 1 if taken */
static int kept_steps(int n) {
    uint32_t w = rd8(WATCH), s = rd8(WATCH + 1), r = rd8(WATCH + 2);
    int k, e, j, c;
    if (kept.n == 0 || kept.ram != watch_ram || kept.chip != aica_watch_gen - watch_own ||
        kept.changes != watch_changes || kept.cycles != arm7_cycles()) {
        return 0;
    }
    for (k = 0; k < kept.n && (kept.place[k][0] != w || kept.place[k][1] != s || kept.place[k][2] != r); k++) {
    }
    if (k == kept.n || kept.before[k] != watch_chose) {
        return 0;
    }
    e = (k + n) % kept.n;
    in_watch = 1;
    wr8(WATCH, kept.place[e][0]);
    wr8(WATCH + 1, kept.place[e][1]);
    wr8(WATCH + 2, kept.place[e][2]);
    for (j = 0, c = (k + n - 1) % kept.n; j < n && j < kept.n; j++, c = c > 0 ? c - 1 : kept.n - 1) {
        if (kept.chose[c]) {
            watch_choose(kept.after[c]);
            break;
        }
    }
    in_watch = 0;
    kept.chip = aica_watch_gen - watch_own;
    m3_kept += (unsigned long)n;
    return 1;
}

/* n steps of the channel watch while the chip stands still. Once its place comes round again with nothing
   done since, the steps repeat: it goes to where the last of them would leave it. The first step isn't
   compared, as the channel it looks at may have been chosen by someone else */
static void watch_steps(int n) {
    static uint32_t seen[3 * 64 * 8], seen_at[3 * 64 * 8], round;
    uint8_t place[WATCH_STEPS][3], chose[WATCH_STEPS];
    int chose_at[WATCH_STEPS], i, from, end, last = -1;
    uint32_t w, s, r, key = 0;
    unsigned changes;
    if (!watch_fast || n > WATCH_STEPS || aica_tracing()) {
        for (i = 0; i < n; i++) {
            channels_watch();
        }
        return;
    }
    if (watch_memo && kept_steps(n)) {
        return;
    }
    changes = watch_changes;
    round++;
    for (i = 0; i < n; i++) {
        w = rd8(WATCH);
        s = rd8(WATCH + 1);
        r = rd8(WATCH + 2);
        if (i > 0 && w <= 2 && s < 64 && r < 8) {
            key = w << 9 | s << 3 | r;
            if (seen[key] == round) {
                break;
            }
            seen[key] = round;
            seen_at[key] = (uint32_t)i;
        }
        place[i][0] = (uint8_t)w;
        place[i][1] = (uint8_t)s;
        place[i][2] = (uint8_t)r;
        chose_at[i] = last;
        chose[i] = (uint8_t)watch_chose;
        watch_chosen = 0;
        channels_watch();
        if (watch_chosen) {
            last = i;
        }
        if (watch_changes != changes) {
            changes = watch_changes;
            round++;
        }
    }
    if (i == n) {
        return;
    }
    m3_repeats += (unsigned long)(n - i);
    from = (int)seen_at[key];
    end = from + (n - i) % (i - from);
    if (watch_memo) {
        keep_round((const uint8_t(*)[3])place, chose, chose_at, from, i, last);
    }
    if (end != from) {              /* else whole rounds: back where it is */
        in_watch = 1;
        wr8(WATCH, place[end][0]);
        wr8(WATCH + 1, place[end][1]);
        wr8(WATCH + 2, place[end][2]);
        if (chose_at[end] >= from) {
            watch_choose(chose[end]);   /* the last channel chosen before then */
        }
        in_watch = 0;
    }
}

/* n passes with nothing else to do */
static void idle_passes(int n) {
    if (n <= 0) {
        return;
    }
    add32(SHARED, (uint32_t)n);
    n = n < watch_max ? n : watch_max;
    watch_steps(n);
    m3_watched += (unsigned long)n;
    load_meter();
}

/* one pass run by the interpreter, an instruction at a time, from the top back to it */
static int interpreted_pass(void) {
    int n = 0;
    do {
        n += arm7_run(1);
    } while (arm7_pc() != PASS && n < (1 << 20));
    return n;
}

/* a pass in C from the top; what it hands to the interpreter is left there */
static void c_pass(void) {
    ARM7_REGS s;
    arm7_get_regs(&s);
    pass_sp = s.r[13];
    pass_arm = 0;
    if (setjmp(not_here) == 0) {
        main_pass();
    } else {
        note_missing();
        pass_arm = 2;
    }
}

static unsigned long passes, compared, handed, pass_wrong, missing_n[64];
static uint32_t missing_at[64];

static void note_missing(void) {
    int i;
    for (i = 0; i < 64 && missing_at[i] && missing_at[i] != missing; i++) {
    }
    if (i < 64) {
        missing_at[i] = missing;
        missing_n[i]++;
    }
}

static void pass_report(void) {
    char line[512];
    int i, n = 0;
    n += snprintf(line + n, sizeof line - (size_t)n,
                  "sound driver check: %lu passes, %lu whole in C, %lu handed off, %lu wrong; to do:", passes,
                  compared, handed, pass_wrong);
    for (i = 0; i < 64 && missing_at[i] && n < (int)sizeof line - 24; i++) {
        n += snprintf(line + n, sizeof line - (size_t)n, " %04lx %lu", (unsigned long)missing_at[i], missing_n[i]);
    }
    host_log("%s", line);
}

/* a pass by the interpreter, then undone and made by the C (with what it leaves to the interpreter); the
   interpreter's kept */
static int pass_checked(void) {
    ARM7_REGS before, after_i;
    uint32_t a;
    int ran, w;
    arm7_get_regs(&before);
    aica_state_save(chip0);
    memcpy(ram0, aica_ram, CHECK_RAM);
    log_now = log_i;
    log_n = 0;
    aica_write_log = logged;
    ran = interpreted_pass();
    aica_write_log = NULL;
    n_i = log_n;
    arm7_get_regs(&after_i);
    aica_state_save(chip_i);
    memcpy(ram_i, aica_ram, CHECK_RAM);
    passes++;

    arm7_set_regs(&before);
    aica_state_load(chip0);
    memcpy(aica_ram, ram0, CHECK_RAM);
    log_now = log_c;
    log_n = 0;
    aica_write_log = logged;
    c_pass();
    if (pass_arm == 2) {
        handed++;
        (void)interpreted_pass();
    } else if (pass_arm == 0) {
        compared++;
    }
    aica_write_log = NULL;
    n_c = log_n;
    if ((w = writes_differ()) >= 0) {
        pass_wrong++;
        if (pass_wrong <= 20 && (w >= LOG_N || w >= n_i || w >= n_c)) {
            host_log("sound driver check: pass %lu wrong: %d register writes against %d", passes, n_i, n_c);
        } else if (pass_wrong <= 20) {
            host_log("sound driver check: pass %lu wrong: register write %d, %04lx=%04lx against %04lx=%04lx", passes,
                     w, (unsigned long)log_i[w].off, (unsigned long)log_i[w].v, (unsigned long)log_c[w].off,
                     (unsigned long)log_c[w].v);
        }
    } else if ((a = ram_differs()) < CHECK_RAM) {
        pass_wrong++;
        if (pass_wrong <= 20) {
            host_log("sound driver check: pass %lu wrong: RAM byte %04lx, %02x against %02x", passes,
                     (unsigned long)a, ram_i[a], aica_ram[a]);
        }
    }
    if ((passes & 4095) == 0) {
        pass_report();
    }
    arm7_set_regs(&after_i);
    aica_state_load(chip_i);
    memcpy(aica_ram, ram_i, CHECK_RAM);
    return ran;
}

/* ---- from aica.c ---- */

void snd_drv_set(int m) {
    const char* e = getenv("MVC2_SNDWATCH");
    mode = m;
    if (e != NULL) {
        watch_max = atoi(e);
    }
    e = getenv("MVC2_WATCHFAST");
    if (e != NULL) {
        watch_fast = atoi(e);
    }
    e = getenv("MVC2_WATCHMEMO");
    if (e != NULL) {
        watch_memo = atoi(e);
    }
}

int snd_drv_fiq(void) {
    if (!mode || !arm7_fiq_due() || !driver_ok()) {
        return 0;
    }
    if (chip_rd8(INTREQ) == 2 && !(chip_rd32(MIDI_IN) & 0x100)) {
        return 0;                   /* MIDI in: the interpreter's */
    }
    if (mode == 2 || mode == 4) {
        fiq_checked();
    } else {
        (void)fiq_run();
    }
    return 1;
}

/* the interpreter, an instruction at a time, back to the top of the pass */
static int to_pass(int cycles) {
    int n = 0;
    while (arm7_pc() != PASS && n < cycles) {
        n += arm7_run(1);
    }
    m3_arm += (unsigned long)n;
    return n;
}

int snd_drv_main(int cycles, int rate) {
    int done = 0, n;
    if (mode < 3 || !driver_ok()) {
        return -1;
    }
    if (mode == 4) {
        if (arm7_pc() != PASS || !check_ready()) {
            return -1;
        }
        while (done < cycles) {
            done += arm7_fiq_line() ? interpreted_pass() : pass_checked();
        }
        return done;
    }
    /* as at the sound CPU's real speed: the passes with something to do, at once, then the idle ones */
    for (n = 0; n < PASS_CAP; n++) {
        if (arm7_pc() != PASS) {
            done += to_pass(cycles - done);
            if (arm7_pc() != PASS) {
                return done;
            }
        }
        if (!work_waiting()) {
            break;
        }
        c_pass();
        m3_passes++;
    }
    if (n == PASS_CAP && work_waiting() && m3_capped++ == 0) {
        host_log("sound driver: a segment hit its %d passes with work waiting", PASS_CAP);
    }
    if (done < cycles) {
        n = (int)((int64_t)(cycles - done) * REAL_RATE / (rate * IDLE_PASS));
        idle_passes(n);
        m3_idle += (unsigned long)n;
    }
    if ((++m3_calls & 0x7FFF) == 0) {
        host_log("sound driver: %lu passes in C, %lu idle, %lu cycles interpreted, %lu segments capped, %lu of %lu watch steps repeats, %lu along a kept round",
                 m3_passes, m3_idle, m3_arm, m3_capped, m3_repeats, m3_watched, m3_kept);
    }
    return done > cycles ? done : cycles;
}
