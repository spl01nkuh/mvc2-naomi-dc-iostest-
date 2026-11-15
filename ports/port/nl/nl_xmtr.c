/*
 * nl_xmtr.c  The NAOMI library's matrices (nlxmtr, nlvect). XMTRX is a plain array: 4x4 by rows,
 * p' = p x XMTRX, each transform in front; 0x10000 a turn.
 */

#include <math.h>
#include <string.h>
#include "host_matrix.h"
#include "host_sections.h"

typedef float NLMTX[16];

typedef struct {
    float x, y, z;
} NLV3;

typedef struct {
    float x, y, z, w;
} NLV4;

typedef MVC2HostMatrixStack NLMATRIXSTACK;

#define VIEW_DEPTH 8
#define PROJ_DEPTH 8

#ifdef __3DS__
/* on the 3DS each matrix in two cache lines, and copied in four ldm/stm pairs */
#define LINES __attribute__((aligned(32)))
typedef struct {
    float f[16];
} MTX_COPY;
#define COPY(d, s) (*(MTX_COPY*)(d) = *(const MTX_COPY*)(s))
#else
#define LINES
#define COPY(d, s) memcpy((d), (s), sizeof(NLMTX))
#endif

NLMATRIXSTACK nlMatrixStack[4];
static unsigned char matrix_mode = 1;
static NLMTX xm LINES;
static NLMTX view_stack[VIEW_DEPTH + 1] LINES;
static NLMTX proj_stack[PROJ_DEPTH + 1] LINES;

static const NLMTX unit LINES = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

#ifndef __3DS__
static void mul(float* d, const float* a, const float* b) {
    NLMTX t;
    int r, c;
    for (r = 0; r < 4; r++) {
        for (c = 0; c < 4; c++) {
            t[r * 4 + c] = a[r * 4] * b[c] + a[r * 4 + 1] * b[4 + c] + a[r * 4 + 2] * b[8 + c] + a[r * 4 + 3] * b[12 + c];
        }
    }
    COPY(d, t);
}
#endif

static float sin_a(int ang) {
    return (float)sin((ang & 0xffff) * (6.283185307179586 / 65536.0));
}

static float cos_a(int ang) {
    return (float)cos((ang & 0xffff) * (6.283185307179586 / 65536.0));
}

#ifdef __3DS__
/* a row's four sums made side by side, each summed in the plain one's order: the VFP's multiply-adds wait for the
   one before on the same sum only */
static void mul(float* d, const float* a, const float* b) {
    NLMTX t;
    float b0 = b[0], b1 = b[1], b2 = b[2], b3 = b[3], b4 = b[4], b5 = b[5], b6 = b[6], b7 = b[7];
    float b8 = b[8], b9 = b[9], b10 = b[10], b11 = b[11], b12 = b[12], b13 = b[13], b14 = b[14], b15 = b[15];
    int r;
    for (r = 0; r < 4; r++) {
        float x0 = a[r * 4], x1 = a[r * 4 + 1], x2 = a[r * 4 + 2], x3 = a[r * 4 + 3];
        float c0 = x0 * b0, c1 = x0 * b1, c2 = x0 * b2, c3 = x0 * b3;
        c0 += x1 * b4, c1 += x1 * b5, c2 += x1 * b6, c3 += x1 * b7;
        c0 += x2 * b8, c1 += x2 * b9, c2 += x2 * b10, c3 += x2 * b11;
        c0 += x3 * b12, c1 += x3 * b13, c2 += x3 * b14, c3 += x3 * b15;
        t[r * 4] = c0, t[r * 4 + 1] = c1, t[r * 4 + 2] = c2, t[r * 4 + 3] = c3;
    }
    COPY(d, t);
}

/* the last angles' sines and cosines, the library's being doubles' */
static struct {
    int ang;    /* the angle + 1, 0 for none */
    float s, c;
} turns[256];

static void sin_cos(int ang, float* s, float* c) {
    int a = ang & 0xffff, k = (a ^ a >> 8) & 255;
    if (turns[k].ang != a + 1) {
        turns[k].ang = a + 1;
        turns[k].s = sin_a(a);
        turns[k].c = cos_a(a);
    }
    *s = turns[k].s;
    *c = turns[k].c;
}
#endif

/* ---- the stacks ---- */

static void stack_init(NLMATRIXSTACK* s, NLMTX* base, int n) {
    s->count = 0;
    s->max = (short)(n - 1);
    s->base = base;
    s->matrix = base;
    COPY(base, unit);
}

/* the model stack is the game's array, the others the library's; nlMatrixStack[0] is the one in
   use, 1 to 3 keep each while another is */
NLMTX* nlInitMatrix(NLMTX* m, int n) {
    NLMTX* base = (NLMTX*)(((unsigned long)m + 31) & ~31ul);
    stack_init(&nlMatrixStack[1], base, n);
    stack_init(&nlMatrixStack[2], view_stack, VIEW_DEPTH);
    stack_init(&nlMatrixStack[3], proj_stack, PROJ_DEPTH);
    matrix_mode = 1;
    nlMatrixStack[0] = nlMatrixStack[1];
    COPY(xm, unit);
    return base;
}

void nlLoadMatrix(const NLMTX* m) {
    COPY(xm, m);
}

/* NULL: XMTRX written back to the current matrix */
NLMTX* nlGetMatrix(NLMTX* m) {
    if (m == NULL) {
        m = nlMatrixStack[0].matrix;
    }
    COPY(m, xm);
    return m;
}

void nlMatrixMode(int mode) {
    nlGetMatrix(NULL);
    nlMatrixStack[matrix_mode] = nlMatrixStack[0];
    matrix_mode = (unsigned char)mode;
    nlMatrixStack[0] = nlMatrixStack[matrix_mode];
    nlLoadMatrix(nlMatrixStack[0].matrix);
}

/* XMTRX pushed; then m (NULL: the same) loaded */
int nlPushMatrix(const NLMTX* m) {
    NLMATRIXSTACK* s = &nlMatrixStack[0];
    int ok = s->count < s->max;
    COPY(s->matrix, xm);
    s->count++;
    if (ok) {
        s->matrix++;
    }
    if (m != NULL) {
        COPY(xm, m);
    }
    return ok;
}

static int pop(int n, int load) {
    NLMATRIXSTACK* s = &nlMatrixStack[0];
    int ok;
    if (n <= 0) {
        n = 1;
    }
    ok = s->count >= n;
    s->count = (short)(ok ? s->count - n : 0);
    if (s->count < s->max) {
        s->matrix = s->base + s->count;
    }
    if (load) {
        COPY(xm, s->matrix);
    }
    return ok;
}

int nlPopMatrix(int n) {
    return pop(n, 1);
}

int nlDecMatrixPtr(int n) {
    return pop(n, 0);
}

/* XMTRX = the matrix pushed last x XMTRX */
void nlMultiPushedMatrix(void) {
    mul(xm, *(nlMatrixStack[0].matrix - 1), xm);
}

void nlMultiMatrix(const NLMTX* m) {
    mul(xm, *m, xm);
}

NLMTX* nlGetMatrix8(NLMTX* m) {
    return nlGetMatrix(m);
}

void nlLoadMatrix8(const NLMTX* m) {
    nlLoadMatrix(m);
}

void nlMultiMatrix8(const NLMTX* m) {
    nlMultiMatrix(m);
}

void nlUnitMatrix(void) {
    COPY(xm, unit);
}

/* ---- transforms, each in front of XMTRX ---- */

void nlTranslate(float x, float y, float z) {
#ifdef __3DS__
    float t0 = x * xm[0], t1 = x * xm[1], t2 = x * xm[2], t3 = x * xm[3];
    t0 += y * xm[4], t1 += y * xm[5], t2 += y * xm[6], t3 += y * xm[7];
    t0 += z * xm[8], t1 += z * xm[9], t2 += z * xm[10], t3 += z * xm[11];
    xm[12] += t0, xm[13] += t1, xm[14] += t2, xm[15] += t3;
#else
    int c;
    for (c = 0; c < 4; c++) {
        xm[12 + c] += x * xm[c] + y * xm[4 + c] + z * xm[8 + c];
    }
#endif
}

void nlTranslateV(const NLV3* v) {
    nlTranslate(v->x, v->y, v->z);
}

void nlScale(float x, float y, float z) {
    int c;
    for (c = 0; c < 4; c++) {
        xm[c] *= x;
        xm[4 + c] *= y;
        xm[8 + c] *= z;
    }
}

void nlScaleV(const NLV3* v) {
    nlScale(v->x, v->y, v->z);
}

/* rows i and j of XMTRX turned by ang */
static void rotate(int i, int j, int ang) {
#ifdef __3DS__
    float s, c, *p = xm + i * 4, *q = xm + j * 4;
    float a0 = p[0], a1 = p[1], a2 = p[2], a3 = p[3], b0 = q[0], b1 = q[1], b2 = q[2], b3 = q[3];
    sin_cos(ang, &s, &c);
    p[0] = c * a0 + s * b0, p[1] = c * a1 + s * b1, p[2] = c * a2 + s * b2, p[3] = c * a3 + s * b3;
    q[0] = -s * a0 + c * b0, q[1] = -s * a1 + c * b1, q[2] = -s * a2 + c * b2, q[3] = -s * a3 + c * b3;
#else
    float s = sin_a(ang), c = cos_a(ang);
    int k;
    for (k = 0; k < 4; k++) {
        float a = xm[i * 4 + k], b = xm[j * 4 + k];
        xm[i * 4 + k] = c * a + s * b;
        xm[j * 4 + k] = -s * a + c * b;
    }
#endif
}

void nlRotateAX(int ang) {
    rotate(1, 2, ang);
}

void nlRotateAY(int ang) {
    rotate(2, 0, ang);
}

void nlRotateAZ(int ang) {
    rotate(0, 1, ang);
}

/* ---- points and vectors ---- */

void nlCalcPoint(const NLV3* s, NLV3* d) {
    float x = s->x, y = s->y, z = s->z;
    d->x = x * xm[0] + y * xm[4] + z * xm[8] + xm[12];
    d->y = x * xm[1] + y * xm[5] + z * xm[9] + xm[13];
    d->z = x * xm[2] + y * xm[6] + z * xm[10] + xm[14];
}

void nlCalcPoint4(const NLV3* s, NLV4* d) {
    float x = s->x, y = s->y, z = s->z;
    d->x = x * xm[0] + y * xm[4] + z * xm[8] + xm[12];
    d->y = x * xm[1] + y * xm[5] + z * xm[9] + xm[13];
    d->z = x * xm[2] + y * xm[6] + z * xm[10] + xm[14];
    d->w = x * xm[3] + y * xm[7] + z * xm[11] + xm[15];
}

void nlCalcVector(const NLV3* s, NLV3* d) {
    float x = s->x, y = s->y, z = s->z;
    d->x = x * xm[0] + y * xm[4] + z * xm[8];
    d->y = x * xm[1] + y * xm[5] + z * xm[9];
    d->z = x * xm[2] + y * xm[6] + z * xm[10];
}

/* returns the length before */
float nlUnitVector(NLV3* v) {
    float n = sqrtf(v->x * v->x + v->y * v->y + v->z * v->z);
    if (n != 0.0f) {
        float r = 1.0f / n;
        v->x *= r;
        v->y *= r;
        v->z *= r;
    }
    return n;
}

/* +X right, +Y up, +Z towards the eye */
void nlLookAt(const NLV3* eye, const NLV3* obj, int z) {
    NLV3 f, r, u;
    NLMTX m;
    f.x = eye->x - obj->x;
    f.y = eye->y - obj->y;
    f.z = eye->z - obj->z;
    nlUnitVector(&f);
    r.x = f.z;              /* (0, 1, 0) x f */
    r.y = 0.0f;
    r.z = -f.x;
    if (nlUnitVector(&r) == 0.0f) {
        r.x = 1.0f;
    }
    u.x = f.y * r.z - f.z * r.y;
    u.y = f.z * r.x - f.x * r.z;
    u.z = f.x * r.y - f.y * r.x;
    m[0] = r.x, m[1] = u.x, m[2] = f.x, m[3] = 0.0f;
    m[4] = r.y, m[5] = u.y, m[6] = f.y, m[7] = 0.0f;
    m[8] = r.z, m[9] = u.z, m[10] = f.z, m[11] = 0.0f;
    m[12] = -(eye->x * r.x + eye->y * r.y + eye->z * r.z);
    m[13] = -(eye->x * u.x + eye->y * u.y + eye->z * u.z);
    m[14] = -(eye->x * f.x + eye->y * f.y + eye->z * f.z);
    m[15] = 1.0f;
    if (z != 0) {
        NLMTX t;
        memcpy(t, unit, sizeof t);
#ifdef __3DS__
        sin_cos(z, &t[1], &t[0]);
        t[5] = t[0];
#else
        t[0] = t[5] = cos_a(z);
        t[1] = sin_a(z);
#endif
        t[4] = -t[1];
        mul(m, m, t);
    }
    mul(xm, m, xm);
}

#if defined(__3DS__) && defined(MVC2_PROF)
/* ---- MVC2_XMTR_BENCH: the plain routines the 3DS's replace, timed beside them ---- */

void nl_xmtr_old_multi(const NLMTX* m) {
    NLMTX t;
    int r, c;
    for (r = 0; r < 4; r++) {
        for (c = 0; c < 4; c++) {
            t[r * 4 + c] = (*m)[r * 4] * xm[c] + (*m)[r * 4 + 1] * xm[4 + c] + (*m)[r * 4 + 2] * xm[8 + c] +
                           (*m)[r * 4 + 3] * xm[12 + c];
        }
    }
    COPY(xm, t);
}

void nl_xmtr_old_translate(float x, float y, float z) {
    int c;
    for (c = 0; c < 4; c++) {
        xm[12 + c] += x * xm[c] + y * xm[4 + c] + z * xm[8 + c];
    }
}

void nl_xmtr_old_rotate_y(int ang) {
    float s = sin_a(ang), c = cos_a(ang);
    int k;
    for (k = 0; k < 4; k++) {
        float a = xm[8 + k], b = xm[k];
        xm[8 + k] = c * a + s * b;
        xm[k] = -s * a + c * b;
    }
}
#endif

/* ---- for nlproj.c's assembler routines ---- */

/* XMTRX = XMTRX x a x b */
void nl_host_mul_matrix_ftrv(const NLMTX* a, const NLMTX* b) {
    mul(xm, xm, *a);
    mul(xm, xm, *b);
}

/* as load_matrix_ftrv_asm: each row (m3, m0, m1, -m3) */
void nl_host_load_matrix_ftrv(const NLMTX* m) {
    int r;
    for (r = 0; r < 4; r++) {
        const float* s = *m + r * 4;
        xm[r * 4 + 0] = s[3];
        xm[r * 4 + 1] = s[0];
        xm[r * 4 + 2] = s[1];
        xm[r * 4 + 3] = -s[3];
    }
}

/* ---- for the model routines (nl_obj.c) ---- */

__attribute__((section(MVC2_NL_HOT_SECTION))) const float* nl_xmtrx(void) {
    return xm;
}

__attribute__((section(MVC2_NL_HOT_SECTION))) const float* nl_view_matrix(void) {
    return *nlMatrixStack[2].matrix;
}
