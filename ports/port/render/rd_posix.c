/* pthread conditions work on iOS, where unnamed POSIX semaphores do not. */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
#include "rd_posix.h"

#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#include <pthread/qos.h>
#endif
#endif

struct RD_SEM {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    unsigned count;
};
struct RD_THREAD {
    pthread_t thread;
    int (*entry)(void*);
    void* context;
};

RD_SEM* rd_sem_create(unsigned count) {
    RD_SEM* sem = malloc(sizeof(*sem));
    if (!sem) return NULL;
    if (pthread_mutex_init(&sem->lock, NULL)) {
        free(sem);
        return NULL;
    }
    if (pthread_cond_init(&sem->changed, NULL)) {
        pthread_mutex_destroy(&sem->lock);
        free(sem);
        return NULL;
    }
    sem->count = count;
    return sem;
}
void rd_sem_destroy(RD_SEM* sem) {
    if (!sem) return;
    pthread_cond_destroy(&sem->changed);
    pthread_mutex_destroy(&sem->lock);
    free(sem);
}
void rd_sem_wait(RD_SEM* sem) {
    if (pthread_mutex_lock(&sem->lock)) abort();
    while (!sem->count) {
        if (pthread_cond_wait(&sem->changed, &sem->lock)) abort();
    }
    --sem->count;
    if (pthread_mutex_unlock(&sem->lock)) abort();
}
void rd_sem_post(RD_SEM* sem) {
    if (pthread_mutex_lock(&sem->lock)) abort();
    if (sem->count == UINT_MAX) abort();
    ++sem->count;
    if (pthread_cond_signal(&sem->changed)) abort();
    if (pthread_mutex_unlock(&sem->lock)) abort();
}
static void* thread_main(void* context) {
    RD_THREAD* thread = context;
    (void)thread->entry(thread->context);
    return NULL;
}
RD_THREAD* rd_thread_create(int (*entry)(void*), const char* name, void* context) {
    (void)name;
    if (!entry) return NULL;
    RD_THREAD* thread = malloc(sizeof(*thread));
    if (!thread) return NULL;
    thread->entry = entry;
    thread->context = context;
    int status;
#if defined(__APPLE__) && TARGET_OS_IPHONE
    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes) == 0) {
        /* Each render phase blocks the interactive gameplay owner until
         * its workers finish. Give them the same scheduling class. */
        (void)pthread_attr_set_qos_class_np(&attributes, QOS_CLASS_USER_INTERACTIVE, 0);
        status = pthread_create(&thread->thread, &attributes, thread_main, thread);
        pthread_attr_destroy(&attributes);
    } else
#endif
        status = pthread_create(&thread->thread, NULL, thread_main, thread);
    if (status) {
        free(thread);
        return NULL;
    }
    return thread;
}
void rd_thread_join(RD_THREAD* thread) {
    if (!thread) return;
    if (pthread_join(thread->thread, NULL)) abort();
    free(thread);
}
int rd_cpu_count(void) {
    long count = sysconf(_SC_NPROCESSORS_ONLN);
    return count < 1 ? 1 : count > INT_MAX ? INT_MAX : (int)count;
}
uint64_t rd_counter(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) abort();
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}
