/*
 * nl_trig.c  The 3DS keeps the last answers of nlmath's angle routines: the linker's --wrap puts these in front
 * of them, for the game's calls and nlproj's. Each answer depends on the argument alone, so a kept one is the
 * same. MVC2_TRIGMEMO=0: none kept.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define WAYS 256

float __real_nlSin(int ang);
float __real_nlCos(int ang);
float __real_nlTan(int ang);
int32_t __real_nlArcTan(float x);

/* key 0: empty */
typedef struct {
    uint32_t key;
    union {
        float f;
        int32_t i;
    } v;
} MEMO;

static MEMO sin_m[WAYS], cos_m[WAYS], tan_m[WAYS], atan_m[WAYS];
static int memo_on = -1;

static int on(void) {
    if (memo_on < 0) {
        const char* e = getenv("MVC2_TRIGMEMO");
        memo_on = e == NULL || atoi(e) != 0;
    }
    return memo_on;
}

static MEMO* way(MEMO* m, uint32_t key) {
    return &m[(key ^ (key >> 8) ^ (key >> 16) ^ (key >> 24)) & (WAYS - 1)];
}

/* an angle's key: its 16 bits, plus one */
#define ANGLE(name, table)                          \
    float __wrap_##name(int ang) {                  \
        uint32_t key = (uint32_t)(ang & 0xffff) + 1; \
        MEMO* m = way(table, key);                  \
        if (!on()) {                                \
            return __real_##name(ang);              \
        }                                           \
        if (m->key != key) {                        \
            m->v.f = __real_##name(ang);            \
            m->key = key;                           \
        }                                           \
        return m->v.f;                              \
    }

ANGLE(nlSin, sin_m)
ANGLE(nlCos, cos_m)
ANGLE(nlTan, tan_m)

/* the float's bits; the answer, 0 to 0xffff, kept plus one */
int32_t __wrap_nlArcTan(float x) {
    uint32_t key;
    MEMO* m;
    if (!on()) {
        return __real_nlArcTan(x);
    }
    memcpy(&key, &x, sizeof key);
    m = way(atan_m, key);
    if (m->v.i == 0 || m->key != key) {
        m->key = key;
        m->v.i = __real_nlArcTan(x) + 1;
    }
    return m->v.i - 1;
}
