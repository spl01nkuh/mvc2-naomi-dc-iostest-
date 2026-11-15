/*
 * ctr_memcpy.c  memcpy through the linker's --wrap: with MVC2_MEMCPY_FAST=1, 32 bytes at a time by ldm and stm
 * where both ends are word-aligned, apart and 16 bytes or more; newlib's, a word loop, for the rest
 */

#include <stddef.h>
#include <stdint.h>

#include "ctr_front.h"

void* __real_memcpy(void* to, const void* from, size_t n);

int ctr_memcpy_fast;

typedef struct {
    uint32_t w[8];
} BLOCK32;

typedef struct {
    uint32_t w[4];
} BLOCK16;

/* the loops kept as they are: gcc would make them a call to memcpy */
__attribute__((optimize("no-tree-loop-distribute-patterns"))) void* ctr_memcpy(void* to, const void* from, size_t n) {
    uint32_t* w = to;
    const uint32_t* r = from;
    if (n < 16 || (((uintptr_t)to | (uintptr_t)from) & 3) != 0 || (uintptr_t)to - (uintptr_t)from < n ||
        (uintptr_t)from - (uintptr_t)to < n) {
        return __real_memcpy(to, from, n);
    }
    for (; n >= 32; n -= 32, w += 8, r += 8) {
        *(BLOCK32*)w = *(const BLOCK32*)r;
    }
    if (n & 16) {
        *(BLOCK16*)w = *(const BLOCK16*)r;
        w += 4;
        r += 4;
    }
    if (n & 8) {
        w[0] = r[0];
        w[1] = r[1];
        w += 2;
        r += 2;
    }
    if (n & 4) {
        *w++ = *r++;
    }
    if (n & 3) {
        uint8_t* a = (uint8_t*)w;
        const uint8_t* b = (const uint8_t*)r;
        if (n & 2) {
            a[0] = b[0];
            a[1] = b[1];
            a += 2;
            b += 2;
        }
        if (n & 1) {
            a[0] = b[0];
        }
    }
    return to;
}

void* __wrap_memcpy(void* to, const void* from, size_t n) {
    return ctr_memcpy_fast ? ctr_memcpy(to, from, n) : __real_memcpy(to, from, n);
}
