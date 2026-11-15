/*
 * nl_port.h  What the port's NAOMI library routines share
 */

#ifndef NL_PORT_H
#define NL_PORT_H

#include <stddef.h>
#include <stdint.h>

#include "rd.h"
#include "host_assets.h"

typedef struct {
    RD_TEXTURE tex;
    int type;
    int loaded;
    int palette;        /* the bank a sprite draws with */
    size_t bytes;
} NL_TEX;

#define NL_MAX_TEX 0x1000

extern NL_TEX nl_tex[NL_MAX_TEX];
extern int nl_tex_offset;
extern int nl_palette_direct;

int nl_tex_load(NL_TEX* t, const NLthd* thd);
int nl_tex_reload(NL_TEX* t, const void* texels);
void nl_tex_free(NL_TEX* t);        /* the game's free: its texels given back when the renderer is past them */
void nl_tex_frame(void);            /* before each frame is given to the renderer */
void nl_tex_free_all(void);
uint64_t nl_hash64(const void* src, size_t n);    /* a texture source's, as its cache keeps them */

/* nlObjPutSetFadeColor's, 0 to 1 */
extern float nl_fade_r, nl_fade_g, nl_fade_b;

static inline uint16_t nl_palette_base(const NL_TEX* t, int bank) {
    return (uint16_t)(t->tex.format == RD_PAL8 ? (bank & 3) * 256 : (bank & 63) * 16);
}

/* 0 at or below 0, 255 at or above 1. The bounds are tested on the float's bits: on the ARM11 each
   VFP compare waits for the pipeline to drain. A NaN is converted as it is. */
static inline __attribute__((always_inline)) uint32_t nl_float_byte(float f) {
    union {
        float f;
        int32_t i;
    } u = { f };
    if ((u.i > 0 && u.i < 0x3F800000) || (u.i & 0x7FFFFFFF) > 0x7F800000) {
        return (uint32_t)(int)(f * 255.0f + 0.5f);
    }
    return u.i > 0 ? 255 : 0;
}

static inline __attribute__((always_inline)) uint32_t nl_float_argb(float a, float r, float g, float b) {
    return nl_float_byte(a) << 24 | nl_float_byte(r) << 16 | nl_float_byte(g) << 8 | nl_float_byte(b);
}

/* nl_xmtr.c's current matrix, by rows, and the view matrix */
const float* nl_xmtrx(void);
const float* nl_view_matrix(void);

/* nl_work.c: nlSetScaleFactor's scale, set back to 1 */
float nl_take_scale_factor(void);

#endif
