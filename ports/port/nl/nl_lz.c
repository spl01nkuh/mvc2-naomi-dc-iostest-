/*
 * nl_lz.c  The game's sprite cells, unpacked by lz_unpack every frame, kept as unpacked
 *
 * MVC2_LZ_CACHE=1: a cell unpacked before is copied back, found by its source's address and size and checked by
 * its packed bytes' hash. =2: the game unpacks it as well, and the two are compared.
 */

#include <stdlib.h>
#include <string.h>

#include "naomi_host.h"
#include "nl_port.h"

#define LZ_MAX 4096             /* larger unpacks aren't kept */
#define LZ_OVER 16              /* what a copy can write past the end */
#define LZ_ARENA (256 * 1024)
#define LZ_SETS 256
#define LZ_WAYS 4

typedef struct {
    const uint8_t* src;
    uint64_t hash;              /* of the packed bytes */
    uint32_t size;              /* the unpack's size, as asked */
    uint16_t packed, written;   /* the bytes read, and written from dst on */
    uint32_t at, lap;           /* in the arena */
    uint32_t used;
} LZ_CELL;

static int mode = -1, failed;
static LZ_CELL (*cells)[LZ_WAYS];
static uint8_t* arena;
static uint32_t arena_at, arena_lap = 2, lz_clock;
static uint8_t check[LZ_MAX + LZ_OVER], guard[LZ_OVER];
static uint32_t check_n, mismatches, hits, misses;
static unsigned report_frame;

/* the game's unpack into out, which has LZ_OVER bytes past size; 0 if it reads before out */
static int unpack(const uint8_t* src, uint32_t size, uint8_t* out, uint32_t* packed, uint32_t* written) {
    const uint8_t* s = src;
    uint8_t *d = out, *end = out + size;
    int flag = 0, bit = 0;
    do {
        if (!bit) {
            flag = *s++;
            bit = 0x80;
        }
        if (flag & bit) {
            uint32_t c = *s++, n = (c & 15) + 2, back = (c >> 4) + 1;
            const uint8_t* p = d - back;
            if ((uint32_t)(d - out) < back) {
                return 0;
            }
            while (n--) {
                *d++ = *p++;
            }
        } else {
            *d++ = *s++;
        }
        bit >>= 1;
    } while (d < end);
    *packed = (uint32_t)(s - src);
    *written = (uint32_t)(d - out);
    return 1;
}

static int intact(const LZ_CELL* e) {
    return e->lap == arena_lap || (e->lap + 1 == arena_lap && e->at >= arena_at);
}

/* where n bytes go in the arena, over the oldest; arena_at is moved past what is kept of them */
static uint8_t* arena_take(uint32_t n, uint32_t* at) {
    if (arena_at + n > LZ_ARENA) {
        arena_at = 0;
        arena_lap++;
    }
    *at = arena_at;
    return arena + arena_at;
}

/* the cell's unpacked bytes, kept or made, into out; 0 if it isn't one kept */
static int cached(const uint8_t* src, uint32_t size, uint8_t* out, uint32_t* written) {
    LZ_CELL *set = cells[((uint32_t)(uintptr_t)src * 2654435761u) >> 24], *e = NULL;
    uint32_t packed, n, at, i;
    uint8_t* b;
    for (i = 0; i < LZ_WAYS; i++) {
        LZ_CELL* c = &set[i];
        if (c->src == src && c->size == size && intact(c)) {
            if (nl_hash64(src, c->packed) == c->hash) {
                c->used = ++lz_clock;
                memcpy(out, arena + c->at, c->written);
                *written = c->written;
                hits++;
                return 1;
            }
            e = c;
            break;
        }
        if (e == NULL || !intact(c) || (intact(e) && c->used < e->used)) {
            e = c;
        }
    }
    b = arena_take(size + LZ_OVER, &at);
    if (!unpack(src, size, b, &packed, &n)) {
        arena_at = at + size + LZ_OVER;
        return 0;
    }
    arena_at = at + ((n + 7) & ~7u);
    e->src = src;
    e->size = size;
    e->packed = (uint16_t)packed;
    e->written = (uint16_t)n;
    e->hash = nl_hash64(src, packed);
    e->at = at;
    e->lap = arena_lap;
    e->used = ++lz_clock;
    memcpy(out, b, n);
    *written = n;
    misses++;
    return 1;
}

static void report(void) {
    unsigned f = host_frame();
    if (f / 600 != report_frame / 600) {
        report_frame = f;
        if (mode == 2) {
            host_log("lz cache: %u hits, %u misses, %u mismatches", (unsigned)hits, (unsigned)misses, (unsigned)mismatches);
        } else {
            host_log("lz cache: %u hits, %u misses", (unsigned)hits, (unsigned)misses);
        }
    }
}

/* 1: dst unpacked; 2: for the game to unpack, then nl_lz_check; 0: for the game */
int nl_lz_unpack(const void* src, int size, void* dst) {
    uint32_t n;
    if (mode < 0) {
        const char* e = getenv("MVC2_LZ_CACHE");
        mode = e == NULL ? 0 : atoi(e);
    }
    if (mode <= 0 || failed || size <= 0 || size > LZ_MAX) {
        return 0;
    }
    if (cells == NULL) {
        if ((cells = host_try_calloc(LZ_SETS, sizeof *cells)) == NULL || (arena = host_try_malloc(LZ_ARENA)) == NULL) {
            free(cells);
            cells = NULL;
            failed = 1;
            host_log("lz cache: no room, the game unpacks");
            return 0;
        }
    }
    report();
    if (mode == 1) {
        return cached(src, (uint32_t)size, dst, &n);
    }
    if (!cached(src, (uint32_t)size, check, &check_n)) {
        return 0;
    }
    memcpy(guard, (const uint8_t*)dst + check_n, LZ_OVER);
    return 2;
}

/* after the game's own unpack: the bytes it wrote the same, and none past them */
void nl_lz_check(const void* dst) {
    const uint8_t* d = dst;
    if (memcmp(d, check, check_n) != 0 || memcmp(d + check_n, guard, LZ_OVER) != 0) {
        if (mismatches++ < 8) {
            host_log("lz cache: mismatch at %p, %u bytes", dst, (unsigned)check_n);
        }
    }
}
