/*
 * ctr_alloc.c  An allocation that fails stops the program, saying what it wanted, rather than leave a NULL to
 * crash on later; what can do without the memory asks with host_try_malloc
 */

#include <malloc.h>
#include <stdint.h>
#include <stdio.h>

#include <3ds.h>

#include "naomi_host.h"
#include "ctr_front.h"

void* __real_malloc(size_t bytes);
void* __real_calloc(size_t n, size_t size);
void* __real_realloc(void* p, size_t bytes);

extern u32 __ctru_heap_size;
const char* ctr_alloc_error;

static void out_of_memory(const char* what, size_t bytes, const void* caller) {
    static char text[300];
    static int stopping;
    struct mallinfo mi;
    if (__atomic_exchange_n(&stopping, 1, __ATOMIC_SEQ_CST)) {
        if (threadGetCurrent() == NULL) {
            return;                                     /* while it stops: the caller has its NULL */
        }
        for (;;) {
            svcSleepThread(1000000000LL);
        }
    }
    mi = mallinfo();
    snprintf(text, sizeof text,
             "Out of memory: %s of %lu bytes failed (called from %08lX). The heap has %lu KB in use of %lu KB, "
             "%lu KB of it never taken; the linear heap has %lu KB free.",
             what, (unsigned long)bytes, (unsigned long)(uintptr_t)caller, (unsigned long)mi.uordblks >> 10,
             (unsigned long)__ctru_heap_size >> 10, (unsigned long)(__ctru_heap_size - mi.arena) >> 10,
             (unsigned long)linearSpaceFree() >> 10);
    if (threadGetCurrent() != NULL) {
        host_log("%s", text);
        ctr_alloc_error = text;                         /* the main thread stops after the frame */
        for (;;) {
            svcSleepThread(1000000000LL);
        }
    }
    ctr_stop(text);
}

void* __wrap_malloc(size_t bytes) {
    void* p = __real_malloc(bytes);
    if (p == NULL && bytes != 0) {
        out_of_memory("malloc", bytes, __builtin_return_address(0));
    }
    return p;
}

void* __wrap_calloc(size_t n, size_t size) {
    void* p = __real_calloc(n, size);
    if (p == NULL && n != 0 && size != 0) {
        out_of_memory("calloc", n * size, __builtin_return_address(0));
    }
    return p;
}

void* __wrap_realloc(void* old, size_t bytes) {
    void* p = __real_realloc(old, bytes);
    if (p == NULL && bytes != 0) {
        out_of_memory("realloc", bytes, __builtin_return_address(0));
    }
    return p;
}

void* host_try_malloc(size_t bytes) {
    return __real_malloc(bytes);
}

void* host_try_calloc(size_t n, size_t size) {
    return __real_calloc(n, size);
}

void host_out_of_memory(const char* what, size_t bytes) {
    out_of_memory(what, bytes, __builtin_return_address(0));
}
