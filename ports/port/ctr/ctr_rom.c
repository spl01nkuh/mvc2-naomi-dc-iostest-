/*
 * ctr_rom.c  The cartridge ROM on the 3DS: one image file on the SD card (mvsc2.bin, unpacked the first
 * time), its blocks read ahead on the system core after each miss. A read costs about 10 ms before its first
 * byte, so blocks side by side are read together, up to a piece.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <3ds.h>

#include "naomi_host.h"
#include "ctr_front.h"
#include "ctr_render.h"
#include "prof.h"

#define CACHE_LEAST (2u << 20)
#define BLOCK_SIZE 0x10000u
#define BUFS (CTR_ROM_BUFFERS / BLOCK_SIZE)
#define AHEAD 8                 /* blocks read past a request's end */
#define PIECE_MOST (BUFS / 2)
#define HELD_STALE 600          /* frames: with MVC2_ROM_DIRECT, a held block let go into the cache when its stream
                                   has stopped reading for so long */

enum { B_FREE, B_QUEUED, B_READING, B_DONE };

typedef struct {
    uint32_t block;
    uint32_t age;               /* the order queued */
    volatile uint32_t state;
    int held;                   /* with MVC2_ROM_ROLL: past its request, kept here until read, so its read rolls on */
    uint32_t piece_no;          /* the blocks queued together; with MVC2_ROM_DIRECT read together only */
    unsigned touched;           /* the frame its stream last read a block before it */
    uint8_t* data;
} BUF;

static BUF bufs[BUFS];
static uint32_t piece_clock;
static uint8_t* buf_mem;
static LightLock lock;
static LightEvent wake;
static Thread thread;
static volatile int quit;
static Handle fh_thread, fh_main;
static uint32_t file_blocks, queue_clock;
static uint32_t piece = PIECE_MOST;     /* the most blocks a read takes (MVC2_ROM_PIECE, in KB) */
static int is_open;
static int roll;                /* MVC2_ROM_ROLL=1: a stream's read-ahead goes on as it reads, not only after a miss */
static uint32_t n_sync, n_sync_reads, n_served, n_waited, n_ahead;  /* for the log, each 600 frames */
static int trace;               /* MVC2_ROM_TRACE=1: each read logged, with its time */
static uint32_t ahead = AHEAD;  /* MVC2_ROM_AHEAD; with MVC2_ROM_ROLL also the most of a stream's blocks kept in the
                                   buffers until it reads them */
static int direct;              /* MVC2_ROM_DIRECT=1: ctr_rom_dma */
static uint32_t load_chunk;     /* the load queue's request, MVC2_LOAD_KB */
static uint32_t n_direct, n_direct_kb;
static int in_dma;              /* ctr_rom_dma reading through the cache: its stream read ahead there, not here */
static uint32_t sync_last;      /* and the last block a miss reads with it, past its request at a stream's start */
static unsigned log_frame;

/* MVC2_ROM_PRELOAD=1: the title's loads read by the reader's thread when it has nothing else to read, from frame
   PRELOAD_FROM, one at a time while the heap keeps PRELOAD_LEAVE free past them; each given to the game once and let
   go, with those before it in the list that it has not asked for. One for the cache is put there when the game is
   given the one before it. */
#define PRELOAD_FROM 60
#define PRELOAD_LEAVE (4u << 20)
#define PRELOAD_PIECE (128u << 10)      /* a read of one, so that a miss waits for no more */
static const struct {
    uint32_t at, size;
    int cache;
} title_load[] = {
    { 0x012A4120, 0x01E100, 0 },        /* the attract, or a coin put in */
    { 0x0755BD40, 0x100000, 0 },
    { 0x023AC720, 0x200000, 0 },
    { 0x012A4120, 0x01E100, 0 },        /* a coin put in after it */
    { 0x021E7C60, 0x184AC0, 0 },        /* START */
    { 0x08030000, 0x030000, 1 },        /* the blocks of the file it unpacks next */
    { 0x0238C720, 0x020000, 0 },
};
#define PRELOADS (int)(sizeof title_load / sizeof *title_load)
#define START_LOAD 4
enum { P_WAIT, P_READING, P_GONE };
static struct {
    uint8_t* data;
    uint32_t done;                      /* the bytes read so far */
    int state;
    int failed;
} pre[PRELOADS];                        /* under lock */
static int preload, pre_busy = -1;      /* the one the thread is reading into */
extern char* fake_heap_end;

/* The game's table of its files in the ROM, an offset and a size each (packed ones with the top bit set), checked
   against its first and last; with MVC2_ROM_DIRECT, where a stream's file ends. With MVC2_ROM_PRELOAD the select
   screen's portraits are read into the cache PORTRAITS_AFTER frames after START. A pick's portraits bring their characters'
   files read ahead: the small one PICK_FILE past each, and the first blocks of the big one PICK_BIG past it once
   the stream of the one the game loads before it starts. */
#define FILE_TABLE 0x2085D4u
#define FILE_COUNT 834
#define PORTRAIT_FIRST 3
#define PORTRAIT_LAST 61
#define PICK_FILE 60
#define PICK_BIG 194
#define PICKS 8
#define HEAD_BLOCKS 3
#define STAGE_FIRST 789         /* the stages' files, two each, and the music each fight on one plays */
#define STAGE_LAST 806
#define STAGE_MUSIC 124
#define ATTRACT_MUSIC 135       /* its head read into the cache from PRELOAD_FROM */
#define PORTRAITS_AFTER 30
static unsigned portraits_at;   /* the frame to read the portraits from, or 0 */
static int attract_music;
static uint32_t* file_tab;
static unsigned file_last_found;
static int picked[PICKS], n_picked, next_pick;   /* the big files, in the order the game loads them */
static unsigned pick_frame;

/* the blocks to read into the cache when the thread has nothing else to read */
#define WANTS 16
static struct {
    uint32_t b, last;
} wants[WANTS];
static int n_wants;

/* a stream of the game's DMA: the last block read or queued ahead, its file's last block, the next piece's size */
static uint32_t s_end, s_file_last, s_step;
static int s_known;             /* its file in the table */

static float ms_since(u64 t0) {
    return (float)(svcGetSystemTick() - t0) * 1000.0f / SYSCLOCK_ARM11;
}

static int raw_read(Handle h, uint32_t b, uint32_t n, void* dst) {
    u32 got = 0;
    return R_SUCCEEDED(FSFILE_Read(h, &got, (u64)b * BLOCK_SIZE, dst, n * BLOCK_SIZE)) && got == n * BLOCK_SIZE;
}

static int open_raw(const char* path, Handle* h) {
    return strncmp(path, "sdmc:", 5) == 0 &&
           R_SUCCEEDED(FSUSER_OpenFileDirectly(h, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""),
                                               fsMakePath(PATH_ASCII, path + 5), FS_OPEN_READ, 0));
}

static void reader_main(void* arg) {
    (void)arg;
    while (!quit) {
        BUF* next = NULL;
        uint32_t n = 1, k;
        int i;
        LightLock_Lock(&lock);
        for (i = 0; i < BUFS; i++) {
            if (bufs[i].state == B_QUEUED && (next == NULL || bufs[i].age < next->age)) {
                next = &bufs[i];
            }
        }
        if (next == NULL && preload) {
            /* else the next piece of a load the title will ask for */
            for (i = 0; i < PRELOADS; i++) {
                if (pre[i].state == P_READING && !pre[i].failed && pre[i].done < title_load[i].size) {
                    break;
                }
            }
            pre_busy = i < PRELOADS ? i : -1;
        }
        if (next != NULL) {
            /* with the queued blocks after it in the buffers after it; with MVC2_ROM_DIRECT those queued with it, so
               that a miss waits for no more than one piece */
            i = (int)(next - bufs);
            while (n < piece && i + (int)n < BUFS && bufs[i + n].state == B_QUEUED && bufs[i + n].block == next->block + n &&
                   (!direct || bufs[i + n].piece_no == next->piece_no)) {
                n++;
            }
            for (k = 0; k < n; k++) {
                next[k].state = B_READING;
            }
        }
        LightLock_Unlock(&lock);
        if (next == NULL && pre_busy >= 0) {
            uint32_t at = pre[pre_busy].done, len = title_load[pre_busy].size - at;
            u32 got = 0;
            int ok;
            len = len < PRELOAD_PIECE ? len : PRELOAD_PIECE;
            PROF_START(t);
            ok = R_SUCCEEDED(FSFILE_Read(fh_thread, &got, title_load[pre_busy].at + at, pre[pre_busy].data + at, len)) &&
                 got == len;
            PROF_STOP(PZ_ROM_THREAD, t);
            LightLock_Lock(&lock);
            pre[pre_busy].done += ok ? len : 0;
            pre[pre_busy].failed = !ok;
            pre_busy = -1;
            LightLock_Unlock(&lock);
            continue;
        }
        if (next == NULL) {
            LightEvent_Wait(&wake);
            continue;
        }
        {
            u64 t0 = svcGetSystemTick();
            PROF_START(t);
            if (!raw_read(fh_thread, next->block, n, next->data)) {
                memset(next->data, 0xFF, n * BLOCK_SIZE);
            }
            PROF_STOP(PZ_ROM_THREAD, t);
            PROF_COUNT(PZ_ROM_THREAD, n);
            if (trace) {
                host_log("rom %u: %lX-%lX read ahead, %.1f ms", host_frame(), (unsigned long)next->block,
                         (unsigned long)(next->block + n - 1), ms_since(t0));
            }
        }
        LightLock_Lock(&lock);
        for (k = 0; k < n; k++) {
            next[k].state = B_DONE;
        }
        n_ahead += n;
        LightLock_Unlock(&lock);
    }
}

static BUF* find(uint32_t b) {
    int i;
    for (i = 0; i < BUFS; i++) {
        if (bufs[i].state != B_FREE && bufs[i].block == b) {
            return &bufs[i];
        }
    }
    return NULL;
}

/* the blocks read into the cache, their buffers free again; with MVC2_ROM_ROLL, a stream's kept until read, but
   for the oldest past ahead */
static void put_done(void) {
    int i, held = 0;
    for (i = 0; i < BUFS; i++) {
        if (direct && bufs[i].state == B_DONE && bufs[i].held && host_frame() - bufs[i].touched > HELD_STALE) {
            bufs[i].held = 0;
        }
        held += bufs[i].state != B_FREE && bufs[i].held;
    }
    while (held > (int)ahead) {
        BUF* old = NULL;
        for (i = 0; i < BUFS; i++) {
            if (bufs[i].state == B_DONE && bufs[i].held && (old == NULL || bufs[i].age < old->age)) {
                old = &bufs[i];
            }
        }
        if (old == NULL) {
            break;
        }
        old->held = 0;
        held--;
    }
    for (i = 0; i < BUFS; i++) {
        if (bufs[i].state == B_DONE && !bufs[i].held) {
            naomi_rom_put(bufs[i].block, bufs[i].data);
            LightLock_Lock(&lock);
            bufs[i].state = B_FREE;
            LightLock_Unlock(&lock);
        }
    }
}

/* the first run of free buffers *n long, else the longest, its length then in *n; -1 for none */
static int free_run(uint32_t* n) {
    uint32_t len = 0, most = 0;
    int i, at = -1, best = -1;
    for (i = 0; i <= BUFS; i++) {
        if (i < BUFS && bufs[i].state == B_FREE) {
            if (len++ == 0) {
                at = i;
            }
            if (len == *n) {
                return at;
            }
        } else {
            if (len > most) {
                most = len;
                best = at;
            }
            len = 0;
        }
    }
    *n = most;
    return best;
}

/* from a buffer that has it or is reading it, else read now: with the request's blocks after it that no buffer
   has, up to a piece, into free buffers side by side */
static int rom_read(uint32_t b, uint32_t last, void* dst) {
    BUF* found;
    uint32_t n = 1, k;
    int first = -1, ok, waited = 0;
    u64 t0 = svcGetSystemTick();
    PROF_START(t);
    PROF_PLACE(PP_ROM_READ);
    LightLock_Lock(&lock);
    found = find(b);
    if (found != NULL && found->state == B_QUEUED) {
        found->state = B_FREE;
        found = NULL;
    }
    LightLock_Unlock(&lock);
    if (found == NULL) {
        /* the blocks read ahead so far into the cache: a load can take many of them in one frame */
        put_done();
    }
    LightLock_Lock(&lock);
    if (found == NULL) {
        uint32_t to = sync_last > last ? sync_last : last;
        while (n < piece && b + n <= to && b + n < file_blocks && !naomi_rom_cached(b + n)) {
            BUF* q = find(b + n);
            if (q != NULL) {
                if (q->state != B_QUEUED) {
                    break;
                }
                q->state = B_FREE;
            }
            n++;
        }
        if (n > 1 && (first = free_run(&n)) >= 0 && n > 1) {
            for (k = 0; k < n; k++) {
                bufs[first + k].block = b + k;
                bufs[first + k].state = B_READING;
                bufs[first + k].held = 0;
            }
        } else {
            n = 1;
            first = -1;
        }
    }
    LightLock_Unlock(&lock);
    if (found == NULL && first < 0) {
        ok = b < file_blocks && raw_read(fh_main, b, 1, dst);
        PROF_COUNT(PZ_ROM_MISS, 1);
        n_sync++;
        n_sync_reads++;
    } else if (found == NULL) {
        if (!(ok = raw_read(fh_main, b, n, bufs[first].data))) {
            memset(bufs[first].data, 0xFF, n * BLOCK_SIZE);
        }
        memcpy(dst, bufs[first].data, BLOCK_SIZE);
        LightLock_Lock(&lock);
        bufs[first].state = B_FREE;
        for (k = 1; k < n; k++) {
            bufs[first + k].state = B_DONE;
        }
        LightLock_Unlock(&lock);
        PROF_COUNT(PZ_ROM_MISS, n);
        n_sync += n;
        n_sync_reads++;
    } else {
        if (found->state == B_READING) {
            n_waited++;
            waited = 1;
        } else {
            n_served++;
        }
        while (found->state == B_READING) {
            svcSleepThread(100000);
        }
        memcpy(dst, found->data, BLOCK_SIZE);
        LightLock_Lock(&lock);
        found->state = B_FREE;
        found->held = 0;
        LightLock_Unlock(&lock);
        ok = 1;
    }
    PROF_STOP(PZ_ROM_MISS, t);
    if (trace) {
        host_log("rom %u: %lX (to %lX) %s %lu, %.1f ms", host_frame(), (unsigned long)b, (unsigned long)last,
                 found == NULL ? "read" : waited ? "waited for" : "taken", (unsigned long)(found == NULL ? n : 1),
                 ms_since(t0));
    }
    return ok;
}

static uint32_t req_last = 0xFFFFFFFFu, req_end;     /* the last request missed in, and how far it is read ahead */
static uint32_t req_stop = 0xFFFFFFFFu;                 /* the last block of a sound stream going on from it */
static uint32_t snd_first;                              /* that stream's first block, when it loops */
#define LOOP_BLOCKS 4                                   /* of its first blocks read ahead, for the first held one to go on from */
static int snd_loop;

/* block p queued for the thread, in the buffer at *at where it is free, else any free one, else the oldest queued
   one past last; under lock. 0 when a buffer has it or the cache does, -1 for no buffer */
static int queue_one(uint32_t p, uint32_t last, int held, int* at) {
    BUF* use;
    int i;
    if ((use = find(p)) != NULL) {
        use->touched = host_frame();
        return 0;
    }
    if (naomi_rom_cached(p)) {
        return 0;
    }
    if (*at >= 0 && *at < BUFS && bufs[*at].state == B_FREE) {
        use = &bufs[*at];
    }
    for (i = 0; i < BUFS && use == NULL; i++) {
        if (bufs[i].state == B_FREE) {
            use = &bufs[i];
        }
    }
    for (i = 0; i < BUFS && use == NULL; i++) {
        if (bufs[i].state == B_QUEUED && bufs[i].block > last && (use == NULL || bufs[i].age < use->age)) {
            use = &bufs[i];
        }
    }
    if (use == NULL) {
        return -1;
    }
    use->block = p;
    use->age = ++queue_clock;
    use->state = B_QUEUED;
    use->held = held;
    use->piece_no = piece_clock;
    use->touched = host_frame();
    *at = (int)(use - bufs) + 1;
    return 1;
}

/* after a miss: the rest of the request it was in, and for a stream the blocks after; for a sound stream that
   loops (ctr_rom_stream), its first blocks too once its read-ahead reaches its end */
static void rom_missed(uint32_t b, uint32_t last) {
    uint32_t p, want = piece;
    int at, got = 0, queued = 0;
    if (thread == NULL) {
        return;
    }
    if (last != req_last) {
        /* past a request's end only when it goes on from the one before, as a stream does */
        int on = !in_dma && req_last != 0xFFFFFFFFu && b > req_last && b <= req_end + 1;
        int again = !in_dma && snd_loop && b >= snd_first && b < snd_first + LOOP_BLOCKS && b < req_last;
        if (!on && !again) {
            req_stop = 0xFFFFFFFFu;
            snd_loop = 0;
        }
        req_end = on || again ? (last + ahead < req_stop ? last + ahead : req_stop) : last;
        req_last = last;
    }
    put_done();
    LightLock_Lock(&lock);
    /* side by side where it can, for the thread to read together */
    at = free_run(&want);
    piece_clock++;
    for (p = b + 1; p <= req_end && p < file_blocks && (got = queue_one(p, last, roll && p > last, &at)) >= 0; p++) {
        queued |= got;
    }
    if (snd_loop && got >= 0 && req_end == req_stop && last + ahead > req_stop) {
        uint32_t k, n = last + ahead - req_stop;
        for (k = 0; k < n && k < LOOP_BLOCKS && snd_first + k < req_stop &&
                    (got = queue_one(snd_first + k, last, roll, &at)) >= 0;
             k++) {
            queued |= got;
        }
    }
    LightLock_Unlock(&lock);
    if (trace && queued) {
        host_log("rom %u: %lX-%lX queued", host_frame(), (unsigned long)(b + 1), (unsigned long)(p - 1));
    }
    if (queued) {
        LightEvent_Signal(&wake);
    }
}

/* in a buffer, read or being read */
static int buffered(uint32_t b) {
    BUF* f;
    int in;
    LightLock_Lock(&lock);
    f = find(b);
    in = f != NULL && f->state != B_QUEUED;
    LightLock_Unlock(&lock);
    return in;
}

static int have(uint32_t b) {
    return naomi_rom_cached(b) || buffered(b);
}

/* the queued blocks from b to last let go */
static void unqueue(uint32_t b, uint32_t last) {
    int i;
    LightLock_Lock(&lock);
    for (i = 0; i < BUFS; i++) {
        if (bufs[i].state == B_QUEUED && bufs[i].block >= b && bufs[i].block <= last) {
            bufs[i].state = B_FREE;
            bufs[i].held = 0;
        }
    }
    LightLock_Unlock(&lock);
}

/* the file the byte at is in, from the game's table; -1 for none */
static int file_of(uint32_t at) {
    unsigned i, k;
    if (file_tab == NULL) {
        return -1;
    }
    for (k = 0; k < FILE_COUNT; k++) {
        i = (file_last_found + k) % FILE_COUNT;
        if ((file_tab[2 * i] & 0x80000000u) == 0 && at - file_tab[2 * i] < file_tab[2 * i + 1]) {
            file_last_found = i;
            return (int)i;
        }
    }
    return -1;
}

static uint32_t file_last_block(int f) {
    return (file_tab[2 * f] + file_tab[2 * f + 1] - 1) / BLOCK_SIZE;
}

static int known(uint32_t b) {
    BUF* f;
    LightLock_Lock(&lock);
    f = find(b);
    LightLock_Unlock(&lock);
    return f != NULL || naomi_rom_cached(b);
}

/* the blocks from b to last that neither the cache nor a buffer has, queued for the thread as one piece, side by side
   where buffers are free; the first block not queued */
static uint32_t queue_piece(uint32_t b, uint32_t last) {
    uint32_t want = last - b + 1, p, n = 0;
    int i, at;
    put_done();
    LightLock_Lock(&lock);
    at = free_run(&want);
    piece_clock++;
    for (p = b; p <= last && p < file_blocks; p++) {
        BUF* use = NULL;
        if (naomi_rom_cached(p) || find(p) != NULL) {
            continue;
        }
        if (at >= 0 && at < BUFS && bufs[at].state == B_FREE) {
            use = &bufs[at];
        }
        for (i = 0; i < BUFS && use == NULL; i++) {
            if (bufs[i].state == B_FREE) {
                use = &bufs[i];
            }
        }
        if (use == NULL) {
            break;
        }
        use->block = p;
        use->age = ++queue_clock;
        use->state = B_QUEUED;
        use->held = 0;
        use->piece_no = piece_clock;
        at = (int)(use - bufs) + 1;
        n++;
    }
    LightLock_Unlock(&lock);
    if (n != 0) {
        if (trace) {
            host_log("rom %u: %lX-%lX queued", host_frame(), (unsigned long)b, (unsigned long)(p - 1));
        }
        LightEvent_Signal(&wake);
    }
    return p;
}

/* the blocks from b to last to read into the cache when the thread is free */
static void want(uint32_t b, uint32_t last) {
    int i;
    for (i = 0; i < n_wants; i++) {
        if (wants[i].b <= b && wants[i].last >= last) {
            return;
        }
    }
    if (n_wants < WANTS) {
        wants[n_wants].b = b;
        wants[n_wants].last = last;
        n_wants++;
    }
}

/* a stream of the game's DMA from a new place: within its file where the table has it, its first read then taking
   two blocks more and the pieces after it 256 KB and then 512; else 64, 256 and 512. Into the next file too where
   the stream went on into this one from the file before it (chained), and for a stage's first file. A pick's small
   file or big one brings the first blocks of the next big one, and a stage's file its music's. */
static void stream_start(uint32_t offset, uint32_t last, int chained) {
    int f = file_of(offset), i;
    if (f >= 0 && n_picked > 0) {
        for (i = next_pick; i < n_picked && picked[i] != f; i++) {
        }
        if (i < n_picked || (f >= PORTRAIT_FIRST + PICK_FILE && f <= PORTRAIT_LAST + PICK_FILE)) {
            next_pick = i < n_picked ? i + 1 : next_pick;
            if (next_pick < n_picked) {
                uint32_t b = file_tab[2 * picked[next_pick]] / BLOCK_SIZE, to = file_last_block(picked[next_pick]);
                want(b, b + HEAD_BLOCKS - 1 < to ? b + HEAD_BLOCKS - 1 : to);
            }
        }
    }
    if (preload && f >= STAGE_FIRST && f <= STAGE_LAST) {
        uint32_t b = file_tab[2 * (STAGE_MUSIC + (f - STAGE_FIRST) / 2)] / BLOCK_SIZE;
        want(b, b + 1);
    }
    s_end = last;
    s_file_last = f >= 0 ? file_last_block(f) : file_blocks - 1;
    if (f >= 0 && f + 1 < FILE_COUNT && file_tab[2 * (f + 1)] == file_tab[2 * f] + file_tab[2 * f + 1] &&
        (chained || (f >= STAGE_FIRST && f < STAGE_LAST && (f - STAGE_FIRST) % 2 == 0))) {
        s_file_last = file_last_block(f + 1);
    }
    s_step = f >= 0 ? 4 : 1;
    s_known = f >= 0;
    sync_last = f >= 0 ? (last + 2 < s_file_last ? last + 2 : s_file_last) : 0;
}

/* after each of its requests: the next piece queued once fewer than half of MVC2_ROM_AHEAD are ahead of it */
static void stream_ahead(uint32_t last) {
    uint32_t low = ahead / 2 > 0 ? ahead / 2 : 1, to;
    if (s_end < last) {
        s_end = last;
    }
    while (s_end < s_file_last && known(s_end + 1)) {
        s_end++;
    }
    if (s_end - last >= low || s_end >= s_file_last) {
        return;
    }
    to = s_end + s_step < s_file_last ? s_end + s_step : s_file_last;
    s_end = queue_piece(s_end + 1, to) - 1;
    s_step = s_step * 4 < piece ? s_step * 4 : piece;
}

/* the next piece of the first want, when the thread has nothing else to read */
static void feed_wants(void) {
    int i, queued = 0;
    LightLock_Lock(&lock);
    for (i = 0; i < BUFS; i++) {
        queued |= bufs[i].state == B_QUEUED || bufs[i].state == B_READING;
    }
    LightLock_Unlock(&lock);
    while (!queued && n_wants > 0) {
        uint32_t b = wants[0].b, to;
        while (b <= wants[0].last && known(b)) {
            b++;
        }
        if (b > wants[0].last) {
            memmove(&wants[0], &wants[1], (size_t)(n_wants - 1) * sizeof *wants);
            n_wants--;
            continue;
        }
        to = b + piece - 1 < wants[0].last ? b + piece - 1 : wants[0].last;
        wants[0].b = queue_piece(b, to);
        break;
    }
}

/* MVC2_ROM_PRELOAD: START brings the select screen's portraits into the cache soon after, and a pick's portrait its
   file */
static void note_load(uint32_t offset, uint32_t size) {
    int f;
    if (file_tab == NULL) {
        return;
    }
    if (offset == title_load[START_LOAD].at && size == title_load[START_LOAD].size) {
        portraits_at = host_frame() + PORTRAITS_AFTER;
        return;
    } else if ((f = file_of(offset)) >= PORTRAIT_FIRST && f <= PORTRAIT_LAST && file_tab[2 * f] == offset &&
               file_tab[2 * f + 1] == size && file_tab[2 * (f + PICK_FILE) + 1] != 0) {
        if (host_frame() - pick_frame > 60) {
            n_picked = next_pick = 0;
        }
        pick_frame = host_frame();
        if (n_picked < PICKS && file_tab[2 * (f + PICK_BIG) + 1] != 0) {
            picked[n_picked++] = f + PICK_BIG;
        }
        want(file_tab[2 * (f + PICK_FILE)] / BLOCK_SIZE, file_last_block(f + PICK_FILE));
    } else {
        return;
    }
    feed_wants();
}

/* MVC2_ROM_PRELOAD: the file START unpacks next, unpacked from the blocks read ahead for it by a thread on the game's
   core below the game, so in the time the game waits for a frame */
#define START_PACKED_AT 0x0803BDA0u
#define START_PACKED_SIZE 0x1E5E0u
#define START_BLOCKS (START_LOAD + 1)
int ctr_m1_unpack(uint32_t src, uint8_t* dst, uint32_t size, const uint8_t* mem, uint32_t at, uint32_t len);
static Thread unpacker;
static uint8_t* unpacked;
static volatile int unpack_ok;
static int unpack_tried;
static s32 game_prio = 0x30;

static void unpack_main(void* arg) {
    (void)arg;
    unpack_ok = ctr_m1_unpack(START_PACKED_AT, unpacked, START_PACKED_SIZE, pre[START_BLOCKS].data,
                              title_load[START_BLOCKS].at, title_load[START_BLOCKS].size);
}

static void unpack_wait(void) {
    if (unpacker != NULL) {
        threadJoin(unpacker, U64_MAX);
        threadFree(unpacker);
        unpacker = NULL;
    }
}

/* once its blocks are all read, between frames */
static void start_unpack(void) {
    int ready;
    if (unpack_tried) {
        return;
    }
    LightLock_Lock(&lock);
    ready = pre[START_BLOCKS].state == P_READING && !pre[START_BLOCKS].failed &&
            pre[START_BLOCKS].done == title_load[START_BLOCKS].size && pre_busy != START_BLOCKS;
    LightLock_Unlock(&lock);
    if (!ready || (uint32_t)(fake_heap_end - (char*)sbrk(0)) < START_PACKED_SIZE + PRELOAD_LEAVE) {
        return;
    }
    unpack_tried = 1;
    unpack_ok = 0;
    if ((unpacked = host_try_malloc(START_PACKED_SIZE)) != NULL &&
        (unpacker = threadCreate(unpack_main, NULL, 16 * 1024, game_prio + 2 < 0x3F ? game_prio + 2 : 0x3F, 0, false)) ==
            NULL) {
        free(unpacked);
        unpacked = NULL;
    }
}

/* the game's packed read (naomi_board.c), 1 when given here */
int ctr_rom_packed(uint32_t offset, void* dst, uint32_t size) {
    int ok;
    if (!is_open || unpacked == NULL || offset != START_PACKED_AT || size != START_PACKED_SIZE) {
        return 0;
    }
    unpack_wait();
    if ((ok = unpack_ok) != 0) {
        memcpy(dst, unpacked, size);
    }
    free(unpacked);
    unpacked = NULL;
    host_log("rom: the packed file at %lX %s at frame %u (MVC2_ROM_PRELOAD)", (unsigned long)offset,
             ok ? "given unpacked ahead" : "not unpacked ahead", host_frame());
    return ok;
}

/* the title's load i, from the thread when it is not reading into it */
static uint8_t* take_preload(int i, uint32_t* done) {
    uint8_t* data;
    if (i == START_BLOCKS) {
        unpack_wait();
    }
    LightLock_Lock(&lock);
    pre[i].state = P_GONE;
    while (pre_busy == i) {
        LightLock_Unlock(&lock);
        svcSleepThread(100000);
        LightLock_Lock(&lock);
    }
    data = pre[i].data;
    *done = pre[i].done;
    pre[i].data = NULL;
    LightLock_Unlock(&lock);
    return data;
}

/* MVC2_ROM_PRELOAD: one of the title's loads, given from what the thread has read of it and the rest read now */
static int give_preload(uint32_t offset, void* dst, uint32_t size) {
    uint32_t done = 0;
    uint8_t* data;
    int i, j;
    for (i = 0; i < PRELOADS && !(title_load[i].at == offset && title_load[i].size == size && !title_load[i].cache &&
                                  pre[i].state == P_READING);
         i++) {
    }
    if (i == PRELOADS) {
        return 0;
    }
    data = take_preload(i, &done);
    memcpy(dst, data, done);
    if (done < size) {
        u32 got = 0;
        PROF_START(t);
        if (R_FAILED(FSFILE_Read(fh_main, &got, offset + done, (uint8_t*)dst + done, size - done)) || got != size - done) {
            naomi_rom_read(offset + done, (uint8_t*)dst + done, size - done);
        }
        PROF_STOP(PZ_ROM_MISS, t);
    }
    free(data);
    host_log("rom: the title's load from %lX given at frame %u, %lu of its %lu KB read ahead (MVC2_ROM_PRELOAD)",
             (unsigned long)offset, host_frame(), (unsigned long)done >> 10, (unsigned long)size >> 10);
    for (j = 0; j < i; j++) {
        if (pre[j].state != P_GONE) {
            free(take_preload(j, &done));
        }
    }
    if (i + 1 < PRELOADS && title_load[i + 1].cache && pre[i + 1].state == P_READING) {
        uint32_t k;
        data = take_preload(i + 1, &done);
        for (k = 0; k < done / BLOCK_SIZE; k++) {
            naomi_rom_put(title_load[i + 1].at / BLOCK_SIZE + k, data + k * BLOCK_SIZE);
        }
        free(data);
        host_log("rom: %lu KB from %lX put into the cache (MVC2_ROM_PRELOAD)", (unsigned long)(k * (BLOCK_SIZE >> 10)),
                 (unsigned long)title_load[i + 1].at);
    }
    return 1;
}

/* the next of the title's loads given memory and the thread woken for it, between frames */
static void start_preload(void) {
    int i;
    for (i = 0; i < PRELOADS && pre[i].state != P_WAIT; i++) {
        if (pre[i].state == P_READING && !pre[i].failed && pre[i].done < title_load[i].size) {
            return;
        }
    }
    if (i < PRELOADS && (uint32_t)(fake_heap_end - (char*)sbrk(0)) >= title_load[i].size + PRELOAD_LEAVE &&
        (pre[i].data = host_try_malloc(title_load[i].size)) != NULL) {
        LightLock_Lock(&lock);
        pre[i].done = 0;
        pre[i].failed = 0;
        pre[i].state = P_READING;
        LightLock_Unlock(&lock);
        LightEvent_Signal(&wake);
    }
}

/* MVC2_ROM_DIRECT=1, the game's DMA from the ROM (naomi_board.c), 1 when done here. From a new place, each run of
   blocks that neither the cache nor the buffers have is read in one read straight into the game's memory, only the
   bytes asked for, and not cached. A stream (a request where the last one ended), the load queue's requests and
   one from where such a read was made before go through the cache. A stream is read ahead in growing pieces up to
   its file's end, or its own: a request shorter than the one before. */
static int dma_direct(uint32_t offset, void* dst, uint32_t size, int stream, int ended) {
    static uint32_t seen[64], s_at = 0xFFFFFFFFu;
    static unsigned next_seen;
    uint32_t end = offset + size, last = (end - 1) / BLOCK_SIZE, at = offset;
    int streaming = stream || size == load_chunk, i;
    uint8_t* d = dst;
    for (i = 0; i < 64 && seen[i] != offset + 1; i++) {
    }
    if (streaming || i < 64) {
        int ok;
        if (streaming && offset != s_at) {
            stream_start(offset, last, stream);
        }
        ok = naomi_rom_read(offset, dst, size);
        sync_last = 0;
        if (!ok) {
            return 0;
        }
        if (ended) {
            /* and one that goes on from it starts again; the read-ahead past it let go but within the table's files */
            if (!s_known) {
                unqueue(last + 1, s_end);
            }
            s_at = 0xFFFFFFFFu;
        } else if (streaming) {
            stream_ahead(last);
            s_at = end;
        }
        return 1;
    }
    seen[next_seen++ % 64] = offset + 1;
    while (at < end) {
        uint32_t b = at / BLOCK_SIZE, n = b + 1, to;
        int in = have(b);
        while (n <= last && have(n) == in) {
            n++;
        }
        to = n * BLOCK_SIZE < end ? n * BLOCK_SIZE : end;
        if (in) {
            if (!naomi_rom_read(at, d, to - at)) {
                return 0;
            }
        } else {
            u64 t0 = svcGetSystemTick();
            u32 got = 0;
            PROF_START(t);
            unqueue(b, n - 1);
            if (R_FAILED(FSFILE_Read(fh_main, &got, at, d, to - at)) || got != to - at) {
                naomi_rom_read(at, d, to - at);
            }
            PROF_STOP(PZ_ROM_MISS, t);
            PROF_COUNT(PZ_ROM_MISS, n - b);
            n_direct++;
            n_direct_kb += (to - at) >> 10;
            if (trace) {
                host_log("rom %u: %lX+%lX straight in, %.1f ms", host_frame(), (unsigned long)at,
                         (unsigned long)(to - at), ms_since(t0));
            }
        }
        d += to - at;
        at = to;
    }
    return 1;
}

int ctr_rom_dma(uint32_t offset, void* dst, uint32_t size) {
    static uint32_t prev_end = 0xFFFFFFFFu, prev_size;
    uint32_t end = offset + size;
    int stream = offset == prev_end, ended = stream && size < prev_size, done;
    prev_end = end;
    prev_size = size;
    if (!is_open || size == 0 || end < offset || (end - 1) / BLOCK_SIZE >= file_blocks) {
        return 0;
    }
    if (preload && give_preload(offset, dst, size)) {
        done = 1;
    } else if (direct) {
        in_dma = 1;
        done = dma_direct(offset, dst, size, stream, ended);
        in_dma = 0;
    } else {
        done = 0;
    }
    if (preload) {
        note_load(offset, size);
    }
    return done;
}

/* MVC2_ROM_DIRECT: a sound stream (s3l.c) set to start at at, after its head was read: read ahead from there as
   when a stream has gone on from one block into the next, up to its end, and past it to its start when it loops */
void ctr_rom_stream(uint32_t at, uint32_t size, int loop) {
    uint32_t b = at / BLOCK_SIZE;
    if (!is_open || !direct || size == 0 || b == 0 || b >= file_blocks) {
        return;
    }
    req_last = req_end = b - 1;
    req_stop = (at + size - 1) / BLOCK_SIZE;
    snd_first = b;
    snd_loop = loop != 0;
    rom_missed(b, b);
}

/* between frames */
void ctr_rom_poll(void) {
    PROF_START(t);
    if (!is_open) {
        return;
    }
    put_done();
    if (preload && host_frame() >= PRELOAD_FROM) {
        start_preload();
        start_unpack();
        if (file_tab != NULL && !attract_music) {
            uint32_t b = file_tab[2 * ATTRACT_MUSIC] / BLOCK_SIZE;
            want(b, b + 1);
            attract_music = 1;
        }
        if (file_tab != NULL && portraits_at != 0 && host_frame() >= portraits_at) {
            want(file_tab[2 * PORTRAIT_FIRST] / BLOCK_SIZE, file_last_block(PORTRAIT_LAST));
            portraits_at = 0;
        }
    }
    feed_wants();
    if (host_frame() / 600 != log_frame / 600) {
        log_frame = host_frame();
        if (n_sync_reads != 0 || n_served != 0 || n_waited != 0 || n_ahead != 0 || n_direct != 0) {
            host_log("rom: %lu blocks read on the game's side in %lu reads, %lu taken from the read-ahead, %lu waited "
                     "for, %lu read ahead", (unsigned long)n_sync, (unsigned long)n_sync_reads, (unsigned long)n_served,
                     (unsigned long)n_waited, (unsigned long)n_ahead);
            if (direct) {
                host_log("rom: %lu KB read straight into the game's memory in %lu reads", (unsigned long)n_direct_kb,
                         (unsigned long)n_direct);
            }
        }
        n_sync = n_sync_reads = n_served = n_waited = n_ahead = n_direct = n_direct_kb = 0;
    }
    PROF_STOP(PZ_ROM_POLL, t);
}

static void unpack_progress(uint32_t done, uint32_t total) {
    const uint32_t width = 30;
    uint32_t i, n = done * width / total;
    printf("\x1b[14;1H[");
    for (i = 0; i < width; i++) {
        putchar(i < n ? '#' : '-');
    }
    printf("] %3lu%%", (unsigned long)(done * 100 / total));
    gfxFlushBuffers();
    gspWaitForVBlank();
    ctr_render_alive();
}

const char* ctr_rom_error;

/* MVC2_ROM_BENCH=1: 2 MB read in pieces of 64 KB, 512 KB and 2 MB, each from its own part of the image */
static void bench(void) {
    static const uint32_t piece[] = { 0x10000, 0x80000, 0x200000, 0x10000 };
    uint8_t* buf = host_try_malloc(0x200000);
    u32 got;
    int i;
    if (buf == NULL || file_blocks < 0x500) {
        host_log("rom bench: no room");
        free(buf);
        return;
    }
    FSFILE_Read(fh_main, &got, 0, buf, BLOCK_SIZE);
    for (i = 0; i < 4; i++) {
        u64 at = (u64)(0x100 + i * 0x100) * BLOCK_SIZE, t0 = svcGetSystemTick();
        uint32_t done;
        for (done = 0; done < 0x200000; done += piece[i]) {
            FSFILE_Read(fh_main, &got, at + done, buf + done, piece[i]);
        }
        {
            double ms = (double)(svcGetSystemTick() - t0) * 1000.0 / SYSCLOCK_ARM11;
            host_log("rom bench: 2048 KB in %lu KB reads: %.1f ms, %.2f MB/s", (unsigned long)(piece[i] >> 10), ms,
                     ms > 0.0 ? 2.0 * 1000.0 / ms : 0.0);
        }
    }
    free(buf);
}

static int rom_open(const char* dir) {
    char path[1100];
    s32 prio = 0x30;
    u64 size = 0;
    u32 limit = 0;
    int i;
    if (naomi_rom_cache_bytes < CACHE_LEAST) {
        host_log("arcade mode: a ROM cache of %lu KB, less than the %u KB it needs", (unsigned long)(naomi_rom_cache_bytes >> 10),
                 CACHE_LEAST >> 10);
        ctr_rom_error = "Arcade mode needs more memory than a 3dsx has on an Old 3DS. Install the CIA (with FBI) and "
                        "start it from the HOME Menu, or play the Dreamcast version.";
        return 0;
    }
    snprintf(path, sizeof path, "%s%s/%s", strncmp(dir, "sdmc:", 5) != 0 ? CTR_HOME "/" : "", dir, naomi_rom_image_name());
    if (!open_raw(path, &fh_main)) {
        consoleClear();
        printf("Marvel vs. Capcom 2\n\nONE-TIME ROM UNPACK\n\nThe ROM set is unpacked into\n%s/%s\n"
               "(136 MB), so the game can read any\npart of it quickly.\n\nThis happens only once. Please don't\n"
               "turn off the 3DS or take out the\nSD card.\n", dir, naomi_rom_image_name());
        unpack_progress(0, 1);
        host_log("unpacking the ROM set into %s", path);
        if (!naomi_rom_make_image(dir, path, unpack_progress) || !open_raw(path, &fh_main)) {
            host_log("could not unpack the ROM set into %s", path);
            consoleClear();
            return 0;
        }
        printf("\n\nDone. The set's other files can be\ndeleted now.\n");
        consoleClear();
    }
    if (R_FAILED(FSFILE_GetSize(fh_main, &size)) || !open_raw(path, &fh_thread)) {
        FSFILE_Close(fh_main);
        return 0;
    }
    file_blocks = (uint32_t)(size / BLOCK_SIZE);
    if (getenv("MVC2_ROM_BENCH") != NULL) {
        bench();
    }
    trace = getenv("MVC2_ROM_TRACE") != NULL && atoi(getenv("MVC2_ROM_TRACE")) != 0;
    direct = getenv("MVC2_ROM_DIRECT") != NULL && atoi(getenv("MVC2_ROM_DIRECT")) != 0;
    preload = getenv("MVC2_ROM_PRELOAD") != NULL && atoi(getenv("MVC2_ROM_PRELOAD")) != 0;
    for (i = 0; i < PRELOADS; i++) {
        pre[i].data = NULL;
        pre[i].state = P_WAIT;
    }
    pre_busy = -1;
    n_wants = n_picked = next_pick = attract_music = unpack_tried = 0;
    portraits_at = 0;
    s_end = s_file_last = 0;
    if ((direct || preload) && (file_tab = malloc(FILE_COUNT * 8)) != NULL) {
        u32 got = 0;
        if (R_FAILED(FSFILE_Read(fh_main, &got, FILE_TABLE, file_tab, FILE_COUNT * 8)) || got != FILE_COUNT * 8 ||
            file_tab[0] != 0x800000 || file_tab[1] != 0xE780 || file_tab[2 * (FILE_COUNT - 1)] != 0x0755BD40) {
            free(file_tab);
            file_tab = NULL;
        }
        host_log("ROM: the game's table of its files %s", file_tab != NULL ? "read" : "not found");
    }
    load_chunk = getenv("MVC2_LOAD_KB") != NULL ? (uint32_t)atoi(getenv("MVC2_LOAD_KB")) / 2 * 2048 : 0;
    if (getenv("MVC2_ROM_AHEAD") != NULL) {
        ahead = (uint32_t)atoi(getenv("MVC2_ROM_AHEAD"));
        ahead = ahead > BUFS ? BUFS : ahead;
    }
    roll = getenv("MVC2_ROM_ROLL") != NULL && atoi(getenv("MVC2_ROM_ROLL")) != 0;
    if (getenv("MVC2_ROM_PIECE") != NULL) {
        piece = (uint32_t)atoi(getenv("MVC2_ROM_PIECE")) / (BLOCK_SIZE >> 10);
        piece = piece < 1 ? 1 : piece > PIECE_MOST ? PIECE_MOST : piece;
    }
    buf_mem = malloc(BUFS * BLOCK_SIZE);
    if (buf_mem == NULL) {
        FSFILE_Close(fh_main);
        FSFILE_Close(fh_thread);
        free(file_tab);
        file_tab = NULL;
        return 0;
    }
    for (i = 0; i < BUFS; i++) {
        bufs[i].data = buf_mem + i * BLOCK_SIZE;
        bufs[i].state = B_FREE;
        bufs[i].held = 0;
    }
    LightLock_Init(&lock);
    LightEvent_Init(&wake, RESET_ONESHOT);
    quit = 0;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    game_prio = prio;
    thread = NULL;
    /* the system core, at 30% or more; MVC2_ROM_CORE=0: the game's core, above the game */
    if (getenv("MVC2_ROM_CORE") != NULL && atoi(getenv("MVC2_ROM_CORE")) == 0) {
        thread = threadCreate(reader_main, NULL, 16 * 1024, prio - 1, 0, false);
    } else if (R_SUCCEEDED(APT_GetAppCpuTimeLimit(&limit)) &&
               (limit >= 30 || R_SUCCEEDED(APT_SetAppCpuTimeLimit(30)))) {
        thread = threadCreate(reader_main, NULL, 16 * 1024, prio - 1, 1, false);
    }
    if (thread == NULL) {
        thread = threadCreate(reader_main, NULL, 16 * 1024, prio - 1, -2, false);
    }
    is_open = 1;
    host_log("ROM from %s, read ahead %s, up to %lu KB a read%s%s", path,
             thread != NULL ? "on another thread" : "not at all (no thread)", (unsigned long)(piece * (BLOCK_SIZE >> 10)),
             getenv("MVC2_ROM_PIECE") != NULL ? " (MVC2_ROM_PIECE)" : "",
             roll ? "; a stream's read ahead as it reads (MVC2_ROM_ROLL)" : "");
    if (ahead != AHEAD) {
        host_log("ROM: %lu blocks read past a stream's request (MVC2_ROM_AHEAD)", (unsigned long)ahead);
    }
    if (direct) {
        host_log("ROM: the game's reads from a new place straight into its memory, a stream's read ahead in pieces "
                 "growing to %lu KB up to its end (MVC2_ROM_DIRECT)", (unsigned long)(piece * (BLOCK_SIZE >> 10)));
    }
    if (preload) {
        host_log("ROM: the title's loads read ahead from frame %d, while %u KB of the heap stay free, and the select "
                 "screen's into the cache (MVC2_ROM_PRELOAD)", PRELOAD_FROM, PRELOAD_LEAVE >> 10);
    }
    return 1;
}

static void rom_close(void) {
    int i;
    if (!is_open) {
        return;
    }
    if (thread != NULL) {
        quit = 1;
        LightEvent_Signal(&wake);
        threadJoin(thread, U64_MAX);
        threadFree(thread);
        thread = NULL;
    }
    unpack_wait();
    free(unpacked);
    unpacked = NULL;
    for (i = 0; i < PRELOADS; i++) {
        free(pre[i].data);
        pre[i].data = NULL;
        pre[i].state = P_GONE;
    }
    FSFILE_Close(fh_main);
    FSFILE_Close(fh_thread);
    free(buf_mem);
    buf_mem = NULL;
    free(file_tab);
    file_tab = NULL;
    is_open = 0;
}

static const NAOMI_ROM_READER reader = { rom_open, rom_read, rom_missed, rom_close };

void ctr_rom_start(void) {
    naomi_rom_set_reader(&reader);
}
