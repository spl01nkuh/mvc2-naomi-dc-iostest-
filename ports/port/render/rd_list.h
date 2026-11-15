/*
 * rd_list.h  The frame's lists, as the back ends read them
 */

#ifndef RD_LIST_H
#define RD_LIST_H

#include <string.h>

#include "rd.h"

#define RD_LISTS 3
#ifdef __3DS__
/* the frame's triangles, the lists sharing them by their recent use, a full part growing into the rest: an Old
   3DS's heap has no room for each list's most at once (the game sends at most about 3800 a frame) */
#define RD_POOL_TRIS 7168
#define RD_MAX_TRIS RD_POOL_TRIS
#else
#define RD_MAX_TRIS 32768
#define RD_POOL_TRIS (RD_LISTS * RD_MAX_TRIS)
#endif
#define RD_MAX_STATES 8192

typedef struct {
    uint16_t state;
    uint8_t share;      /* v[0] and v[1] as the list's triangle before: in bits 0-1 and 2-3, its corner + 1 (0: not known) */
    uint8_t stays;      /* 1: not culled by its area (its state culls nothing, or the area, summed as the back ends
                           sum it, is at least RD_CULL_VAL across and faces the drawn way); 0: not known */
    RD_VTX v[3];
} RD_TRI;

#define RD_SHARE(a, b) (((a) + 1) | ((b) + 1) << 2)

#ifdef __3DS__
/* a triangle's flags as the 3DS back end reads them: culled by its state, no pixel on the screen, its corners' 1/w
   the same bits, a share with the triangle before */
enum { RD_TRI_CULLED = 1, RD_TRI_UNSEEN = 2, RD_TRI_FLAT_Z = 4, RD_TRI_SHARES = 8 };

/* a triangle's nearest and farthest 1/w, state and flags, made as it is added, so that the back end's passes over
   them read 12 bytes a triangle, not the triangle; written whole, as an Old 3DS stores outside its cache a word at
   a time */
typedef struct {
    float lo, hi;
    uint16_t state;
    uint8_t flags;
    uint8_t pad;
} RD_TRI_INFO;

/* f < -1, on the float's bits: from -1's next down to -infinity, no NaN */
static inline int rd_below_m1(float f) {
    uint32_t b;
    memcpy(&b, &f, 4);
    return b - 0xBF800001u <= 0xFF800000u - 0xBF800001u;
}

/* f > c for a positive c, on the bits: from c's next up to infinity */
static inline int rd_above_c(float f, uint32_t c) {
    uint32_t b;
    memcpy(&b, &f, 4);
    return b - (c + 1) <= 0x7F800000u - (c + 1);
}

#define RD_BITS_641 0x44204000u     /* RD_WIDTH + 1.0f */
#define RD_BITS_481 0x43F08000u     /* RD_HEIGHT + 1.0f */

/* no corner on the screen's side of one of its edges: it has no pixel */
static inline int rd_unseen(const RD_VTX* a, const RD_VTX* b, const RD_VTX* c) {
    return (rd_below_m1(a->x) & rd_below_m1(b->x) & rd_below_m1(c->x)) |
           (rd_below_m1(a->y) & rd_below_m1(b->y) & rd_below_m1(c->y)) |
           (rd_above_c(a->x, RD_BITS_641) & rd_above_c(b->x, RD_BITS_641) & rd_above_c(c->x, RD_BITS_641)) |
           (rd_above_c(a->y, RD_BITS_481) & rd_above_c(b->y, RD_BITS_481) & rd_above_c(c->y, RD_BITS_481));
}

/* drawn in any order among others like it: tested against the depth and writing it, not blended. The 3DS's slices
   take their own parts of the depth buffer, so their order doesn't matter either */
static inline int rd_order_free(const RD_STATE* s) {
    return s->list != RD_TRANS && s->zwrite && s->depth != RD_NEVER && s->depth != RD_EQUAL && s->depth != RD_NOTEQUAL &&
           s->depth != RD_ALWAYS && s->src == RD_ONE && s->dst == RD_ZERO;
}

/* with rd_seen set: a state's texture is wanted, a triangle in it being opaque, punch-through or translucent with a
   pixel on the screen, or a group drawn in it */
extern int rd_seen;
#endif
#define RD_CULL_VAL 0.02    /* FPU_CULL_VAL, as libnaomi sets it: a smaller triangle is culled when its state culls */

#define RD_GPU_PUTS 128
#define RD_GPU_GROUPS 512

typedef struct {
    float m[16];
    float near_w;
} RD_GPU_PUT;

typedef struct {
    RD_GPU_GROUP g;
    uint16_t put, state;
    int at;                 /* its list's triangles before it */
} RD_GPU_DRAW;

#ifdef __3DS__
/* with rd_group_info set, a group's depths and list as it is added, so that the back end's first pass over the groups
   reads 24 bytes a group, not the group, its put and its state; written whole */
typedef struct {
    float wlo, whi, key;    /* the group's, its pass's key for a deep one drawn in two */
    float near_w;           /* its put's */
    int at;
    int16_t mesh;           /* -1 past 32767 */
    uint8_t list, free;     /* its state's list and rd_order_free */
} RD_GPU_INFO;

extern int rd_group_info;
#endif

typedef struct {
    RD_TRI* tris[RD_LISTS];     /* each list's part of pool, set by rd_begin_frame */
    int ntris[RD_LISTS];
    int room[RD_LISTS];         /* its part's size */
    RD_STATE states[RD_MAX_STATES];
    int nstates;
    RD_VTX background[3];   /* its corners: top left, top right, bottom left */
    RD_GPU_PUT puts[RD_GPU_PUTS];
    int nputs;
    RD_GPU_DRAW groups[RD_GPU_GROUPS];
    int ngroups;
    RD_TRI pool[RD_POOL_TRIS];
#ifdef __3DS__
    RD_TRI_INFO* info[RD_LISTS];   /* in step with tris */
    RD_TRI_INFO info_pool[RD_POOL_TRIS];
    int dropped;                    /* triangles past a full pool */
    int moved;                      /* times the parts were laid out again for a full one */
    uint8_t seen[RD_MAX_STATES];    /* with rd_seen, by state */
    RD_GPU_INFO ginfo[RD_GPU_GROUPS];   /* with rd_group_info, by group */
#endif
} RD_FRAME;

extern RD_FRAME* rd_frame;   /* the one the game fills: a back end may give it another */

/* memcmp(a, b, sizeof *a) == 0, a word at a time without the call */
static inline int rd_same_state(const RD_STATE* a, const RD_STATE* b) {
    uint32_t d = 0, x, y;
    int i;
#pragma GCC unroll 8
    for (i = 0; i < (int)(sizeof *a / 4); i++) {
        memcpy(&x, (const char*)a + 4 * i, 4);
        memcpy(&y, (const char*)b + 4 * i, 4);
        d |= x ^ y;
    }
    return d == 0;
}

typedef struct {
    float table[128];       /* 0 to 1, in steps of 1/255 as the registers hold them */
    uint32_t color;
    float density;
} RD_FOG;

extern RD_FOG rd_fog;

/* the quality words rd_quality last took, each 1 when given; a back end does what it can of them */
typedef struct {
    uint8_t half, fast, nofog, nosort;
} RD_QUALITY;

extern RD_QUALITY rd_q;

#endif
