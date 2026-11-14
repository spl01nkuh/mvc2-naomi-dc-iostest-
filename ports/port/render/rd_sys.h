/*
 * rd_sys.h  The few SDL calls the back ends make (threads, semaphores, the clock), for SDL2 and,
 * with RD_SDL3 defined, SDL3
 */

#ifndef RD_SYS_H
#define RD_SYS_H

#if defined(RD_NATIVE_SYSTEM)
#include "rd_posix.h"
#elif defined(__3DS__)
/* the 3DS draws with its GPU (port/ctr); rd_soft runs there on the one thread, for checks (CTR_SOFT) */
#include <time.h>
typedef int RD_SEM;
typedef void RD_THREAD;
#define rd_sem_wait(s) ((void)(s))
#define rd_sem_post(s) ((void)(s))
#define rd_cpu_count() 1
#define rd_sem_create(n) ((RD_SEM*)NULL)
#define rd_sem_destroy(s) ((void)(s))
#define rd_thread_create(f, name, data) ((void)(f), (RD_THREAD*)NULL)
#define rd_thread_join(t) ((void)(t))
#define rd_counter() ((uint64_t)clock())
#define rd_frequency() ((uint64_t)CLOCKS_PER_SEC)
#elif defined(RD_SDL3)
#include <SDL3/SDL.h>
typedef SDL_Semaphore RD_SEM;
typedef SDL_Thread RD_THREAD;
#define rd_sem_wait SDL_WaitSemaphore
#define rd_sem_post SDL_SignalSemaphore
#define rd_cpu_count SDL_GetNumLogicalCPUCores
#define rd_sem_create SDL_CreateSemaphore
#define rd_sem_destroy SDL_DestroySemaphore
#define rd_thread_create SDL_CreateThread
#define rd_thread_join(t) SDL_WaitThread((t), NULL)
#define rd_counter SDL_GetPerformanceCounter
#define rd_frequency SDL_GetPerformanceFrequency
#else
#include <SDL.h>
typedef SDL_sem RD_SEM;
typedef SDL_Thread RD_THREAD;
#define rd_sem_wait SDL_SemWait
#define rd_sem_post SDL_SemPost
#define rd_cpu_count SDL_GetCPUCount
#define rd_sem_create SDL_CreateSemaphore
#define rd_sem_destroy SDL_DestroySemaphore
#define rd_thread_create SDL_CreateThread
#define rd_thread_join(t) SDL_WaitThread((t), NULL)
#define rd_counter SDL_GetPerformanceCounter
#define rd_frequency SDL_GetPerformanceFrequency
#endif

#endif
