/*
 * ctr_stream.c  One file read ahead on a thread of its own, in pieces asked for in order: the Dreamcast's music
 * from the SD card (MVC2_ADX_AHEAD). Only the thread reads the file; the game's thread opens and closes it, with
 * the thread idle, and copies out of the pieces read. And one disc read at a time done on another thread
 * (ctr_read_*, MVC2_GD_ASYNC).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <3ds.h>

#include "naomi_host.h"
#include "prof.h"

#define PIECES 3

enum { P_FREE, P_QUEUED, P_READING, P_DONE };

static struct {
    uint32_t off, len, age;
    volatile uint32_t state;
} piece[PIECES];

static uint8_t* mem;
static uint32_t piece_max;
static FILE* file;
static LightLock lock;
static LightEvent wake;
static Thread thread;
static uint32_t queue_clock;
static volatile uint64_t read_ticks, read_most;     /* the thread's reads, since ctr_stream_times */
static volatile uint32_t reads;

/* the readers' core: the game's (0), above the game, where a read's return isn't kept waiting behind the sound and the
   lists; MVC2_READER_CORE=1 puts them on core 1 */
static int reader_core(void) {
    static int core = -1;
    if (core < 0) {
        const char* e = getenv("MVC2_READER_CORE");
        core = e != NULL && atoi(e) == 1 ? 1 : 0;
    }
    return core;
}

static void reader_main(void* arg) {
    (void)arg;
    for (;;) {
        int i, at = -1;
        LightLock_Lock(&lock);
        for (i = 0; i < PIECES; i++) {
            if (piece[i].state == P_QUEUED && (at < 0 || piece[i].age < piece[at].age)) {
                at = i;
            }
        }
        if (at >= 0) {
            piece[at].state = P_READING;
        }
        LightLock_Unlock(&lock);
        if (at < 0) {
            LightEvent_Wait(&wake);
            continue;
        }
        {
            uint8_t* to = mem + (size_t)at * piece_max;
            size_t got = 0;
            uint64_t t0 = svcGetSystemTick(), dt;
            if (fseek(file, (long)piece[at].off, SEEK_SET) == 0) {
                got = fread(to, 1, piece[at].len, file);
            }
            memset(to + got, 0, piece[at].len - got);
            dt = svcGetSystemTick() - t0;
            LightLock_Lock(&lock);
            read_ticks += dt;
            read_most = dt > read_most ? dt : read_most;
            reads++;
            piece[at].state = P_DONE;
            LightLock_Unlock(&lock);
        }
    }
}

/* the thread's reads since the last call: how many, and their mean and longest in ms */
uint32_t ctr_stream_times(float* mean, float* most) {
    uint32_t n;
    LightLock_Lock(&lock);
    n = reads;
    *mean = n ? (float)read_ticks * 1000.0f / SYSCLOCK_ARM11 / (float)n : 0.0f;
    *most = (float)read_most * 1000.0f / SYSCLOCK_ARM11;
    read_ticks = read_most = 0;
    reads = 0;
    LightLock_Unlock(&lock);
    return n;
}

/* the file's pieces read from now on; 0 when it can't be (and the caller reads it itself) */
int ctr_stream_open(const char* path, uint32_t most) {
    s32 prio = 0x30;
    int i;
    ctr_stream_close();
    if (mem == NULL) {
        if ((mem = malloc((size_t)PIECES * most)) == NULL) {
            return 0;
        }
        piece_max = most;
        LightLock_Init(&lock);
        LightEvent_Init(&wake, RESET_ONESHOT);
        svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
        thread = threadCreate(reader_main, NULL, 16 * 1024, prio - 1, reader_core(), true);
        if (thread == NULL) {
            thread = threadCreate(reader_main, NULL, 16 * 1024, prio - 1, -2, true);
        }
        if (thread == NULL) {
            free(mem);
            mem = NULL;
            return 0;
        }
    }
    {
        PROF_START(t0);
        file = most <= piece_max ? fopen(path, "rb") : NULL;
        PROF_STOP(PZ_GD_OPEN, t0);
    }
    if (file == NULL) {
        return 0;
    }
    setvbuf(file, NULL, _IONBF, 0);
    for (i = 0; i < PIECES; i++) {
        piece[i].state = P_FREE;
    }
    return 1;
}

/* the thread idle and the pieces all free */
void ctr_stream_drop(void) {
    int i, busy;
    if (file == NULL) {
        return;
    }
    LightLock_Lock(&lock);
    for (i = 0; i < PIECES; i++) {
        if (piece[i].state == P_QUEUED) {
            piece[i].state = P_FREE;
        }
    }
    LightLock_Unlock(&lock);
    do {
        busy = 0;
        for (i = 0; i < PIECES; i++) {
            busy |= piece[i].state == P_READING;
        }
        if (busy) {
            svcSleepThread(100000);
        }
    } while (busy);
    for (i = 0; i < PIECES; i++) {
        piece[i].state = P_FREE;
    }
}

/* and the file closed */
void ctr_stream_close(void) {
    if (file != NULL) {
        PROF_START(t0);
        ctr_stream_drop();
        fclose(file);
        file = NULL;
        PROF_STOP(PZ_GD_OPEN, t0);
    }
}

/* len bytes from off asked for; the piece, or -1 when all are taken */
int ctr_stream_queue(uint32_t off, uint32_t len) {
    int i;
    if (file == NULL || len > piece_max) {
        return -1;
    }
    LightLock_Lock(&lock);
    for (i = 0; i < PIECES && piece[i].state != P_FREE; i++) {
    }
    if (i < PIECES) {
        piece[i].off = off;
        piece[i].len = len;
        piece[i].age = ++queue_clock;
        piece[i].state = P_QUEUED;
    }
    LightLock_Unlock(&lock);
    if (i == PIECES) {
        return -1;
    }
    LightEvent_Signal(&wake);
    return i;
}

/* len bytes from off that the caller has read already, as a piece read; the piece, or -1 when all are taken */
int ctr_stream_put(uint32_t off, uint32_t len, const void* bytes) {
    int i;
    if (file == NULL || len > piece_max) {
        return -1;
    }
    for (i = 0; i < PIECES && piece[i].state != P_FREE; i++) {
    }
    if (i == PIECES) {
        return -1;
    }
    memcpy(mem + (size_t)i * piece_max, bytes, len);
    LightLock_Lock(&lock);
    piece[i].off = off;
    piece[i].len = len;
    piece[i].age = ++queue_clock;
    piece[i].state = P_DONE;
    LightLock_Unlock(&lock);
    return i;
}

/* the piece asked for that holds n bytes from off, or -1 */
int ctr_stream_find(uint32_t off, uint32_t n) {
    int i, at = -1;
    for (i = 0; i < PIECES; i++) {
        if (piece[i].state != P_FREE && off >= piece[i].off && off + n <= piece[i].off + piece[i].len &&
            (at < 0 || piece[i].age < piece[at].age)) {
            at = i;
        }
    }
    return at;
}

/* the piece's bytes, NULL while they are still being read */
const uint8_t* ctr_stream_take(int i, uint32_t* off) {
    if (piece[i].state != P_DONE) {
        return NULL;
    }
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    *off = piece[i].off;
    return mem + (size_t)i * piece_max;
}

void ctr_stream_free(int i) {
    LightLock_Lock(&lock);
    piece[i].state = P_FREE;
    LightLock_Unlock(&lock);
}

/* the disc reader: one read at a time, from a file open already, in steps the music's reads can come between; and
   when it has nothing asked of it, one whole file read ahead (MVC2_GD_AHEAD), a step at a time */
/* a step's bytes, MVC2_GD_STEP_KB (128): each costs the SD card a request, and a requested read waits for one of the
   read-ahead's at most */
static size_t job_step(void) {
    static size_t step;
    if (step == 0) {
        const char* e = getenv("MVC2_GD_STEP_KB");
        step = (e != NULL && atoi(e) >= 16 ? (size_t)atoi(e) : 128) << 10;
    }
    return step;
}

static struct {
    FILE* f;
    long off;
    uint8_t* to;
    size_t want, got;
    uint64_t ticks;
} job;
static volatile int job_busy;
static LightEvent job_wake;
static Thread job_thread;

enum { A_FREE, A_READING, A_DONE, A_STOP };

static struct {
    char path[1200];
    FILE* f;
    FILE* done_f;                   /* MVC2_GD_KEEP=1: once read, still open for the game's open of it */
    uint8_t* mem;
    size_t cap, size, got;
    uint64_t ticks;
    volatile int state;
} ahead;

static void job_read(void) {
    size_t got = 0;
    uint64_t t0 = svcGetSystemTick();
    if (fseek(job.f, job.off, SEEK_SET) == 0) {
        while (got < job.want) {
            size_t step = job.want - got < job_step() ? job.want - got : job_step(), n;
            n = fread(job.to + got, 1, step, job.f);
            got += n;
            if (n < step) {
                break;
            }
        }
    }
    memset(job.to + got, 0, job.want - got);
    job.got = got;
    job.ticks = svcGetSystemTick() - t0;
    __atomic_store_n(&job_busy, 0, __ATOMIC_RELEASE);
}

/* the file read ahead: opened, or a step of it read; done or let go of at its end, or when asked to stop */
static void ahead_step(void) {
    int from = A_READING;
    uint64_t t0 = svcGetSystemTick();
    int end, ok = 1;
    if (__atomic_load_n(&ahead.state, __ATOMIC_ACQUIRE) == A_STOP) {
        ok = 0;
    } else if (ahead.f == NULL) {
        long n = 0;
        ahead.got = 0;
        ahead.ticks = 0;
        ok = (ahead.f = fopen(ahead.path, "rb")) != NULL;
        if (ok) {
            setvbuf(ahead.f, NULL, _IONBF, 0);
            ok = fseek(ahead.f, 0, SEEK_END) == 0 && (n = ftell(ahead.f)) >= 0 && (size_t)n <= ahead.cap &&
                 fseek(ahead.f, 0, SEEK_SET) == 0;
            ahead.size = ok ? (size_t)n : 0;
        }
    } else {
        size_t step = ahead.size - ahead.got < job_step() ? ahead.size - ahead.got : job_step();
        size_t n = fread(ahead.mem + ahead.got, 1, step, ahead.f);
        ahead.got += n;
        ok = n == step;
    }
    ahead.ticks += svcGetSystemTick() - t0;
    end = !ok || ahead.got == ahead.size;
    if (end) {
        static int keep = -1;
        if (keep < 0) {
            keep = getenv("MVC2_GD_KEEP") != NULL && atoi(getenv("MVC2_GD_KEEP")) != 0;
        }
        ahead.done_f = ok && keep ? ahead.f : NULL;
        if (ahead.done_f == NULL && ahead.f != NULL) {
            fclose(ahead.f);
        }
        ahead.f = NULL;
        if (!ok || !__atomic_compare_exchange_n(&ahead.state, &from, A_DONE, 0, __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
            if (ahead.done_f != NULL) {
                fclose(ahead.done_f);
                ahead.done_f = NULL;
            }
            __atomic_store_n(&ahead.state, A_FREE, __ATOMIC_RELEASE);
        }
    }
}

/* files to close, put by the game's thread and closed by the reader's */
#define CLOSES 16
static FILE* closes[CLOSES];
static volatile uint32_t close_put, close_got;

/* small files opened and read whole ahead, after any read asked for and before the read-ahead's steps; each left
   open for the game's open of it */
#define SMALLS 8
enum { S_FREE, S_QUEUED, S_DONE, S_GONE };
static struct {
    char path[256];
    FILE* f;
    uint8_t* mem;
    size_t most, size;
    volatile int state;
} small[SMALLS];

/* the first one queued opened and read, or failed; 0 when none is queued */
static int small_step(void) {
    int i;
    for (i = 0; i < SMALLS; i++) {
        if (__atomic_load_n(&small[i].state, __ATOMIC_ACQUIRE) == S_QUEUED) {
            FILE* f = fopen(small[i].path, "rb");
            long n = -1;
            int ok = 0;
            if (f != NULL) {
                setvbuf(f, NULL, _IONBF, 0);
                if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) >= 0 && (size_t)n <= small[i].most &&
                    fseek(f, 0, SEEK_SET) == 0 && (small[i].mem = malloc(n > 0 ? (size_t)n : 1)) != NULL) {
                    ok = fread(small[i].mem, 1, (size_t)n, f) == (size_t)n;
                }
            }
            if (!ok) {
                if (f != NULL) {
                    fclose(f);
                }
                free(small[i].mem);
                small[i].mem = NULL;
                f = NULL;
            }
            small[i].f = f;
            small[i].size = ok ? (size_t)n : 0;
            __atomic_store_n(&small[i].state, ok ? S_DONE : S_GONE, __ATOMIC_RELEASE);
            return 1;
        }
    }
    return 0;
}

static void job_main(void* arg) {
    (void)arg;
    for (;;) {
        int a;
        uint32_t c;
        if (__atomic_load_n(&job_busy, __ATOMIC_ACQUIRE)) {
            job_read();
            continue;
        }
        if ((c = __atomic_load_n(&close_got, __ATOMIC_RELAXED)) != __atomic_load_n(&close_put, __ATOMIC_ACQUIRE)) {
            fclose(closes[c % CLOSES]);
            __atomic_store_n(&close_got, c + 1, __ATOMIC_RELEASE);
            continue;
        }
        if (small_step()) {
            continue;
        }
        a = __atomic_load_n(&ahead.state, __ATOMIC_ACQUIRE);
        if (a == A_READING || a == A_STOP) {
            ahead_step();
            continue;
        }
        LightEvent_Wait(&job_wake);
    }
}

static int job_make(void) {
    if (job_thread == NULL) {
        s32 prio = 0x30;
        LightEvent_Init(&job_wake, RESET_ONESHOT);
        svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
        job_thread = threadCreate(job_main, NULL, 16 * 1024, prio - 1, reader_core(), true);
        if (job_thread == NULL) {
            job_thread = threadCreate(job_main, NULL, 16 * 1024, prio - 1, -2, true);
        }
    }
    return job_thread != NULL;
}

/* want bytes from off in the open file f read to to on the reader, what is past the file's end cleared; 0 when it
   can't be (the caller reads them itself). Only the reader uses f until the read ends */
int ctr_read_start(FILE* f, long off, void* to, size_t want) {
    if (!job_make() || __atomic_load_n(&job_busy, __ATOMIC_ACQUIRE)) {
        return 0;
    }
    job.f = f;
    job.off = off;
    job.to = to;
    job.want = want;
    __atomic_store_n(&job_busy, 1, __ATOMIC_RELEASE);
    LightEvent_Signal(&job_wake);
    return 1;
}

int ctr_read_busy(void) {
    return __atomic_load_n(&job_busy, __ATOMIC_ACQUIRE);
}

/* the read finished, waited for if need be: the bytes it found, and its time in ms */
size_t ctr_read_end(float* ms) {
    while (ctr_read_busy()) {
        svcSleepThread(100000);
    }
    *ms = (float)job.ticks * 1000.0f / SYSCLOCK_ARM11;
    return job.got;
}

/* f closed on the reader, between its reads; here when it can't be */
void ctr_close_later(FILE* f) {
    uint32_t p = close_put;
    if (!job_make() || p - __atomic_load_n(&close_got, __ATOMIC_ACQUIRE) == CLOSES) {
        fclose(f);
        return;
    }
    closes[p % CLOSES] = f;
    __atomic_store_n(&close_put, p + 1, __ATOMIC_RELEASE);
    LightEvent_Signal(&job_wake);
}

/* the whole file at path, up to most bytes, opened and read on the reader; 0 when it can't be */
int ctr_small_start(const char* path, size_t most) {
    int i, at = -1;
    if (!job_make() || strlen(path) >= sizeof small[0].path) {
        return 0;
    }
    for (i = 0; i < SMALLS; i++) {
        int s = __atomic_load_n(&small[i].state, __ATOMIC_ACQUIRE);
        if (s != S_FREE && strcmp(small[i].path, path) == 0) {
            return 1;
        }
        if (s == S_FREE && at < 0) {
            at = i;
        }
    }
    if (at < 0) {
        return 0;
    }
    strcpy(small[at].path, path);
    small[at].most = most;
    small[at].f = NULL;
    small[at].mem = NULL;
    __atomic_store_n(&small[at].state, S_QUEUED, __ATOMIC_RELEASE);
    LightEvent_Signal(&job_wake);
    return 1;
}

static int small_find(const char* path) {
    int i;
    for (i = 0; i < SMALLS; i++) {
        int s = __atomic_load_n(&small[i].state, __ATOMIC_ACQUIRE);
        if (s != S_FREE && strcmp(small[i].path, path) == 0) {
            while (__atomic_load_n(&small[i].state, __ATOMIC_ACQUIRE) == S_QUEUED) {
                svcSleepThread(100000);
            }
            return i;
        }
    }
    return -1;
}

/* the file at path as the reader opened it, its size in size, if it was read ahead; the caller closes it */
FILE* ctr_small_open(const char* path, long* size) {
    int i = small_find(path);
    FILE* f;
    if (i < 0 || __atomic_load_n(&small[i].state, __ATOMIC_ACQUIRE) != S_DONE || small[i].f == NULL) {
        return NULL;
    }
    f = small[i].f;
    small[i].f = NULL;
    *size = (long)small[i].size;
    return f;
}

/* want bytes from off of the file read ahead, what is past its end cleared, and the file let go of; 0 when it
   wasn't read ahead */
int ctr_small_take(const char* path, size_t off, void* to, size_t want) {
    int i = small_find(path);
    size_t n;
    if (i < 0) {
        return 0;
    }
    if (__atomic_load_n(&small[i].state, __ATOMIC_ACQUIRE) != S_DONE) {
        __atomic_store_n(&small[i].state, S_FREE, __ATOMIC_RELEASE);
        return 0;
    }
    n = off < small[i].size ? (small[i].size - off < want ? small[i].size - off : want) : 0;
    memcpy(to, small[i].mem + off, n);
    memset((uint8_t*)to + n, 0, want - n);
    free(small[i].mem);
    small[i].mem = NULL;
    if (small[i].f != NULL) {
        ctr_close_later(small[i].f);
        small[i].f = NULL;
    }
    __atomic_store_n(&small[i].state, S_FREE, __ATOMIC_RELEASE);
    return 1;
}

/* all of them let go of */
void ctr_small_drop(void) {
    int i;
    for (i = 0; i < SMALLS; i++) {
        if (__atomic_load_n(&small[i].state, __ATOMIC_ACQUIRE) != S_FREE) {
            small_find(small[i].path);
            if (small[i].f != NULL) {
                fclose(small[i].f);
                small[i].f = NULL;
            }
            free(small[i].mem);
            small[i].mem = NULL;
            __atomic_store_n(&small[i].state, S_FREE, __ATOMIC_RELEASE);
        }
    }
}

/* the file read ahead let go of, its memory too when all */
void ctr_ahead_drop(int all) {
    int from = A_READING;
    if (!__atomic_compare_exchange_n(&ahead.state, &from, A_STOP, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) &&
        from == A_DONE) {
        if (ahead.done_f != NULL) {
            ctr_close_later(ahead.done_f);
            ahead.done_f = NULL;
        }
        __atomic_store_n(&ahead.state, A_FREE, __ATOMIC_RELEASE);
    }
    while (__atomic_load_n(&ahead.state, __ATOMIC_ACQUIRE) != A_FREE) {
        svcSleepThread(100000);
    }
    if (all) {
        free(ahead.mem);
        ahead.mem = NULL;
        ahead.cap = 0;
    }
}

/* the whole file at path, up to most bytes, read ahead when the reader has nothing else to do; 0 when it can't be */
int ctr_ahead_start(const char* path, size_t most) {
    ctr_ahead_drop(0);
    if (!job_make() || strlen(path) >= sizeof ahead.path) {
        return 0;
    }
    if (ahead.cap < most) {
        free(ahead.mem);
        ahead.cap = (ahead.mem = malloc(most)) != NULL ? most : 0;
        if (ahead.mem == NULL) {
            return 0;
        }
    }
    strcpy(ahead.path, path);
    __atomic_store_n(&ahead.state, A_READING, __ATOMIC_RELEASE);
    LightEvent_Signal(&job_wake);
    return 1;
}

/* when the file at path is the one read ahead: 1 while it is still being read, 2 once it is read; else 0 */
int ctr_ahead_has(const char* path) {
    int a = __atomic_load_n(&ahead.state, __ATOMIC_ACQUIRE);
    return (a == A_READING || a == A_DONE) && strcmp(ahead.path, path) == 0 ? (a == A_DONE) + 1 : 0;
}

/* the file at path as the reader opened it, its size in size, once it is read ahead; the caller closes it */
FILE* ctr_ahead_file(const char* path, long* size) {
    FILE* f;
    if (ctr_ahead_has(path) != 2 || ahead.done_f == NULL) {
        return NULL;
    }
    f = ahead.done_f;
    ahead.done_f = NULL;
    *size = (long)ahead.size;
    return f;
}

/* no file being read ahead or waiting to be taken */
int ctr_ahead_idle(void) {
    return __atomic_load_n(&ahead.state, __ATOMIC_ACQUIRE) == A_FREE;
}

/* want bytes from off of the file read ahead, waited for if need be, what is past its end cleared, and the file let
   go of; 0 when it was not read whole after all (the caller reads them itself). ms: how long it was read for */
int ctr_ahead_take(size_t off, void* to, size_t want, float* ms) {
    size_t n;
    while (__atomic_load_n(&ahead.state, __ATOMIC_ACQUIRE) == A_READING) {
        svcSleepThread(100000);
    }
    if (__atomic_load_n(&ahead.state, __ATOMIC_ACQUIRE) != A_DONE) {
        return 0;
    }
    n = off < ahead.size ? (ahead.size - off < want ? ahead.size - off : want) : 0;
    memcpy(to, ahead.mem + off, n);
    memset((uint8_t*)to + n, 0, want - n);
    *ms = (float)ahead.ticks * 1000.0f / SYSCLOCK_ARM11;
    if (ahead.done_f != NULL) {
        ctr_close_later(ahead.done_f);
        ahead.done_f = NULL;
    }
    __atomic_store_n(&ahead.state, A_FREE, __ATOMIC_RELEASE);
    return 1;
}
