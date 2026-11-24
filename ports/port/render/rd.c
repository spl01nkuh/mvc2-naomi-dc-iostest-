/*
 * rd.c  The frame's triangles, gathered by list for a back end
 */

#include <math.h>
#include <string.h>

#include "rd.h"
#include "rd_list.h"
#include "../include/host_sections.h"

uint32_t rd_palette[1024];
uint32_t rd_palette_serial;

static RD_FRAME first_frame;
RD_FRAME* rd_frame = &first_frame;
RD_QUALITY rd_q;

#if defined(HOST_NATIVE64) || defined(RD_NATIVE_SYSTEM)
static int native_widescreen;
static int native_widescreen_requested;
#endif

void rd_set_widescreen_preference(int enabled) {
#if defined(HOST_NATIVE64) || defined(RD_NATIVE_SYSTEM)
    native_widescreen_requested = enabled != 0;
#else
    (void)enabled;
#endif
}

void rd_begin_stage_scene(void) {
#if defined(HOST_NATIVE64) || defined(RD_NATIVE_SYSTEM)
    if (!native_widescreen_requested || rd_frame->nputs != 0) return;
    /* Keep the frame narrow if another drawing pass has already used the
     * original viewport. All geometry uses one origin and clip width. */
    for (int i = 0; i < RD_LISTS; ++i) {
        if (rd_frame->ntris[i] != 0) return;
    }
    rd_set_widescreen(1);
#endif
}

void rd_set_widescreen(int enabled) {
#if defined(HOST_NATIVE64) || defined(RD_NATIVE_SYSTEM)
    enabled = enabled != 0;
    if (native_widescreen == enabled) return;
    float old_offset = native_widescreen ? (RD_MAX_WIDTH - RD_WIDTH) * 0.5f : 0.0f;
    float new_offset = enabled ? (RD_MAX_WIDTH - RD_WIDTH) * 0.5f : 0.0f;
    native_widescreen = enabled;
    for (int i = 0; i < 3; ++i) rd_frame->background[i].x += new_offset - old_offset;
#else
    (void)enabled;
#endif
}

int rd_width(void) {
#if defined(HOST_NATIVE64) || defined(RD_NATIVE_SYSTEM)
    return native_widescreen ? RD_MAX_WIDTH : RD_WIDTH;
#else
    return RD_WIDTH;
#endif
}

int rd_height(void) { return RD_HEIGHT; }

float rd_widescreen_offset(void) { return (rd_width() - RD_WIDTH) * 0.5f; }

void rd_quality(const char* words) {
    rd_q.half = words != NULL && strstr(words, "half") != NULL;
    rd_q.fast = words != NULL && strstr(words, "fast") != NULL;
    rd_q.nofog = words != NULL && strstr(words, "nofog") != NULL;
    rd_q.nosort = words != NULL && strstr(words, "nosort") != NULL;
}

#ifdef __3DS__
/* each list's part of the pool: its share of their recent most, each with a quarter more and 512 over. A list that
   ran out with the pool full is taken to want twice its part. Its most falls by a 64th a frame */
static void share_pool(RD_FRAME* f) {
    static int most[RD_LISTS];
    int want[RD_LISTS], i, sum = 0, at = 0;
    for (i = 0; i < RD_LISTS; i++) {
        int n = f->room[i] > 0 && f->ntris[i] >= f->room[i] ? f->room[i] * 2 : f->ntris[i];
        most[i] = n > most[i] - most[i] / 64 ? n : most[i] - most[i] / 64;
        want[i] = most[i] + most[i] / 4 + 512;
        sum += want[i];
    }
    for (i = 0; i < RD_LISTS; i++) {
        int room = (int)((int64_t)RD_POOL_TRIS * want[i] / sum);
        f->tris[i] = &f->pool[at];
        f->info[i] = &f->info_pool[at];
        f->room[i] = room;
        at += room;
    }
    f->dropped = 0;
    f->moved = 0;
}

/* a list's part full: the parts laid out again, each keeping its triangles, the list given half of the pool's unused
   rest and the others the other half. 0 when none is left */
static int grow_part(RD_FRAME* f, int list) {
    int start[RD_LISTS], room[RD_LISTS], i, used = 0, spare, mine, at = 0;
    for (i = 0; i < RD_LISTS; i++) {
        used += f->ntris[i];
    }
    spare = RD_POOL_TRIS - used;
    if (spare <= 0) {
        return 0;
    }
    mine = (spare + 1) / 2;
    for (i = 0; i < RD_LISTS; i++) {
        room[i] = f->ntris[i] + (i == list ? mine : (spare - mine) / (RD_LISTS - 1));
        start[i] = at;
        at += room[i];
    }
    room[RD_LISTS - 1] += RD_POOL_TRIS - at;
    /* those moving up from the last, then those moving down from the first: none lands on one not yet moved */
    for (i = RD_LISTS - 1; i >= 0; i--) {
        if (&f->pool[start[i]] > f->tris[i]) {
            memmove(&f->pool[start[i]], f->tris[i], (size_t)f->ntris[i] * sizeof(RD_TRI));
            memmove(&f->info_pool[start[i]], f->info[i], (size_t)f->ntris[i] * sizeof(RD_TRI_INFO));
        }
    }
    for (i = 0; i < RD_LISTS; i++) {
        if (&f->pool[start[i]] < f->tris[i]) {
            memmove(&f->pool[start[i]], f->tris[i], (size_t)f->ntris[i] * sizeof(RD_TRI));
            memmove(&f->info_pool[start[i]], f->info[i], (size_t)f->ntris[i] * sizeof(RD_TRI_INFO));
        }
        f->tris[i] = &f->pool[start[i]];
        f->info[i] = &f->info_pool[start[i]];
        f->room[i] = room[i];
    }
    f->moved++;
    return 1;
}
#endif

void rd_begin_frame(void) {
    int i;
#if defined(HOST_NATIVE64) || defined(RD_NATIVE_SYSTEM)
    rd_set_widescreen(0);
#endif
#ifdef __3DS__
    share_pool(rd_frame);
#else
    for (i = 0; i < RD_LISTS; i++) {
        rd_frame->tris[i] = &rd_frame->pool[i * RD_MAX_TRIS];
        rd_frame->room[i] = RD_MAX_TRIS;
    }
#endif
    for (i = 0; i < RD_LISTS; i++) {
        rd_frame->ntris[i] = 0;
    }
    rd_frame->nstates = 0;
    rd_frame->nputs = 0;
    rd_frame->ngroups = 0;
}

RD_FOG rd_fog;

void rd_background_plane(const RD_VTX* v) {
    memcpy(rd_frame->background, v, sizeof rd_frame->background);
#if defined(HOST_NATIVE64) || defined(RD_NATIVE_SYSTEM)
    /* The background is an infinite plane defined by these three reference
     * points. Shift its reference frame with the scene, without stretching
     * the original colour/depth gradients or leaving uncovered side bars. */
    float offset = rd_widescreen_offset();
    for (int i = 0; i < 3; ++i) rd_frame->background[i].x += offset;
#endif
}

void rd_background(uint32_t argb) {
    RD_VTX v[3];
    memset(v, 0, sizeof v);
    v[1].x = RD_WIDTH;
    v[2].y = RD_HEIGHT;
    v[0].base = v[1].base = v[2].base = argb;
    rd_background_plane(v);
}

/* the fog registers keep their values from frame to frame */
void rd_fog_table(const float* table) {
    int i;
    for (i = 0; i < 128; i++) {
        float f = table[i];
        rd_fog.table[i] = (float)(int)((f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f) * 255.0f + 0.5f) / 255.0f;
    }
}

void rd_fog_color(uint32_t argb) {
    rd_fog.color = argb;
}

void rd_fog_density(uint32_t reg) {
    rd_fog.density = (float)(reg >> 8 & 255) / 128.0f * ldexpf(1.0f, (int8_t)(reg & 255));
}

/* the frame's copy of s: the last one again when it is the same */
static __attribute__((section(MVC2_NL_HOT_SECTION))) int add_state(const RD_STATE* s) {
    if (rd_frame->nstates > 0 && rd_same_state(&rd_frame->states[rd_frame->nstates - 1], s)) {
        return rd_frame->nstates - 1;
    }
    if (rd_frame->nstates == RD_MAX_STATES) {
        return -1;
    }
    rd_frame->states[rd_frame->nstates] = *s;
#ifdef __3DS__
    rd_frame->seen[rd_frame->nstates] = 0;
#endif
    return rd_frame->nstates++;
}

static int add_tri(int list, int state, const RD_VTX* a, const RD_VTX* b, const RD_VTX* c, int share, int stays, int culled) {
    RD_TRI* t;
    int i = rd_frame->ntris[list];
    if (i >= rd_frame->room[list]) {
#ifdef __3DS__
        if (!grow_part(rd_frame, list)) {
            rd_frame->dropped++;
            return 0;
        }
#else
        return 0;
#endif
    }
    t = &rd_frame->tris[list][rd_frame->ntris[list]++];
    t->state = (uint16_t)state;
    t->share = (uint8_t)share;
    t->stays = (uint8_t)stays;
    t->v[0] = *a;
    t->v[1] = *b;
    t->v[2] = *c;
#if defined(HOST_NATIVE64) || defined(RD_NATIVE_SYSTEM)
    /* One translation for every submission path: models, world sprites,
     * HUD strips, system text and debug lines all retain their pixel scale.
     * Model culling separately admits the newly visible world at the sides. */
    float offset = rd_widescreen_offset();
    for (int corner = 0; corner < 3; ++corner) t->v[corner].x += offset;
#endif
#ifdef __3DS__
    if (rd_seen && !rd_frame->seen[state] && (list != RD_TRANS || !rd_unseen(a, b, c))) {
        rd_frame->seen[state] = 1;
    }
#endif
    (void)culled;   /* the 3DS finds it again with the triangle's info, as its lists are built */
    return 1;
}

int rd_discard;
#ifdef __3DS__
int rd_seen, rd_group_info;
#endif

/* the back ends' area, from the same corners in the same order */
static double area(const RD_VTX* a, const RD_VTX* b, const RD_VTX* c) {
    return ((double)b->x - a->x) * ((double)c->y - a->y) - ((double)b->y - a->y) * ((double)c->x - a->x);
}

void rd_strip(const RD_STATE* s, const RD_VTX* v, int n) {
    int st, i, added = 0;
    if (rd_discard || (st = add_state(s)) < 0) {
        return;
    }
    for (i = 2; i < n; i++) {
        /* one winding for the whole strip */
        const RD_VTX *a = i & 1 ? &v[i - 1] : &v[i - 2], *b = i & 1 ? &v[i - 2] : &v[i - 1];
        int stays = 1;
        if (s->cull != 0) {
            double d = area(a, b, &v[i]);
            /* every back end culls it by its winding (cull 2: area below 0, 3: above) */
            if (s->cull >= 2 && (s->cull == 2 ? d < 0 : d > 0)) {
                added = 0;
                continue;
            }
            stays = !(fabs(d) < RD_CULL_VAL);
        }
        added = add_tri(s->list, st, a, b, &v[i], !added ? 0 : i & 1 ? RD_SHARE(2, 1) : RD_SHARE(0, 2), stays,
                        s->cull != 0 && !stays);
    }
}

#ifdef __3DS__
/* a quad's triangle culled by its state, by its area as the back ends find it */
static int quad_culled(const RD_STATE* s, const RD_VTX* a, const RD_VTX* b, const RD_VTX* c) {
    double d;
    if (s->cull == 0) {
        return 0;
    }
    d = area(a, b, c);
    return fabs(d) < RD_CULL_VAL || (s->cull == 2 && d < 0) || (s->cull == 3 && d > 0);
}
#else
#define quad_culled(s, a, b, c) 0
#endif

void rd_quad(const RD_STATE* s, const RD_VTX* v) {
    int st, first;
    if (rd_discard || (st = add_state(s)) < 0) {
        return;
    }
    first = add_tri(s->list, st, &v[0], &v[1], &v[2], 0, s->cull == 0, quad_culled(s, &v[0], &v[1], &v[2]));
    add_tri(s->list, st, &v[0], &v[2], &v[3], first ? RD_SHARE(0, 2) : 0, s->cull == 0, quad_culled(s, &v[0], &v[2], &v[3]));
}

int rd_gpu_tl;
int rd_gpu_tl_kinds = 7, rd_gpu_tl_max = RD_GPU_GROUPS, rd_gpu_tl_trans;
float rd_gpu_tl_trans_deep;
uint32_t rd_gpu_tl_deep_left;
int rd_gpu_tl_two_pass;
uint32_t rd_gpu_tl_two_passed;
int rd_gpu_tl_nearclip;
uint32_t rd_gpu_tl_near_left;

__attribute__((section(MVC2_NL_HOT_SECTION))) int rd_gpu_put(const float m[16], float near_w) {
    RD_GPU_PUT* p;
    if (!rd_gpu_tl || rd_discard || rd_frame->nputs == RD_GPU_PUTS) {
        return -1;
    }
    p = &rd_frame->puts[rd_frame->nputs];
    memcpy(p->m, m, sizeof p->m);
    p->near_w = near_w;
    return rd_frame->nputs++;
}

/* in words: gcc calls memcpy for a struct over 64 bytes */
static void copy_words(void* to, const void* from, size_t n) {
    uint32_t w;
    size_t i;
    for (i = 0; i < n / 4; i++) {
        memcpy(&w, (const char*)from + 4 * i, 4);
        memcpy((char*)to + 4 * i, &w, 4);
    }
}

_Static_assert(sizeof(RD_GPU_GROUP) % 4 == 0, "copied in words");

/* drawn where its list's triangles sent so far end */
__attribute__((section(MVC2_NL_HOT_SECTION))) int rd_gpu_group(int put, const RD_STATE* s, const RD_GPU_GROUP* g) {
    RD_GPU_DRAW* d;
    int st, deep = s->list == RD_TRANS && rd_gpu_tl_trans_deep > 0.0f && !(g->wlo > 0.0f && g->whi <= g->wlo * rd_gpu_tl_trans_deep);
    /* a deep translucent group is sorted whole, at one depth: the triangles it passes through would go wrong,
       but for a two-sided shell, whose back faces are behind its front ones */
    if (deep && !(rd_gpu_tl_two_pass && g->two_sided)) {
        rd_gpu_tl_deep_left++;
        return 0;
    }
    if (rd_gpu_tl_nearclip && put >= 0 && put < rd_frame->nputs && !(g->wlo >= rd_frame->puts[put].near_w)) {
        rd_gpu_tl_near_left++;
        return 0;
    }
    if (put < 0 || put >= rd_frame->nputs || rd_frame->ngroups + 1 + deep > rd_gpu_tl_max || !(rd_gpu_tl_kinds >> g->kind & 1) ||
        (st = add_state(s)) < 0) {
        return 0;
    }
    d = &rd_frame->groups[rd_frame->ngroups++];
    copy_words(&d->g, g, sizeof *g);
    d->g.faces = 0;
    d->put = (uint16_t)put;
    d->state = (uint16_t)st;
#ifdef __3DS__
    rd_frame->seen[st] = 1;
#endif
    d->at = rd_frame->ntris[s->list];
    if (deep) {
        d->g.faces = 1;
        d->g.key = g->key_far;
        d[1] = d[0];
        d[1].g.faces = 2;
        d[1].g.key = g->key_near;
        rd_frame->ngroups++;
        rd_gpu_tl_two_passed++;
    }
#ifdef __3DS__
    if (rd_group_info) {
        int k;
        for (k = 0; k <= deep; k++) {
            RD_GPU_INFO q;
            q.wlo = g->wlo;
            q.whi = g->whi;
            q.key = !deep ? g->key : k == 0 ? g->key_far : g->key_near;
            q.near_w = rd_frame->puts[put].near_w;
            q.at = rd_frame->ntris[s->list];
            q.mesh = (int16_t)(g->mesh >= 0 && g->mesh <= 32767 ? g->mesh : -1);
            q.list = s->list;
            q.free = (uint8_t)rd_order_free(s);
            rd_frame->ginfo[d - rd_frame->groups + k] = q;
        }
    }
#endif
    return 1;
}

#ifndef __3DS__
/* only the 3DS's back end draws them */
int rd_gpu_mesh_make(const NL_MESH* m) {
    (void)m;
    return -1;
}

void rd_gpu_mesh_free(int id) {
    (void)id;
}

uint32_t rd_gpu_mesh_room(void) {
    return 0;
}
#endif
