/* ARM64 stackful coroutines, using ordinary native instructions on one thread.
 * The game and its frontend retain thread affinity. No signals, ucontext,
 * executable writable pages, instruction emulation, or worker-thread handoff.
 * Stack guard pages and separate per-thread owners make misuse fail explicitly.
 */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#define _DARWIN_C_SOURCE 1

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "coro.h"
#include "coro_arm64_context.h"

#if !defined(__aarch64__) && !defined(__arm64__)
#error "coro_arm64.c requires ARM64"
#endif
#if defined(__arm64e__)
#error "coro_arm64.c does not implement the arm64e pointer-authentication ABI"
#endif
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

struct coro {
    void* sp;
    void* mapping;
    size_t mapping_size;
    unsigned char* stack;
    size_t stack_size;
    void (*fn)(void*);
    void* arg;
    pthread_t owner;
    int is_main;
};

static _Thread_local coro main_coro;
static _Thread_local coro* current;

extern void coro_arm64_swap(void** saved_sp, void* next_sp);

static void failure(const char* operation) {
    fprintf(stderr, "%s: %s\n", operation, strerror(errno));
    abort();
}

static void require_owner(const coro* c) {
    if (!pthread_equal(c->owner, pthread_self())) {
        errno = EINVAL;
        failure("ARM64 coroutine used from a different thread");
    }
}

coro* coro_self(void) {
    if (current == NULL) {
        main_coro.owner = pthread_self();
        main_coro.is_main = 1;
        current = &main_coro;
    }
    return current;
}

static _Noreturn void coro_start(void) {
    current->fn(current->arg);
    /* The existing coroutine contract has no return destination. */
    fputs("ARM64 coroutine entry returned instead of yielding\n", stderr);
    abort();
}

static void start_on_stack(coro* c, void (*fn)(void*), void* arg) {
    uint64_t* context;
    uint64_t fpcr, fpsr;
    c->fn = fn;
    c->arg = arg;
    /* mmap and the page-rounded length keep the restored SP 16-byte aligned. */
    context = (uint64_t*)(c->stack + c->stack_size - CORO_ARM64_CONTEXT_BYTES);
    memset(context, 0, CORO_ARM64_CONTEXT_BYTES);
    context[(CORO_ARM64_X29 + 8) / 8] = (uint64_t)(uintptr_t)coro_start;
    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    __asm__ volatile("mrs %0, fpsr" : "=r"(fpsr));
    context[CORO_ARM64_FPCR / 8] = fpcr;
    context[CORO_ARM64_FPSR / 8] = fpsr;
    c->sp = context;
}

coro* coro_create(void (*fn)(void*), void* arg, unsigned stack_size) {
    coro* c;
    size_t page, usable, total;
    long page_result;
    coro_self();
    if (fn == NULL) {
        errno = EINVAL;
        failure("coro_create entry");
    }
    page_result = sysconf(_SC_PAGESIZE);
    if (page_result <= 0) failure("coro_create page size");
    page = (size_t)page_result;
    usable = stack_size < 16384u ? 16384u : stack_size;
    if (usable > SIZE_MAX - (page - 1) || page > SIZE_MAX / 2) {
        errno = EOVERFLOW;
        failure("coro_create stack size");
    }
    usable = (usable + page - 1) / page * page;
    if (usable > SIZE_MAX - page * 2) {
        errno = EOVERFLOW;
        failure("coro_create stack size");
    }
    total = usable + page * 2;
    c = calloc(1, sizeof(*c));
    if (c == NULL) failure("coro_create allocation");
    c->mapping = mmap(NULL, total, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (c->mapping == MAP_FAILED) {
        int saved = errno;
        free(c);
        errno = saved;
        failure("coro_create stack mapping");
    }
    c->mapping_size = total;
    c->stack = (unsigned char*)c->mapping + page;
    c->stack_size = usable;
    if (mprotect(c->stack, usable, PROT_READ | PROT_WRITE) != 0) {
        int saved = errno;
        munmap(c->mapping, total);
        free(c);
        errno = saved;
        failure("coro_create stack protection");
    }
    c->owner = pthread_self();
    start_on_stack(c, fn, arg);
    return c;
}

void coro_restart(coro* c, void (*fn)(void*), void* arg) {
    coro_self();
    if (c == NULL) return;
    require_owner(c);
    if (c == current || c->is_main) return;
    if (fn == NULL) {
        errno = EINVAL;
        failure("coro_restart entry");
    }
    start_on_stack(c, fn, arg);
}

void coro_delete(coro* c) {
    coro_self();
    if (c == NULL) return;
    require_owner(c);
    if (c == current || c->is_main) return;
    if (munmap(c->mapping, c->mapping_size) != 0) failure("coro_delete stack unmap");
    free(c);
}

void coro_switch(coro* to) {
    coro* from = coro_self();
    if (to == NULL) {
        errno = EINVAL;
        failure("coro_switch destination");
    }
    require_owner(to);
    if (to == from) return;
    current = to;
    coro_arm64_swap(&from->sp, to->sp);
}
