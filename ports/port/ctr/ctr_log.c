/* ctr_log.c  The log on the 3DS: host_log's lines kept in memory and written to the SD card by a thread below the
   game's priority, so that a window's lines don't hold up its frame (MVC2_LOG_THREAD=0: each written as it comes) */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <3ds.h>

#include "ctr_front.h"

#define RING (64 << 10)
#define GATHER 20000000     /* ns the writer waits after it is woken, for the rest of a window's lines */

static char ring[RING];
static volatile uint32_t head, tail;    /* bytes put and written, running */
static char line[4096];
static LightLock lock, io;              /* the ring's end and the file */
static LightEvent wake, room;
static Thread writer;
static FILE* out;
static volatile int quit;

static void write_out(void) {
    uint32_t h, t;
    LightLock_Lock(&io);
    h = head, t = tail;
    while (t != h) {
        uint32_t at = t % RING, n = h - t < RING - at ? h - t : RING - at;
        fwrite(ring + at, 1, n, out);
        t += n;
    }
    fflush(out);
    __sync_synchronize();
    tail = t;
    LightLock_Unlock(&io);
    LightEvent_Signal(&room);
}

static void writer_main(void* arg) {
    (void)arg;
    while (!quit) {
        LightEvent_Wait(&wake);
        if (!quit) {
            svcSleepThread(GATHER);
        }
        write_out();
    }
}

/* the writer started on core 0 below the caller's priority, writing to f */
void ctr_log_start(FILE* f) {
    const char* e = getenv("MVC2_LOG_THREAD");
    s32 prio = 0x30;
    if (e != NULL && strcmp(e, "0") == 0) {
        fprintf(f, "log: each line written as it comes (MVC2_LOG_THREAD=0)\n");
        fflush(f);
        return;
    }
    out = f;
    LightLock_Init(&lock);
    LightLock_Init(&io);
    LightEvent_Init(&wake, RESET_ONESHOT);
    LightEvent_Init(&room, RESET_ONESHOT);
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    writer = threadCreate(writer_main, NULL, 16 << 10, prio + 2 <= 0x3F ? prio + 2 : 0x3F, 0, false);
    fprintf(f, writer != NULL ? "log: written by a thread of its own\n" : "log: no thread for it, each line written as it comes\n");
    fflush(f);
}

/* host_log's line queued; 0 when there is no writer, and ap untouched */
int ctr_log_line(const char* fmt, va_list ap) {
    uint32_t n, at, part;
    int len;
    if (writer == NULL) {
        return 0;
    }
    LightLock_Lock(&lock);
    len = vsnprintf(line, sizeof line - 1, fmt, ap);
    n = len < 0 ? 0 : len < (int)sizeof line - 1 ? (uint32_t)len : sizeof line - 2;
    line[n++] = '\n';
    while (head + n - tail > RING) {    /* full: the game waits for the writer */
        if (quit) {
            write_out();
        } else {
            LightEvent_Signal(&wake);
            LightEvent_Wait(&room);
        }
    }
    at = head % RING;
    part = n < RING - at ? n : RING - at;
    memcpy(ring + at, line, part);
    memcpy(ring, line + part, n - part);
    __sync_synchronize();
    head += n;
    LightLock_Unlock(&lock);
    LightEvent_Signal(&wake);
    return 1;
}

/* every line written and the writer stopped; later lines are written as they come */
void ctr_log_flush(void) {
    if (writer != NULL) {
        quit = 1;
        LightEvent_Signal(&wake);
        threadJoin(writer, U64_MAX);
        LightLock_Lock(&lock);
        threadFree(writer);
        writer = NULL;
        write_out();
        LightLock_Unlock(&lock);
    }
}
