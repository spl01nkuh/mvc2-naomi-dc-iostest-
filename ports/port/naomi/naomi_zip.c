/*
 * naomi_zip.c  The ROM set's files from a zip of it, as MAME keeps sets: the zip's directory, and inflate
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "naomi_host.h"

#define IN_SIZE 0x10000
#define FAST_BITS 10
#define MAX_BITS 15
#define MAX_PAD 8           /* zero bytes read past a member's data before it is called broken */
#define STEP 0x40000        /* the bytes inflated between calls of a read's step */

struct NAOMI_ZIP {
    FILE* f;
    uint8_t* dir;           /* the central directory */
    uint32_t dir_size;
    uint32_t entries;
};

typedef struct {
    FILE* f;
    uint32_t left;          /* compressed bytes still in the file */
    uint32_t pos, len;
    uint32_t bits, nbits;
    uint32_t pad;
    uint8_t buf[IN_SIZE];
} INPUT;

typedef struct {
    uint16_t fast[1 << FAST_BITS];  /* symbol << 4 | length for codes of up to FAST_BITS bits, else 0 */
    uint16_t count[MAX_BITS + 1];
    uint16_t symbol[288];
} HUFF;

static uint32_t get16(const uint8_t* p) {
    return p[0] | (uint32_t)p[1] << 8;
}

static uint32_t get32(const uint8_t* p) {
    return get16(p) | get16(p + 2) << 16;
}

static int same_name(const uint8_t* s, uint32_t n, const char* name) {
    uint32_t i, from = 0;
    for (i = 0; i < n; i++) {
        if (s[i] == '/' || s[i] == '\\') {
            from = i + 1;
        }
    }
    if (n - from != strlen(name)) {
        return 0;
    }
    for (i = from; i < n; i++) {
        int c = s[i] >= 'A' && s[i] <= 'Z' ? s[i] + 32 : s[i];
        if (c != name[i - from]) {
            return 0;
        }
    }
    return 1;
}

#define TAIL (0x10000 + 22)        /* the end record and the longest comment */

NAOMI_ZIP* naomi_zip_open(const char* path) {
    NAOMI_ZIP* z = NULL;
    FILE* f = fopen(path, "rb");
    uint8_t* tail = malloc(TAIL);
    long size, from, at;
    size_t got = 0;
    int i = -1;
    if (f != NULL && tail != NULL && fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) >= 22) {
        from = size > TAIL ? size - TAIL : 0;
        got = fseek(f, from, SEEK_SET) == 0 ? fread(tail, 1, (size_t)(size - from), f) : 0;
    }
    for (i = (int)got - 22; i >= 0 && get32(tail + i) != 0x06054B50; i--) {
    }
    if (i >= 0 && (z = calloc(1, sizeof *z)) != NULL) {
        z->f = f;
        f = NULL;
        z->entries = get16(tail + i + 10);
        z->dir_size = get32(tail + i + 12);
        at = (long)get32(tail + i + 16);
        z->dir = malloc(z->dir_size ? z->dir_size : 1);
        if (z->dir == NULL || fseek(z->f, at, SEEK_SET) != 0 || fread(z->dir, 1, z->dir_size, z->f) != z->dir_size) {
            naomi_zip_close(z);
            z = NULL;
        }
    }
    if (f != NULL) {
        fclose(f);
    }
    free(tail);
    return z;
}

void naomi_zip_close(NAOMI_ZIP* z) {
    if (z != NULL) {
        fclose(z->f);
        free(z->dir);
        free(z);
    }
}

/* the member's entry in the central directory, or NULL */
static const uint8_t* entry(const NAOMI_ZIP* z, const char* name) {
    uint32_t at = 0, i;
    for (i = 0; i < z->entries && at + 46 <= z->dir_size && get32(z->dir + at) == 0x02014B50; i++) {
        const uint8_t* e = z->dir + at;
        uint32_t n = get16(e + 28);
        if (at + 46 + n <= z->dir_size && same_name(e + 46, n, name)) {
            return e;
        }
        at += 46 + n + get16(e + 30) + get16(e + 32);
    }
    return NULL;
}

int naomi_zip_has(const NAOMI_ZIP* z, const char* name, uint32_t size) {
    const uint8_t* e = entry(z, name);
    return e != NULL && get32(e + 24) == size && (get16(e + 10) == 0 || get16(e + 10) == 8);
}

/* inflate */

static int need(INPUT* in, uint32_t n) {
    while (in->nbits < n) {
        uint32_t c = 0;
        if (in->pos == in->len && in->left > 0) {
            in->len = in->left < IN_SIZE ? in->left : IN_SIZE;
            if (fread(in->buf, 1, in->len, in->f) != in->len) {
                return 0;
            }
            in->left -= in->len;
            in->pos = 0;
        }
        if (in->pos < in->len) {
            c = in->buf[in->pos++];
        } else if (++in->pad > MAX_PAD) {
            return 0;
        }
        in->bits |= c << in->nbits;
        in->nbits += 8;
    }
    return 1;
}

static int take(INPUT* in, uint32_t n, uint32_t* v) {
    if (!need(in, n)) {
        return 0;
    }
    *v = in->bits & ((1u << n) - 1);
    in->bits >>= n;
    in->nbits -= n;
    return 1;
}

/* 0 when the lengths make no code */
static int build(HUFF* h, const uint8_t* length, int n) {
    uint16_t offs[MAX_BITS + 1], next[MAX_BITS + 1];
    int left = 1, len, s, code = 0;
    memset(h->count, 0, sizeof h->count);
    memset(h->fast, 0, sizeof h->fast);
    for (s = 0; s < n; s++) {
        h->count[length[s]]++;
    }
    for (len = 1; len <= MAX_BITS; len++) {
        left = (left << 1) - h->count[len];
        if (left < 0) {
            return 0;
        }
    }
    offs[1] = 0;
    for (len = 1; len < MAX_BITS; len++) {
        offs[len + 1] = (uint16_t)(offs[len] + h->count[len]);
    }
    for (s = 0; s < n; s++) {
        if (length[s] != 0) {
            h->symbol[offs[length[s]]++] = (uint16_t)s;
        }
    }
    h->count[0] = 0;
    for (len = 1; len <= MAX_BITS; len++) {
        code = (code + h->count[len - 1]) << 1;
        next[len] = (uint16_t)code;
    }
    for (s = 0; s < n; s++) {
        int l = length[s], c, r = 0, i;
        if (l == 0) {
            continue;
        }
        c = next[l]++;
        if (l > FAST_BITS) {
            continue;
        }
        for (i = 0; i < l; i++) {
            r |= ((c >> i) & 1) << (l - 1 - i);
        }
        for (i = r; i < 1 << FAST_BITS; i += 1 << l) {
            h->fast[i] = (uint16_t)(s << 4 | l);
        }
    }
    return 1;
}

static int decode(INPUT* in, const HUFF* h) {
    int code = 0, first = 0, index = 0, len;
    uint32_t e;
    if (!need(in, MAX_BITS)) {
        return -1;
    }
    e = h->fast[in->bits & ((1u << FAST_BITS) - 1)];
    if (e != 0) {
        in->bits >>= e & 15;
        in->nbits -= e & 15;
        return (int)(e >> 4);
    }
    for (len = 1; len <= MAX_BITS; len++) {
        int count = h->count[len];
        code |= (in->bits >> (len - 1)) & 1;
        if (code - count < first) {
            in->bits >>= len;
            in->nbits -= len;
            return h->symbol[index + (code - first)];
        }
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    return -1;
}

static const uint16_t len_base[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                       35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const uint8_t len_extra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const uint16_t dist_base[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
                                        1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const uint8_t dist_extra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8,
                                        9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static HUFF lit, dist;
static void (*step)(uint32_t done);
static uint32_t step_at;

static int codes(INPUT* in, uint8_t* dst, uint32_t size, uint32_t* out) {
    for (;;) {
        uint32_t len, d, x;
        int s = decode(in, &lit);
        if (*out >= step_at && step != NULL) {
            step(*out);
            step_at = *out + STEP;
        }
        if (s < 0) {
            return 0;
        }
        if (s < 256) {
            if (*out >= size) {
                return 0;
            }
            dst[(*out)++] = (uint8_t)s;
            continue;
        }
        if (s == 256) {
            return 1;
        }
        s -= 257;
        if (s >= 29 || !take(in, len_extra[s], &x)) {
            return 0;
        }
        len = len_base[s] + x;
        s = decode(in, &dist);
        if (s < 0 || s >= 30 || !take(in, dist_extra[s], &x)) {
            return 0;
        }
        d = dist_base[s] + x;
        if (d > *out || len > size - *out) {
            return 0;
        }
        for (; len > 0; len--, (*out)++) {
            dst[*out] = dst[*out - d];
        }
    }
}

static int dynamic(INPUT* in) {
    static const uint8_t order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    uint8_t length[320];
    uint32_t nlen, ndist, ncode, v;
    uint32_t i;
    if (!take(in, 5, &nlen) || !take(in, 5, &ndist) || !take(in, 4, &ncode)) {
        return 0;
    }
    nlen += 257;
    ndist += 1;
    ncode += 4;
    if (nlen > 286 || ndist > 30) {
        return 0;
    }
    memset(length, 0, 19);
    for (i = 0; i < ncode; i++) {
        if (!take(in, 3, &v)) {
            return 0;
        }
        length[order[i]] = (uint8_t)v;
    }
    if (!build(&lit, length, 19)) {
        return 0;
    }
    for (i = 0; i < nlen + ndist;) {
        int s = decode(in, &lit);
        uint32_t repeat, value = 0;
        if (s < 0) {
            return 0;
        }
        if (s < 16) {
            length[i++] = (uint8_t)s;
            continue;
        }
        if (s == 16) {
            if (i == 0 || !take(in, 2, &repeat)) {
                return 0;
            }
            value = length[i - 1];
            repeat += 3;
        } else if (s == 17) {
            if (!take(in, 3, &repeat)) {
                return 0;
            }
            repeat += 3;
        } else {
            if (!take(in, 7, &repeat)) {
                return 0;
            }
            repeat += 11;
        }
        if (i + repeat > nlen + ndist) {
            return 0;
        }
        while (repeat-- > 0) {
            length[i++] = (uint8_t)value;
        }
    }
    return length[256] != 0 && build(&lit, length, (int)nlen) && build(&dist, length + nlen, (int)ndist);
}

static void fixed(void) {
    uint8_t length[288];
    int i;
    for (i = 0; i < 288; i++) {
        length[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
    }
    build(&lit, length, 288);
    for (i = 0; i < 30; i++) {
        length[i] = 5;
    }
    build(&dist, length, 30);
}

static int inflate(INPUT* in, uint8_t* dst, uint32_t size) {
    uint32_t out = 0, last, type, n, check;
    do {
        if (!take(in, 1, &last) || !take(in, 2, &type)) {
            return 0;
        }
        if (type == 0) {
            in->bits >>= in->nbits & 7;
            in->nbits -= in->nbits & 7;
            if (!take(in, 16, &n) || !take(in, 16, &check) || (n ^ 0xFFFF) != check || n > size - out) {
                return 0;
            }
            for (; n > 0; n--) {
                if (!take(in, 8, &check)) {
                    return 0;
                }
                dst[out++] = (uint8_t)check;
            }
        } else if (type == 1) {
            fixed();
            if (!codes(in, dst, size, &out)) {
                return 0;
            }
        } else if (type == 2) {
            if (!dynamic(in) || !codes(in, dst, size, &out)) {
                return 0;
            }
        } else {
            return 0;
        }
    } while (!last);
    return out == size;
}

static uint32_t crc32(const uint8_t* p, uint32_t n) {
    static uint32_t table[256];
    uint32_t c = 0xFFFFFFFF, i;
    if (table[1] == 0) {
        for (i = 0; i < 256; i++) {
            uint32_t v = i;
            int k;
            for (k = 0; k < 8; k++) {
                v = v & 1 ? 0xEDB88320 ^ (v >> 1) : v >> 1;
            }
            table[i] = v;
        }
    }
    for (i = 0; i < n; i++) {
        c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFF;
}

/* the member's size bytes into dst, its CRC checked; 1 when done. step, if any, is called as it goes */
int naomi_zip_read(NAOMI_ZIP* z, const char* name, uint8_t* dst, uint32_t size, void (*on_step)(uint32_t done)) {
    const uint8_t* e = entry(z, name);
    uint8_t local[30];
    INPUT* in;
    int ok;
    if (e == NULL || get32(e + 24) != size) {
        return 0;
    }
    if (fseek(z->f, (long)get32(e + 42), SEEK_SET) != 0 || fread(local, 1, 30, z->f) != 30 || get32(local) != 0x04034B50
        || fseek(z->f, (long)(get16(local + 26) + get16(local + 28)), SEEK_CUR) != 0) {
        return 0;
    }
    if (get16(e + 10) == 0) {
        ok = get32(e + 20) == size && fread(dst, 1, size, z->f) == size;
    } else if (get16(e + 10) == 8 && (in = malloc(sizeof *in)) != NULL) {
        in->f = z->f;
        in->left = get32(e + 20);
        in->pos = in->len = in->bits = in->nbits = in->pad = 0;
        step = on_step;
        step_at = 0;
        ok = inflate(in, dst, size);
        step = NULL;
        free(in);
    } else {
        ok = 0;
    }
    if (ok && crc32(dst, size) != get32(e + 16)) {
        host_log("%s in the zip fails its CRC", name);
        ok = 0;
    }
    return ok;
}
