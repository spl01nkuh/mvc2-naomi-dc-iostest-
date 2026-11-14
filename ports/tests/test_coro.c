/* Native ARM64 tests. No game images, instruction emulator, SDL or renderer.
 * Build with coro_arm64.c/.S and coro_registers_arm64.S, linking pthread.
 */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1

#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "coro.h"

extern int coro_test_registers(coro* destination);

typedef struct {
    coro* host;
    coro* task;
    pthread_t thread;
    uint64_t steps;
    unsigned entries;
    unsigned token;
} State;

static uint64_t read_fpcr(void) {
    uint64_t value;
    __asm__ volatile("mrs %0, fpcr" : "=r"(value));
    return value;
}

static uint64_t read_fpsr(void) {
    uint64_t value;
    __asm__ volatile("mrs %0, fpsr" : "=r"(value));
    return value;
}

static void write_fpcr(uint64_t value) {
    __asm__ volatile("msr fpcr, %0" : : "r"(value));
}

static void write_fpsr(uint64_t value) {
    __asm__ volatile("msr fpsr, %0" : : "r"(value));
}

static void check_alignment(void) {
    uintptr_t sp;
    __asm__ volatile("mov %0, sp" : "=r"(sp));
    assert((sp & 15u) == 0);
}

__attribute__((noinline)) static void nested_yield(State* state, unsigned depth) {
    volatile uint64_t locals[16];
    for (unsigned i = 0; i < 16; ++i) locals[i] = state->steps + depth * 100u + i;
    uint64_t step = state->steps;
    if (depth != 0) nested_yield(state, depth - 1);
    else {
        check_alignment();
        assert(coro_test_registers(state->host));
    }
    for (unsigned i = 0; i < 16; ++i) assert(locals[i] == step + depth * 100u + i);
}

static void running_task(void* argument) {
    State* state = argument;
    ++state->entries;
    assert(coro_self() == state->task);
    assert(pthread_equal(pthread_self(), state->thread));
    /* Deleting/restarting the current context has the legacy no-op behavior. */
    coro_delete(coro_self());
    coro_restart(coro_self(), running_task, argument);
    const uint64_t initial_control = read_fpcr();
    const uint64_t initial_status = read_fpsr();
    const uint64_t task_control = (initial_control & ~(UINT64_C(3) << 22)) | (UINT64_C(1) << 22);
    const uint64_t task_status = (initial_status & ~UINT64_C(0x9f)) | 1u;
    write_fpcr(task_control);
    write_fpsr(task_status);
    for (;;) {
        assert(read_fpcr() == task_control);
        assert(read_fpsr() == task_status);
        assert(state->token == 17);
        ++state->steps;
        nested_yield(state, 7);
    }
}

static void test_lifecycle_and_registers(void) {
    State state = {0};
    const uint64_t initial_control = read_fpcr();
    const uint64_t initial_status = read_fpsr();
    /* A different round mode/status must survive each switch back from task. */
    const uint64_t host_control = initial_control & ~(UINT64_C(3) << 22);
    const uint64_t host_status = (initial_status & ~UINT64_C(0x9f)) | 16u;
    write_fpcr(host_control);
    write_fpsr(host_status);
    state.host = coro_self();
    state.thread = pthread_self();
    state.token = 17;
    assert(state.host == coro_self());
    coro_switch(state.host);
    coro_restart(state.host, running_task, &state);
    coro_delete(state.host);
    coro_restart(NULL, running_task, &state);
    coro_delete(NULL);
    state.task = coro_create(running_task, &state, 131073);
    for (uint64_t i = 1; i <= 10000; ++i) {
        assert(coro_self() == state.host);
        assert(coro_test_registers(state.task));
        assert(state.steps == i);
        assert(state.entries == 1);
        assert(read_fpcr() == host_control);
        assert(read_fpsr() == host_status);
    }
    coro_restart(state.task, running_task, &state);
    coro_switch(state.task);
    assert(state.steps == 10001 && state.entries == 2);
    assert(read_fpcr() == host_control && read_fpsr() == host_status);
    coro_delete(state.task);
    /* A never-started context can be restarted and destroyed as well. */
    state.task = coro_create(running_task, &state, 1);
    coro_restart(state.task, running_task, &state);
    coro_switch(state.task);
    assert(state.entries == 3 && state.steps == 10002);
    coro_delete(state.task);
    write_fpcr(initial_control);
    write_fpsr(initial_status);
}

static void* independent_thread(void* argument) {
    (void)argument;
    test_lifecycle_and_registers();
    return NULL;
}

static void test_thread_local_families(void) {
    pthread_t threads[2];
    coro* host = coro_self();
    for (unsigned i = 0; i < 2; ++i) assert(pthread_create(&threads[i], NULL, independent_thread, NULL) == 0);
    for (unsigned i = 0; i < 2; ++i) assert(pthread_join(threads[i], NULL) == 0);
    assert(coro_self() == host);
}

static void return_entry(void* argument) { (void)argument; }

static void child_return(void) {
    coro* c = coro_create(return_entry, NULL, 16384);
    coro_switch(c);
}

static void* wrong_thread(void* argument) {
    coro_switch(argument);
    return NULL;
}

static void child_cross_thread(void) {
    pthread_t thread;
    coro* host = coro_self();
    assert(pthread_create(&thread, NULL, wrong_thread, host) == 0);
    assert(pthread_join(thread, NULL) == 0);
}

__attribute__((noinline)) static uint64_t overflow_stack(unsigned depth) {
    volatile unsigned char buffer[8192];
    for (size_t i = 0; i < sizeof(buffer); ++i) buffer[i] = (unsigned char)(depth + i);
    uint64_t result = depth == 0 ? 0 : overflow_stack(depth - 1);
    return result + buffer[depth & 8191u];
}

static void overflow_entry(void* argument) {
    volatile uint64_t result = overflow_stack(100);
    (void)argument;
    (void)result;
    abort();
}

static void child_overflow(void) {
    coro* c = coro_create(overflow_entry, NULL, 16384);
    coro_switch(c);
}

static void expect_signal(void (*fn)(void), int first, int second) {
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        fn();
        _exit(0);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFSIGNALED(status));
    assert(WTERMSIG(status) == first || WTERMSIG(status) == second);
}

int main(void) {
    struct rlimit no_core = {0, 0};
    assert(setrlimit(RLIMIT_CORE, &no_core) == 0);
    test_lifecycle_and_registers();
    test_thread_local_families();
    expect_signal(child_return, SIGABRT, SIGABRT);
    expect_signal(child_cross_thread, SIGABRT, SIGABRT);
    expect_signal(child_overflow, SIGSEGV, SIGBUS);
    puts("ARM64 native coroutines: stack/register/FPU preservation, restart, owner and guard-page checks passed.");
    return 0;
}
