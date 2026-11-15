/*
 * ctr_m1.c  naomi_m1.c's unpacking again, from ROM bytes already in memory, so that another thread can unpack a
 * file ahead of the game (ctr_rom.c)
 */

#include <stdint.h>
#include <string.h>

static const uint8_t* m1_mem;
static uint32_t m1_at, m1_len;
static int m1_short;

static int ctr_m1_fetch(uint32_t offset, void* dst, uint32_t size) {
    if (offset < m1_at || offset - m1_at > m1_len || size > m1_len - (offset - m1_at)) {
        m1_short = 1;
        return 0;
    }
    memcpy(dst, m1_mem + (offset - m1_at), size);
    return 1;
}

#define naomi_rom_read ctr_m1_fetch
#define naomi_m1_read ctr_m1_raw
#include "../naomi/naomi_m1.c"
#undef naomi_rom_read
#undef naomi_m1_read

/* the packed file at src unpacked into dst from the ROM's bytes from at in mem; 0 when they don't hold all it
   reads. One thread at a time. */
int ctr_m1_unpack(uint32_t src, uint8_t* dst, uint32_t size, const uint8_t* mem, uint32_t at, uint32_t len) {
    m1_mem = mem;
    m1_at = at;
    m1_len = len;
    m1_short = 0;
    ctr_m1_raw(src, dst, size);
    return !m1_short;
}
