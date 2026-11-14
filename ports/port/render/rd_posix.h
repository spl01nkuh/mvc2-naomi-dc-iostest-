/* Threading used by the native renderer on Darwin and POSIX hosts. */
#ifndef MVC2_RD_POSIX_H
#define MVC2_RD_POSIX_H
#include <stdint.h>

typedef struct RD_SEM RD_SEM;
typedef struct RD_THREAD RD_THREAD;
RD_SEM* rd_sem_create(unsigned count);
void rd_sem_destroy(RD_SEM* sem);
void rd_sem_wait(RD_SEM* sem);
void rd_sem_post(RD_SEM* sem);
RD_THREAD* rd_thread_create(int (*entry)(void*), const char* name, void* context);
void rd_thread_join(RD_THREAD* thread);
int rd_cpu_count(void);
uint64_t rd_counter(void);
#define rd_frequency() UINT64_C(1000000000)
#endif
