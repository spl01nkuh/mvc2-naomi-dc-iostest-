/* early_load.h  Memory read ahead of its use: PLD does nothing on the Old 3DS, and the in-order ARM11 goes on past a
   load that misses until its register is needed */

#ifndef EARLY_LOAD_H
#define EARLY_LOAD_H

#include <stdint.h>

/* each 32-byte line of the bytes at p read once, the words unused */
static inline void early_lines(const void* p, uint32_t bytes) {
    const volatile uint32_t* w = (const volatile uint32_t*)((uintptr_t)p & ~(uintptr_t)31);
    const uint8_t* end = (const uint8_t*)p + bytes;
    for (; (const uint8_t*)w < end; w += 8) {
        (void)*w;
    }
}

/* four words read back to back, each into a register of its own, and only then used: a second load into a register
   that still waits for the first one waits too, these overlap */
static inline void early_words4(const void* p0, const void* p1, const void* p2, const void* p3) {
    uint32_t a = *(const volatile uint32_t*)p0, b = *(const volatile uint32_t*)p1;
    uint32_t c = *(const volatile uint32_t*)p2, d = *(const volatile uint32_t*)p3;
    __asm__ volatile("" : : "r"(a), "r"(b), "r"(c), "r"(d));
}

/* early_lines' lines four at a time with early_words4, the last line read again to fill a four */
static inline void early_lines4(const void* p, uint32_t bytes) {
    uintptr_t at = (uintptr_t)p & ~(uintptr_t)31, last = ((uintptr_t)p + bytes - 1) & ~(uintptr_t)31;
    for (; bytes != 0 && at <= last; at += 128) {
        early_words4((const void*)at, (const void*)(at + 32 < last ? at + 32 : last),
                     (const void*)(at + 64 < last ? at + 64 : last), (const void*)(at + 96 < last ? at + 96 : last));
    }
}

#endif
