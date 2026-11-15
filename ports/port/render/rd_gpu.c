/*
 * rd_gpu.c  The SDL_GPU back end. The translucent list is peeled a layer at a time, far to near, and
 * each fragment blended by the shader in rd_soft.c's integer sums.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "rd.h"
#include "rd_gpu.h"
#include "rd_gpu_shaders.h"
#include "rd_list.h"

#if defined(__GNUC__) && defined(__i386__)
#pragma GCC target("sse2", "fpmath=sse")
#endif

typedef struct {
    float x, y;
    float depth, q;         /* the depth buffer's value; 1/w (1 for the background) */
    float u, v;             /* u/w, v/w */
    float base[4];          /* a r g b, 0 to 255, over w */
    float offs[3];          /* r g b over w */
    float order;            /* in its list */
    float ab[4], cf[4];     /* corners a, b and c, and the edges' flags (rd_gpu.hlsl's inside) */
    float uvb[4];           /* a sprite's u and v from and to: its texels are read only between */
} GVTX;

/* GVTX's depth to offs */
#define PLANE_VALUES 11

/* rd_gpu.hlsl's Draw */
typedef struct {
    uint32_t flags;
    int32_t width, height, levels;
    int32_t palette;
    float mip_bias;
    int32_t mode;
    int32_t blend;
} DRAW;

typedef struct {
    float fog_table[128];
    float fog_color[4];     /* a r g b, 0 to 255 */
    float fog_density;
    uint32_t zero;          /* 0, unknown to the shader compiler, so it can't regroup the sums */
    float inv_scale;
    int32_t sprite_mode;
} FRAMEU;

enum {
    F_TEX = 1, F_PAL = 2, F_BILINEAR = 4, F_TEX_ALPHA = 8, F_USE_ALPHA = 16, F_OFFSET = 32,
    F_FOG_TABLE = 64, F_FOG_TABLE2 = 128, F_PUNCH = 256, F_SHADING = 512, F_UV = 2048
};

/* 8, though the shader reads 7: SDL's Direct3D 12 driver needs a table size that divides its heaps' */
#define BINDINGS 8

enum { P_BACKGROUND, P_OPAQUE, P_PUNCH, P_KEY, P_PEEL };

/* a run of triangles of one state */
typedef struct {
    int first, count, state;
} BATCH;

typedef struct {
    const RD_TEXTURE* t;
    uint32_t serial;
    uint32_t used;          /* the last frame that drew it */
    SDL_GPUTexture* g;
    int w, h, levels, pal;  /* g's */
} GTEX;

/* a texture's texels, to be copied from the staging buffer at 'at' */
typedef struct {
    SDL_GPUTexture* g;
    const RD_TEXTURE* t;
    uint32_t at;
} PENDING;

/* frames a texture goes undrawn before it is let go */
#define EVICT_FRAMES 600

static SDL_GPUDevice* dev;
static SDL_GPUShader *vs, *ps;
static SDL_GPUSampler* point;
static SDL_GPUTexture *frame_tex[2], *depth_tex, *key_q[2], *key_o[2], *pal_tex, *dummy_rgba, *dummy_r32;
static SDL_GPUGraphicsPipeline* pipes[5][8][8][2];
static SDL_GPUBuffer* vbuf;
static SDL_GPUTransferBuffer* vtrans;
static uint32_t vcap;
static GVTX* verts;         /* mapped, maybe write-combined: each vertex stored whole */
static BATCH* batches[RD_LISTS];
static int nbatches[RD_LISTS], maxbatches[RD_LISTS];
static GTEX* gtex;
static int ngtex, maxgtex;
static int* hash;           /* gtex's indices by texture, -1 empty */
static uint32_t frame_no;
static SDL_GPUTransferBuffer* tb;   /* rd_gpu_read's */
static uint32_t pal_serial = 0xFFFFFFFFu;
static int exact_edges;     /* rd_soft.c's own edges (rd_gpu.hlsl's EXACT_EDGES) */
static int layers_env;      /* MVC2_GPULAYERS */
static int fog_on;
static SDL_GPUTexture* state_tex[RD_MAX_STATES];

/* rd_gpu_view's: the frame drawn at scale times the game's size, then filtered to out_w by out_h (0: not) */
static int want_scale = 1, want_sprites, want_w, want_h;
static int scale = 1, out_w, out_h;     /* the targets as made */
static int sprite_mode;
static SDL_GPUTexture* out_tex;
static SDL_GPUShader *full_vs, *resolve_ps;
static SDL_GPUGraphicsPipeline* resolve_pipe;
static uint32_t tb_size;

static uint32_t argb16_to_rgba(int format, uint32_t p) {
    uint32_t a, r, g, b;
    if (format == RD_ARGB1555) {
        a = p & 0x8000 ? 255 : 0, r = (p >> 10 & 31) * 255 / 31, g = (p >> 5 & 31) * 255 / 31, b = (p & 31) * 255 / 31;
    } else if (format == RD_RGB565) {
        a = 255, r = (p >> 11) * 255 / 31, g = (p >> 5 & 63) * 255 / 63, b = (p & 31) * 255 / 31;
    } else {
        a = (p >> 12) * 17, r = (p >> 8 & 15) * 17, g = (p >> 4 & 15) * 17, b = (p & 15) * 17;
    }
    return a << 24 | b << 16 | g << 8 | r;
}

static uint32_t argb_to_rgba(uint32_t c) {
    return (c & 0xFF00FF00u) | (c >> 16 & 255) | (c & 255) << 16;
}

static SDL_GPUTexture* make_texture(SDL_GPUTextureFormat format, int w, int h, int levels, SDL_GPUTextureUsageFlags usage) {
    SDL_GPUTextureCreateInfo ci;
    memset(&ci, 0, sizeof ci);
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.format = format;
    ci.usage = usage;
    ci.width = (Uint32)w;
    ci.height = (Uint32)h;
    ci.layer_count_or_depth = 1;
    ci.num_levels = (Uint32)levels;
    ci.sample_count = SDL_GPU_SAMPLECOUNT_1;
    return SDL_CreateGPUTexture(dev, &ci);
}

/* the frame's texels to upload, all in one staging buffer: each level at a multiple of 512 bytes */
#define STAGE_ALIGN 512u
static PENDING* pending;
static int npending, maxpending;
static SDL_GPUTransferBuffer* stage;
static uint32_t stage_cap, stage_used;

static int texel_bytes(const RD_TEXTURE* t) {
    return t->format == RD_PAL4 || t->format == RD_PAL8 ? 1 : 4;
}

static uint32_t level_bytes(const RD_TEXTURE* t, int k) {
    int w = t->width >> k, h = t->height >> k;
    w = w < 1 ? 1 : w, h = h < 1 ? 1 : h;
    return ((uint32_t)(w * h * texel_bytes(t)) + STAGE_ALIGN - 1) & ~(STAGE_ALIGN - 1);
}

static void add_pending(SDL_GPUTexture* g, const RD_TEXTURE* t) {
    int k;
    if (npending == maxpending) {
        int n = maxpending ? maxpending * 2 : 256;
        PENDING* p = realloc(pending, (size_t)n * sizeof *p);
        if (p == NULL) {
            return;
        }
        pending = p, maxpending = n;
    }
    pending[npending].g = g, pending[npending].t = t, pending[npending].at = stage_used;
    npending++;
    for (k = 0; k <= t->levels; k++) {
        stage_used += level_bytes(t, k);
    }
}

/* the pending texels and the palette (pal: 1024 ARGB, or NULL) written to the staging buffer, then copied by
   copy into their textures */
static void upload_pending(SDL_GPUCopyPass* copy, const uint32_t* pal) {
    uint32_t pal_at = stage_used;
    uint8_t* p;
    int i, k;
    if (npending == 0 && pal == NULL) {
        return;
    }
    if (pal != NULL) {
        stage_used += 4096;
    }
    if (stage_used > stage_cap) {
        SDL_GPUTransferBufferCreateInfo ti = { SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, 0, 0 };
        if (stage != NULL) {
            SDL_ReleaseGPUTransferBuffer(dev, stage);
        }
        ti.size = stage_used + stage_used / 2;
        stage = SDL_CreateGPUTransferBuffer(dev, &ti);
        stage_cap = stage != NULL ? ti.size : 0;
    }
    p = stage != NULL ? SDL_MapGPUTransferBuffer(dev, stage, true) : NULL;
    if (p == NULL) {
        npending = 0, stage_used = 0;
        return;
    }
    for (i = 0; i < npending; i++) {
        const RD_TEXTURE* t = pending[i].t;
        uint32_t at = pending[i].at;
        for (k = 0; k <= t->levels; k++) {
            int w = t->width >> k, h = t->height >> k, n, j;
            const void* d = k == 0 ? t->texels : t->mip[k];
            w = w < 1 ? 1 : w, h = h < 1 ? 1 : h, n = w * h;
            if (texel_bytes(t) == 1) {
                memcpy(p + at, d, (size_t)n);
            } else {
                uint32_t* px = (uint32_t*)(p + at);
                for (j = 0; j < n; j++) {
                    px[j] = argb16_to_rgba(t->format, ((const uint16_t*)d)[j]);
                }
            }
            at += level_bytes(t, k);
        }
    }
    if (pal != NULL) {
        uint32_t* px = (uint32_t*)(p + pal_at);
        for (i = 0; i < 1024; i++) {
            px[i] = argb_to_rgba(pal[i]);
        }
    }
    SDL_UnmapGPUTransferBuffer(dev, stage);
    for (i = 0; i < npending; i++) {
        const RD_TEXTURE* t = pending[i].t;
        uint32_t at = pending[i].at;
        for (k = 0; k <= t->levels; k++) {
            SDL_GPUTextureTransferInfo src;
            SDL_GPUTextureRegion dst;
            int w = t->width >> k, h = t->height >> k;
            w = w < 1 ? 1 : w, h = h < 1 ? 1 : h;
            memset(&src, 0, sizeof src);
            src.transfer_buffer = stage, src.offset = at;
            memset(&dst, 0, sizeof dst);
            dst.texture = pending[i].g;
            dst.mip_level = (Uint32)k;
            dst.w = (Uint32)w, dst.h = (Uint32)h, dst.d = 1;
            /* cycled before its first level, as the GPU may still be reading the last frame's */
            SDL_UploadToGPUTexture(copy, &src, &dst, k == 0);
            at += level_bytes(t, k);
        }
    }
    if (pal != NULL) {
        SDL_GPUTextureTransferInfo src;
        SDL_GPUTextureRegion dst;
        memset(&src, 0, sizeof src);
        src.transfer_buffer = stage, src.offset = pal_at;
        memset(&dst, 0, sizeof dst);
        dst.texture = pal_tex;
        dst.w = 1024, dst.h = 1, dst.d = 1;
        SDL_UploadToGPUTexture(copy, &src, &dst, true);
    }
    npending = 0, stage_used = 0;
}

static uint32_t hash_of(const RD_TEXTURE* t) {
    return (uint32_t)((uintptr_t)t >> 4) * 2654435761u & (uint32_t)(2 * maxgtex - 1);
}

static void rehash(void) {
    int i;
    for (i = 0; i < 2 * maxgtex; i++) {
        hash[i] = -1;
    }
    for (i = 0; i < ngtex; i++) {
        uint32_t h = hash_of(gtex[i].t);
        while (hash[h] >= 0) {
            h = (h + 1) & (uint32_t)(2 * maxgtex - 1);
        }
        hash[h] = i;
    }
}

/* t's entry in the cache, a new one if it has none */
static GTEX* entry(const RD_TEXTURE* t) {
    uint32_t h;
    if (maxgtex > 0) {
        for (h = hash_of(t); hash[h] >= 0; h = (h + 1) & (uint32_t)(2 * maxgtex - 1)) {
            if (gtex[hash[h]].t == t) {
                return &gtex[hash[h]];
            }
        }
    }
    if (ngtex == maxgtex) {
        int n = maxgtex ? maxgtex * 2 : 256;
        GTEX* p = realloc(gtex, (size_t)n * sizeof *p);
        int* q = realloc(hash, (size_t)n * 2 * sizeof *q);
        if (p != NULL) {
            gtex = p;
        }
        if (q != NULL) {
            hash = q;
        }
        if (p == NULL || q == NULL) {
            return NULL;
        }
        maxgtex = n;
        rehash();
    }
    gtex[ngtex].t = t, gtex[ngtex].g = NULL;
    h = hash_of(t);
    while (hash[h] >= 0) {
        h = (h + 1) & (uint32_t)(2 * maxgtex - 1);
    }
    hash[h] = ngtex;
    return &gtex[ngtex++];
}

static void evict(void) {
    int i, n = 0;
    for (i = 0; i < ngtex; i++) {
        if (frame_no - gtex[i].used > EVICT_FRAMES) {
            SDL_ReleaseGPUTexture(dev, gtex[i].g);
        } else {
            gtex[n++] = gtex[i];
        }
    }
    if (n != ngtex) {
        ngtex = n;
        rehash();
    }
}

/* RGBA8, or R8 indices for the palette formats: kept while its size and format are, its texels queued again
   when they change */
static SDL_GPUTexture* texture(const RD_TEXTURE* t) {
    int pal = t->format == RD_PAL4 || t->format == RD_PAL8;
    GTEX* e = entry(t);
    if (e == NULL) {
        return NULL;
    }
    e->used = frame_no;
    if (e->serial == t->serial && e->g != NULL) {
        return e->g;
    }
    if (e->g != NULL && (e->w != t->width || e->h != t->height || e->levels != t->levels || e->pal != pal)) {
        SDL_ReleaseGPUTexture(dev, e->g);
        e->g = NULL;
    }
    if (e->g == NULL) {
        e->g = make_texture(pal ? SDL_GPU_TEXTUREFORMAT_R8_UNORM : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, t->width, t->height,
                            t->levels + 1, SDL_GPU_TEXTUREUSAGE_SAMPLER);
        e->w = t->width, e->h = t->height, e->levels = t->levels, e->pal = pal;
    }
    e->serial = t->serial;
    if (e->g != NULL) {
        add_pending(e->g, t);
    }
    return e->g;
}

static SDL_GPUBlendFactor factor(int f, int src) {
    switch (f) {
    case RD_ZERO: return SDL_GPU_BLENDFACTOR_ZERO;
    case RD_ONE: return SDL_GPU_BLENDFACTOR_ONE;
    case RD_OTHER: return src ? SDL_GPU_BLENDFACTOR_DST_COLOR : SDL_GPU_BLENDFACTOR_SRC_COLOR;
    case RD_INV_OTHER: return src ? SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR : SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
    case RD_SRC_ALPHA: return SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    case RD_INV_SRC_ALPHA: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    case RD_DST_ALPHA: return SDL_GPU_BLENDFACTOR_DST_ALPHA;
    default: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
    }
}

/* a and b: the depth compare and write (opaque) or the blend factors */
static SDL_GPUGraphicsPipeline* pipeline(int kind, int a, int b) {
    SDL_GPUGraphicsPipeline** slot = &pipes[kind][a & 7][b & 7][0];
    SDL_GPUGraphicsPipelineCreateInfo ci;
    SDL_GPUVertexBufferDescription vb;
    SDL_GPUVertexAttribute at[8];
    SDL_GPUColorTargetDescription ct;
    int i;
    static const Uint32 offsets[8] = { 0, 8, 16, 24, 40, 56, 72, 88 };
    static const SDL_GPUVertexElementFormat formats[8] = {
        SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
        SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
        SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4
    };
    if (*slot != NULL) {
        return *slot;
    }
    memset(&ci, 0, sizeof ci);
    memset(&vb, 0, sizeof vb);
    vb.slot = 0;
    vb.pitch = sizeof(GVTX);
    vb.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    for (i = 0; i < 8; i++) {
        at[i].location = (Uint32)i;
        at[i].buffer_slot = 0;
        at[i].format = formats[i];
        at[i].offset = offsets[i];
    }
    ci.vertex_shader = vs;
    ci.fragment_shader = ps;
    ci.vertex_input_state.vertex_buffer_descriptions = &vb;
    ci.vertex_input_state.num_vertex_buffers = 1;
    ci.vertex_input_state.vertex_attributes = at;
    ci.vertex_input_state.num_vertex_attributes = 8;
    ci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    ci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    ci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    ci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    ci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    ci.depth_stencil_state.enable_depth_test = true;
    memset(&ct, 0, sizeof ct);
    ct.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    switch (kind) {
    case P_BACKGROUND:
        ci.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
        ci.depth_stencil_state.enable_depth_write = true;
        break;
    case P_OPAQUE:
        ci.depth_stencil_state.compare_op = (SDL_GPUCompareOp)(a + 1);
        ci.depth_stencil_state.enable_depth_write = b != 0;
        break;
    case P_KEY:
        ct.format = SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
        ct.blend_state.enable_blend = true;
        ct.blend_state.src_color_blendfactor = ct.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        ct.blend_state.src_alpha_blendfactor = ct.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        ct.blend_state.color_blend_op = ct.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_MIN;
        ci.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;
        break;
    case P_PEEL:    /* blended by the shader, over the opaque depth */
        ci.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;
        break;
    default:        /* P_PUNCH: blended, over the opaque depth, and writing it */
        ct.blend_state.enable_blend = true;
        ct.blend_state.src_color_blendfactor = ct.blend_state.src_alpha_blendfactor = factor(a, 1);
        ct.blend_state.dst_color_blendfactor = ct.blend_state.dst_alpha_blendfactor = factor(b, 0);
        ct.blend_state.color_blend_op = ct.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        ci.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;
        ci.depth_stencil_state.enable_depth_write = true;
        break;
    }
    ci.target_info.color_target_descriptions = &ct;
    ci.target_info.num_color_targets = 1;
    ci.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    ci.target_info.has_depth_stencil_target = true;
    *slot = SDL_CreateGPUGraphicsPipeline(dev, &ci);
    if (*slot == NULL) {
        SDL_Log("rd_gpu: pipeline %d %d %d: %s", kind, a, b, SDL_GetError());
    }
    return *slot;
}

static SDL_GPUShader* shader(const unsigned char* code, size_t size, SDL_GPUShaderFormat format, const char* entry,
                             SDL_GPUShaderStage stage, int samplers, int uniforms) {
    SDL_GPUShaderCreateInfo ci;
    memset(&ci, 0, sizeof ci);
    ci.code = code;
    ci.code_size = size;
    ci.entrypoint = entry;
    ci.format = format;
    ci.stage = stage;
    ci.num_samplers = (Uint32)samplers;
    ci.num_uniform_buffers = (Uint32)uniforms;
    return SDL_CreateGPUShader(dev, &ci);
}

static SDL_GPUGraphicsPipeline* make_resolve_pipe(void) {
    SDL_GPUGraphicsPipelineCreateInfo ci;
    SDL_GPUColorTargetDescription ct;
    memset(&ci, 0, sizeof ci);
    memset(&ct, 0, sizeof ct);
    ci.vertex_shader = full_vs;
    ci.fragment_shader = resolve_ps;
    ci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    ci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    ci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    ci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    ct.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ci.target_info.color_target_descriptions = &ct;
    ci.target_info.num_color_targets = 1;
    return SDL_CreateGPUGraphicsPipeline(dev, &ci);
}

static void release(SDL_GPUTexture** t) {
    if (*t != NULL) {
        SDL_ReleaseGPUTexture(dev, *t);
        *t = NULL;
    }
}

/* the frame's targets at n times the game's size; 0 when one could not be made */
static int make_targets(int n) {
    SDL_GPUTextureUsageFlags key = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    int i, w = RD_WIDTH * n, h = RD_HEIGHT * n;
    for (i = 0; i < 2; i++) {
        release(&frame_tex[i]), release(&key_q[i]), release(&key_o[i]);
    }
    release(&depth_tex);
    for (i = 0; i < 2; i++) {
        frame_tex[i] = make_texture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, w, h, 1,
                                    SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET);
        key_q[i] = make_texture(SDL_GPU_TEXTUREFORMAT_R32_FLOAT, w, h, 1, key);
        key_o[i] = make_texture(SDL_GPU_TEXTUREFORMAT_R32_FLOAT, w, h, 1, key);
    }
    depth_tex = make_texture(SDL_GPU_TEXTUREFORMAT_D32_FLOAT, w, h, 1, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET);
    scale = n;
    return frame_tex[0] != NULL && frame_tex[1] != NULL && key_q[0] != NULL && key_q[1] != NULL && key_o[0] != NULL &&
           key_o[1] != NULL && depth_tex != NULL;
}

void rd_gpu_view(int resolution, int aa, int w, int h, int sprites) {
    int n = resolution > 0 ? resolution : h > 0 ? (h * (aa < 1 ? 1 : aa) + RD_HEIGHT - 1) / RD_HEIGHT : 1;
    want_scale = n < 1 ? 1 : n > RD_GPU_MAX_SCALE ? RD_GPU_MAX_SCALE : n;
    want_w = w, want_h = h;
    want_sprites = sprites;
}

/* the targets as rd_gpu_view wants them: a smaller frame when the larger can't be made */
static void fit_view(void) {
    int n = want_scale, w = 0, h = 0;
    if (n != scale) {
        while (!make_targets(n) && n > 1) {
            n--;
        }
        want_scale = scale;
    }
    if (want_w > 0 && want_h > 0 && (want_w < RD_WIDTH * scale || want_h < RD_HEIGHT * scale)) {
        w = want_w, h = want_h;
    }
    if (w != out_w || h != out_h) {
        release(&out_tex);
        if (w > 0 && resolve_pipe != NULL) {
            out_tex = make_texture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, w, h, 1,
                                   SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET);
        }
        out_w = out_tex != NULL ? w : 0, out_h = out_tex != NULL ? h : 0;
    }
    sprite_mode = scale > 1 ? want_sprites : 0;
}

void rd_gpu_frame_size(int* w, int* h) {
    *w = out_w > 0 ? out_w : RD_WIDTH * scale;
    *h = out_w > 0 ? out_h : RD_HEIGHT * scale;
}

bool rd_gpu_init(SDL_GPUDevice* device) {
    SDL_GPUSamplerCreateInfo si;
    const char* e = getenv("MVC2_GPULAYERS");
    const char* q = getenv("MVC2_RDQUALITY");
    const char* g = getenv("MVC2_GPUEDGES");
    SDL_GPUShaderStage vtx = SDL_GPU_SHADERSTAGE_VERTEX, frag = SDL_GPU_SHADERSTAGE_FRAGMENT;
    int n = want_scale;
    dev = device;
    layers_env = e != NULL && atoi(e) > 0 ? atoi(e) : 0;
    if (q != NULL) {
        rd_quality(q);
    }
    exact_edges = g == NULL || atoi(g) != 0;
    if (SDL_GetGPUShaderFormats(dev) & SDL_GPU_SHADERFORMAT_DXBC) {
        SDL_GPUShaderFormat f = SDL_GPU_SHADERFORMAT_DXBC;
        vs = shader(rd_gpu_vs_dxbc, sizeof rd_gpu_vs_dxbc, f, "vs_main", vtx, 0, 0);
        ps = exact_edges ? shader(rd_gpu_ps_exact_dxbc, sizeof rd_gpu_ps_exact_dxbc, f, "ps_main", frag, BINDINGS, 2)
                         : shader(rd_gpu_ps_dxbc, sizeof rd_gpu_ps_dxbc, f, "ps_main", frag, BINDINGS, 2);
        full_vs = shader(rd_gpu_full_vs_dxbc, sizeof rd_gpu_full_vs_dxbc, f, "vs_full", vtx, 0, 0);
        resolve_ps = shader(rd_gpu_resolve_ps_dxbc, sizeof rd_gpu_resolve_ps_dxbc, f, "ps_resolve", frag, 1, 1);
    } else if (SDL_GetGPUShaderFormats(dev) & SDL_GPU_SHADERFORMAT_SPIRV) {
        SDL_GPUShaderFormat f = SDL_GPU_SHADERFORMAT_SPIRV;
        vs = shader(rd_gpu_vs_spirv, sizeof rd_gpu_vs_spirv, f, "vs_main", vtx, 0, 0);
        ps = exact_edges ? shader(rd_gpu_ps_exact_spirv, sizeof rd_gpu_ps_exact_spirv, f, "ps_main", frag, BINDINGS, 2)
                         : shader(rd_gpu_ps_spirv, sizeof rd_gpu_ps_spirv, f, "ps_main", frag, BINDINGS, 2);
        full_vs = shader(rd_gpu_full_vs_spirv, sizeof rd_gpu_full_vs_spirv, f, "vs_full", vtx, 0, 0);
        resolve_ps = shader(rd_gpu_resolve_ps_spirv, sizeof rd_gpu_resolve_ps_spirv, f, "ps_resolve", frag, 1, 1);
    } else {
        SDL_Log("rd_gpu: the device takes neither DXBC nor SPIR-V");
        return false;
    }
    memset(&si, 0, sizeof si);
    si.min_filter = si.mag_filter = SDL_GPU_FILTER_NEAREST;
    si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    si.address_mode_u = si.address_mode_v = si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    point = SDL_CreateGPUSampler(dev, &si);
    if (full_vs != NULL && resolve_ps != NULL) {
        resolve_pipe = make_resolve_pipe();
    }
    while (!make_targets(n) && n > 1) {
        n--;
    }
    want_scale = scale;
    pal_tex = make_texture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, 1024, 1, 1, SDL_GPU_TEXTUREUSAGE_SAMPLER);
    dummy_rgba = make_texture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, 1, 1, 1, SDL_GPU_TEXTUREUSAGE_SAMPLER);
    dummy_r32 = make_texture(SDL_GPU_TEXTUREFORMAT_R32_FLOAT, 1, 1, 1, SDL_GPU_TEXTUREUSAGE_SAMPLER);
    if (vs == NULL || ps == NULL || point == NULL || frame_tex[1] == NULL || depth_tex == NULL || key_q[1] == NULL ||
        key_o[1] == NULL || pal_tex == NULL || dummy_r32 == NULL) {
        SDL_Log("rd_gpu: %s", SDL_GetError());
        return false;
    }
    return true;
}

void rd_gpu_quit(void) {
    SDL_GPUTexture** own[] = { &frame_tex[0], &frame_tex[1], &depth_tex, &key_q[0], &key_q[1], &key_o[0], &key_o[1],
                               &pal_tex, &dummy_rgba, &dummy_r32, &out_tex };
    SDL_GPUGraphicsPipeline** p = &pipes[0][0][0][0];
    size_t i;
    for (i = 0; i < (size_t)ngtex; i++) {
        SDL_ReleaseGPUTexture(dev, gtex[i].g);
    }
    for (i = 0; i < sizeof own / sizeof own[0]; i++) {
        release(own[i]);
    }
    if (resolve_pipe != NULL) {
        SDL_ReleaseGPUGraphicsPipeline(dev, resolve_pipe);
        resolve_pipe = NULL;
    }
    SDL_ReleaseGPUShader(dev, full_vs);
    SDL_ReleaseGPUShader(dev, resolve_ps);
    full_vs = resolve_ps = NULL;
    scale = 1, out_w = out_h = 0, tb_size = 0;
    for (i = 0; i < sizeof pipes / sizeof pipes[0][0][0][0]; i++) {
        if (p[i] != NULL) {
            SDL_ReleaseGPUGraphicsPipeline(dev, p[i]);
        }
    }
    if (vbuf != NULL) {
        SDL_ReleaseGPUBuffer(dev, vbuf);
        SDL_ReleaseGPUTransferBuffer(dev, vtrans);
    }
    if (tb != NULL) {
        SDL_ReleaseGPUTransferBuffer(dev, tb);
        tb = NULL;
    }
    if (stage != NULL) {
        SDL_ReleaseGPUTransferBuffer(dev, stage);
        stage = NULL;
    }
    free(pending);
    pending = NULL;
    npending = maxpending = 0, stage_cap = stage_used = 0;
    SDL_ReleaseGPUSampler(dev, point);
    SDL_ReleaseGPUShader(dev, vs);
    SDL_ReleaseGPUShader(dev, ps);
    free(gtex);
    free(hash);
    hash = NULL;
    for (i = 0; i < RD_LISTS; i++) {
        free(batches[i]);
        batches[i] = NULL;
        nbatches[i] = maxbatches[i] = 0;
    }
    memset(pipes, 0, sizeof pipes);
    gtex = NULL, ngtex = maxgtex = 0, vbuf = NULL, vtrans = NULL, vcap = 0, pal_serial = 0xFFFFFFFFu;
}

static int cull(const RD_STATE* s, const RD_VTX* a, const RD_VTX* b, const RD_VTX* c) {
    double area = ((double)b->x - a->x) * ((double)c->y - a->y) - ((double)b->y - a->y) * ((double)c->x - a->x);
    return area == 0.0 || !(area == area) || (s->cull != 0 && fabs(area) < 0.02) || (s->cull == 2 && area < 0) ||
           (s->cull == 3 && area > 0);
}

static void put_vertex(GVTX* g, const RD_VTX* v, const RD_VTX* col, const RD_STATE* s, float k, float order) {
    float q = v->z;
    g->x = v->x, g->y = v->y;
    g->depth = q * k, g->q = q;
    g->u = v->u * q, g->v = v->v * q;
    g->base[0] = (float)(col->base >> 24) * q, g->base[1] = (float)(col->base >> 16 & 255) * q;
    g->base[2] = (float)(col->base >> 8 & 255) * q, g->base[3] = (float)(col->base & 255) * q;
    if (s->offset) {
        g->offs[0] = (float)(col->offset >> 16 & 255) * q, g->offs[1] = (float)(col->offset >> 8 & 255) * q;
        g->offs[2] = (float)(col->offset & 255) * q;
    } else {
        g->offs[0] = g->offs[1] = g->offs[2] = 0;
    }
    g->order = order;
    memset(g->ab, 0, sizeof g->ab);
    memset(g->cf, 0, sizeof g->cf);
    memset(g->uvb, 0, sizeof g->uvb);
}

static void add_batch(int list, int first, int count, int state) {
    BATCH* b;
    if (nbatches[list] > 0 && batches[list][nbatches[list] - 1].state == state &&
        batches[list][nbatches[list] - 1].first + batches[list][nbatches[list] - 1].count == first) {
        batches[list][nbatches[list] - 1].count += count;
        return;
    }
    if (nbatches[list] == maxbatches[list]) {
        int n = maxbatches[list] ? maxbatches[list] * 2 : 1024;
        BATCH* p = realloc(batches[list], (size_t)n * sizeof *p);
        if (p == NULL) {
            return;
        }
        batches[list] = p, maxbatches[list] = n;
    }
    b = &batches[list][nbatches[list]++];
    b->first = first, b->count = count, b->state = state;
}

/* rd_soft.c's edge function */
static double edge(double ax, double ay, double bx, double by, double x, double y) {
    return (bx - ax) * (y - ay) - (by - ay) * (x - ax);
}

/* rd_soft.c's edge_owns */
static int edge_owns(const RD_VTX* a, const RD_VTX* b, double sign) {
    double dx = -((double)b->y - a->y) * sign, dy = ((double)b->x - a->x) * sign;
    return dx > 0 || (dx == 0 && dy > 0);
}

/* the exact edges' margin, in pixels: well over the GPU's 1/256 snapping */
#define EDGE_MARGIN (1.0 / 32)
#define MAX_CORNERS 10      /* a hexagon cut by the screen's 4 sides */
#define NEAR_CORNER 8192.0  /* corners within this of the screen's are snapped as finely */

static float min3(float a, float b, float c) {
    float m = a < b ? a : b;
    return m < c ? m : c;
}

static float max3(float a, float b, float c) {
    float m = a > b ? a : b;
    return m > c ? m : c;
}

/* the polygon cut to the side k * coord < k * at; how many corners are left */
static int clip(double* x, double* y, int n, int axis, double k, double at) {
    double ox[MAX_CORNERS], oy[MAX_CORNERS];
    int i, m = 0;
    memcpy(ox, x, n * sizeof *x), memcpy(oy, y, n * sizeof *y);
    for (i = 0; i < n; i++) {
        int j = (i + 1) % n;
        double di = k * ((axis ? oy[i] : ox[i]) - at), dj = k * ((axis ? oy[j] : ox[j]) - at);
        if (di <= 0) {
            x[m] = ox[i], y[m++] = oy[i];
        }
        if ((di < 0 && dj > 0) || (di > 0 && dj < 0)) {
            double f = di / (di - dj);
            x[m] = ox[i] + (ox[j] - ox[i]) * f, y[m++] = oy[i] + (oy[j] - oy[i]) * f;
        }
    }
    return m;
}

/* triangle t's vertices into out: g, or with the exact edges the triangle grown by EDGE_MARGIN for
   the shader to keep rd_soft.c's pixels; how many */
static int put_triangle(GVTX* out, const GVTX* g, const RD_TRI* t) {
    const RD_VTX *a = &t->v[0], *b = &t->v[1], *c = &t->v[2];
    double area = edge(a->x, a->y, b->x, b->y, c->x, c->y), sign = area > 0 ? 1.0 : -1.0, d = EDGE_MARGIN, inv = 1.0 / area;
    double px[MAX_CORNERS], py[MAX_CORNERS], ux[3], uy[3];
    float flags = (float)(edge_owns(b, c, sign) | edge_owns(c, a, sign) << 1 | edge_owns(a, b, sign) << 2 | (area > 0) << 3);
    GVTX v[MAX_CORNERS];
    int m = 0, n = 0, i, j, k, in = 1;
    if (!exact_edges) {
        out[0] = g[0], out[1] = g[1], out[2] = g[2];
        return 3;
    }
    {
        /* grown about its centre by 1 + s: edge i moves out s area / (3 |edge i|) */
        double cx = ((double)a->x + b->x + c->x) * (1.0 / 3), cy = ((double)a->y + b->y + c->y) * (1.0 / 3);
        double l0 = fabs((double)b->x - a->x) + fabs((double)b->y - a->y), l1 = fabs((double)c->x - b->x) + fabs((double)c->y - b->y);
        double l2 = fabs((double)a->x - c->x) + fabs((double)a->y - c->y), lmax = l0 > l1 ? l0 : l1;
        double s = 3 * d * (lmax > l2 ? lmax : l2) * fabs(inv);
        if (max3(a->x, b->x, c->x) <= 0 || min3(a->x, b->x, c->x) >= RD_WIDTH || max3(a->y, b->y, c->y) <= 0 ||
            min3(a->y, b->y, c->y) >= RD_HEIGHT) {
            return 0;
        }
        for (i = 0; i < 3; i++) {
            px[i] = cx + (1 + s) * (t->v[i].x - cx), py[i] = cy + (1 + s) * (t->v[i].y - cy);
            in &= fabs(px[i]) < NEAR_CORNER && fabs(py[i]) < NEAR_CORNER;
        }
        if (s <= 0.25 && in) {
            double mid[PLANE_VALUES];
            for (k = 0; k < PLANE_VALUES; k++) {
                mid[k] = ((double)(&g[0].depth)[k] + (&g[1].depth)[k] + (&g[2].depth)[k]) * (1.0 / 3);
            }
            for (i = 0; i < 3; i++) {
                float* f;
                v[0] = g[i];
                f = &v[0].depth;
                v[0].x = (float)px[i], v[0].y = (float)py[i];
                for (k = 0; k < PLANE_VALUES; k++) {
                    f[k] = (float)(mid[k] + (1 + s) * (f[k] - mid[k]));
                }
                for (j = 0; j < 2; j++) {
                    v[0].ab[2 * j] = t->v[j].x, v[0].ab[2 * j + 1] = t->v[j].y;
                }
                v[0].cf[0] = c->x, v[0].cf[1] = c->y, v[0].cf[2] = flags, v[0].cf[3] = g[0].cf[3];
                out[i] = v[0];
            }
            return 3;
        }
        in = 1;
    }
    /* a thin triangle, or one off the screen: each edge moved out a margin */
    for (i = 0; i < 3; i++) {
        /* outward normal sign * (uy, -ux) */
        const RD_VTX *p = &t->v[i], *q = &t->v[(i + 1) % 3];
        double ex = (double)q->x - p->x, ey = (double)q->y - p->y, r = 1.0 / sqrt(ex * ex + ey * ey);
        ux[i] = ex * r, uy[i] = ey * r;
    }
    for (i = 0; i < 3; i++) {
        /* where the moved edges cross, or at a sharp corner two points a margin past it */
        int e0 = (i + 2) % 3;
        double n0x = sign * uy[e0], n0y = -sign * ux[e0], n1x = sign * uy[i], n1y = -sign * ux[i];
        double cx = t->v[i].x, cy = t->v[i].y, k1 = 1 + n0x * n1x + n0y * n1y;
        if (k1 * (64.0 * 64.0 / 2) >= 1) {
            px[m] = cx + d * (n0x + n1x) / k1, py[m++] = cy + d * (n0y + n1y) / k1;
        } else {
            px[m] = cx + d * (n0x + ux[e0]), py[m++] = cy + d * (n0y + uy[e0]);
            px[m] = cx + d * (n1x - ux[i]), py[m++] = cy + d * (n1y - uy[i]);
        }
    }
    for (i = 0; i < m; i++) {
        in &= px[i] >= -1.0 && px[i] <= RD_WIDTH + 1.0 && py[i] >= -1.0 && py[i] <= RD_HEIGHT + 1.0;
    }
    if (!in) {
        m = clip(px, py, m, 0, -1, -1.0);
        m = clip(px, py, m, 0, 1, RD_WIDTH + 1.0);
        m = clip(px, py, m, 1, -1, -1.0);
        m = clip(px, py, m, 1, 1, RD_HEIGHT + 1.0);
    }
    for (i = 0; i < m; i++) {
        double w0 = edge(b->x, b->y, c->x, c->y, px[i], py[i]) * inv, w1 = edge(c->x, c->y, a->x, a->y, px[i], py[i]) * inv;
        double w2 = edge(a->x, a->y, b->x, b->y, px[i], py[i]) * inv;
        const float* f0 = &g[0].depth;
        const float* f1 = &g[1].depth;
        const float* f2 = &g[2].depth;
        float* f;
        v[i] = g[0];
        f = &v[i].depth;
        v[i].x = (float)px[i], v[i].y = (float)py[i];
        for (k = 0; k < PLANE_VALUES; k++) {
            f[k] = (float)(w0 * f0[k] + w1 * f1[k] + w2 * f2[k]);
        }
        for (j = 0; j < 2; j++) {
            v[i].ab[2 * j] = t->v[j].x, v[i].ab[2 * j + 1] = t->v[j].y;
        }
        v[i].cf[0] = c->x, v[i].cf[1] = c->y, v[i].cf[2] = flags, v[i].cf[3] = g[0].cf[3];
    }
    for (i = 0; i + 2 < m; i++) {
        out[n++] = v[0], out[n++] = v[i + 1], out[n++] = v[i + 2];
    }
    return n;
}

/* the background plane's value f at (x, y) */
static double plane(const RD_VTX* v, const double* f, double x, double y) {
    double area = ((double)v[1].x - v[0].x) * ((double)v[2].y - v[0].y) - ((double)v[1].y - v[0].y) * ((double)v[2].x - v[0].x);
    double w1, w2;
    if (area == 0.0) {
        return f[0];
    }
    w1 = ((x - v[0].x) * ((double)v[2].y - v[0].y) - (y - v[0].y) * ((double)v[2].x - v[0].x)) / area;
    w2 = (((double)v[1].x - v[0].x) * (y - v[0].y) - ((double)v[1].y - v[0].y) * (x - v[0].x)) / area;
    return f[0] + (f[1] - f[0]) * w1 + (f[2] - f[0]) * w2;
}

static int sprite(const RD_STATE* s, const RD_TRI* t) {
    return s->tex != NULL && t->v[0].z == t->v[1].z && t->v[1].z == t->v[2].z;
}

static float snap16(float x) {
    return fabsf(x) < 65536.0f ? floorf(x * 16.0f + 0.5f) * (1.0f / 16.0f) : x;
}

/* the sprites' corners, on a sixteenth of a game pixel: an open-addressed set, ~0 empty */
static uint64_t* corners;
static uint32_t corners_size;

static uint64_t corner_key(float x, float y) {
    uint32_t a, b;
    memcpy(&a, &x, 4), memcpy(&b, &y, 4);
    return (uint64_t)a << 32 | b;
}

static uint64_t* corner_slot(uint64_t k) {
    uint32_t i = (uint32_t)((k * 0x9E3779B97F4A7C15ull) >> 32) & (corners_size - 1);
    while (corners[i] != ~0ull && corners[i] != k) {
        i = (i + 1) & (corners_size - 1);
    }
    return &corners[i];
}

static void find_corners(void) {
    uint32_t want = 64, count = 0;
    int list, i, j;
    for (list = 0; list < RD_LISTS; list++) {
        count += 3 * (uint32_t)rd_frame->ntris[list];
    }
    while (want < 2 * count) {
        want *= 2;
    }
    if (want > corners_size) {
        free(corners);
        corners = malloc(want * sizeof *corners);
        corners_size = corners != NULL ? want : 0;
    }
    if (corners == NULL) {
        return;
    }
    memset(corners, 0xFF, corners_size * sizeof *corners);
    for (list = 0; list < RD_LISTS; list++) {
        for (i = 0; i < rd_frame->ntris[list]; i++) {
            const RD_TRI* t = &rd_frame->tris[list][i];
            const RD_STATE* s = &rd_frame->states[t->state];
            if (sprite(s, t) && !cull(s, &t->v[0], &t->v[1], &t->v[2])) {
                for (j = 0; j < 3; j++) {
                    uint64_t k = corner_key(snap16(t->v[j].x), snap16(t->v[j].y));
                    *corner_slot(k) = k;
                }
            }
        }
    }
}

static int is_corner(float x, float y) {
    return corners != NULL && *corner_slot(corner_key(x, y)) != ~0ull;
}

/* the background's 6 vertices, then each list's triangles; how many */
static int build(float* zscale) {
    int list, i, n = 6;
    float qmax = 0;
    int e;
    for (list = 0; list < RD_LISTS; list++) {
        for (i = 0; i < rd_frame->ntris[list]; i++) {
            const RD_TRI* t = &rd_frame->tris[list][i];
            int j;
            for (j = 0; j < 3; j++) {
                if (t->v[j].z > qmax && isfinite(t->v[j].z)) {
                    qmax = t->v[j].z;
                }
            }
        }
    }
    for (i = 0; i < 3; i++) {
        if (rd_frame->background[i].z > qmax && isfinite(rd_frame->background[i].z)) {
            qmax = rd_frame->background[i].z;
        }
    }
    frexpf(qmax > 0 ? qmax : 1.0f, &e);
    *zscale = ldexpf(1.0f, -e);          /* a power of two: 1/w scaled exactly below 1 */
    for (list = 0; list < RD_LISTS; list++) {
        n += rd_frame->ntris[list] * (exact_edges ? 3 * (MAX_CORNERS - 2) : 3);
    }
    if ((uint32_t)n > vcap) {
        SDL_GPUBufferCreateInfo bi = { SDL_GPU_BUFFERUSAGE_VERTEX, (Uint32)(n * sizeof(GVTX)), 0 };
        SDL_GPUTransferBufferCreateInfo ti = { SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, (Uint32)(n * sizeof(GVTX)), 0 };
        if (vbuf != NULL) {
            SDL_ReleaseGPUBuffer(dev, vbuf);
            SDL_ReleaseGPUTransferBuffer(dev, vtrans);
        }
        vbuf = SDL_CreateGPUBuffer(dev, &bi);
        vtrans = SDL_CreateGPUTransferBuffer(dev, &ti);
        vcap = (uint32_t)n;
    }
    verts = SDL_MapGPUTransferBuffer(dev, vtrans, true);
    {
        /* the background: 1/w = 1, so it is not divided */
        static const float cx[6] = { 0, RD_WIDTH, 0, RD_WIDTH, RD_WIDTH, 0 }, cy[6] = { 0, 0, RD_HEIGHT, 0, RD_HEIGHT, RD_HEIGHT };
        const RD_VTX* b = rd_frame->background;
        double f[3];
        int c, j;
        for (j = 0; j < 6; j++) {
            GVTX g;
            memset(&g, 0, sizeof g);
            g.x = cx[j], g.y = cy[j], g.q = 1.0f;
            g.cf[2] = 16;       /* every pixel */
            for (i = 0; i < 3; i++) {
                f[i] = b[i].z;
            }
            g.depth = (float)plane(b, f, cx[j], cy[j]) * *zscale;
            for (c = 0; c < 4; c++) {
                for (i = 0; i < 3; i++) {
                    f[i] = (double)(b[i].base >> (24 - 8 * c) & 255);
                }
                g.base[c] = (float)plane(b, f, cx[j], cy[j]);
            }
            verts[j] = g;
        }
    }
    n = 6;
    if (scale > 1) {
        find_corners();
    }
    for (list = 0; list < RD_LISTS; list++) {
        nbatches[list] = 0;
        for (i = 0; i < rd_frame->ntris[list]; i++) {
            const RD_TRI* t = &rd_frame->tris[list][i];
            const RD_STATE* s = &rd_frame->states[t->state];
            RD_TRI snapped;
            GVTX g[3];
            int j, k;
            if (cull(s, &t->v[0], &t->v[1], &t->v[2])) {
                continue;
            }
            if (scale > 1) {
                /* drawn larger: corners on a sixteenth of a game pixel, so neighbours a float's rounding apart meet,
                   and sprites' corners (and the corners they share) on whole ones, so sprites side by side meet as
                   they do at 640 by 480 */
                snapped = *t;
                for (j = 0; j < 3; j++) {
                    float x = snap16(t->v[j].x), y = snap16(t->v[j].y);
                    if (is_corner(x, y)) {
                        x = floorf(x + 0.5f), y = floorf(y + 0.5f);
                    }
                    snapped.v[j].x = x, snapped.v[j].y = y;
                }
                t = &snapped;
                if (cull(s, &t->v[0], &t->v[1], &t->v[2])) {
                    continue;
                }
            }
            for (j = 0; j < 3; j++) {
                put_vertex(&g[j], &t->v[j], s->flat ? &t->v[2] : &t->v[j], s, *zscale, (float)i);
            }
            /* a sprite: textured, its 1/w the same at each corner. Half a quad has the quad's u and v, from and to */
            if (sprite(s, t)) {
                float b[4];
                b[0] = fminf(t->v[0].u, fminf(t->v[1].u, t->v[2].u)), b[1] = fminf(t->v[0].v, fminf(t->v[1].v, t->v[2].v));
                b[2] = fmaxf(t->v[0].u, fmaxf(t->v[1].u, t->v[2].u)), b[3] = fmaxf(t->v[0].v, fmaxf(t->v[1].v, t->v[2].v));
                for (j = 0; j < 3; j++) {
                    g[j].cf[3] = 1.0f;
                    memcpy(g[j].uvb, b, sizeof b);
                }
            }
            k = put_triangle(&verts[n], g, t);
            if (k > 0) {
                add_batch(list, n, k, t->state);
                n += k;
            }
        }
    }
    SDL_UnmapGPUTransferBuffer(dev, vtrans);
    return n;
}

static void draw_values(DRAW* d, const RD_STATE* s, int list, int mode) {
    static const float bias[16] = { 0, -4, -2, -1 };
    memset(d, 0, sizeof *d);
    d->mode = mode;
    d->flags = (s->use_alpha ? F_USE_ALPHA : 0) | (s->offset ? F_OFFSET : 0) | (list == RD_PUNCH ? F_PUNCH : 0);
    if (s->fog == RD_FOG_TABLE && fog_on) {
        d->flags |= F_FOG_TABLE;
    } else if (s->fog == RD_FOG_TABLE2) {
        d->flags |= F_FOG_TABLE2;
    }
    if (s->tex != NULL) {
        d->flags |= F_TEX | (s->bilinear ? F_BILINEAR : 0) | (s->tex_alpha ? F_TEX_ALPHA : 0) | (uint32_t)s->shading * F_SHADING |
                    (uint32_t)(s->uv_mode & 15) * F_UV;
        d->flags |= s->tex->format == RD_PAL4 || s->tex->format == RD_PAL8 ? F_PAL : 0;
        d->width = s->tex->width, d->height = s->tex->height, d->levels = s->tex->levels;
        d->palette = s->palette;
        d->mip_bias = bias[s->mip_d & 15];
    }
    d->blend = s->src | s->dst << 4;
}

static SDL_GPUTexture* bound_tex[BINDINGS];

/* the texture, the palette and the peeling's (t2-t6), when they change */
static void bind_textures(SDL_GPURenderPass* rp, SDL_GPUTexture* tex, SDL_GPUTexture* const* keys) {
    SDL_GPUTextureSamplerBinding tb[BINDINGS];
    int j, same = 1;
    for (j = 0; j < BINDINGS; j++) {
        tb[j].sampler = point;
        tb[j].texture = j == 0 || j == 6 ? dummy_rgba : dummy_r32;
        if (j >= 2 && j < 7 && keys != NULL && keys[j - 2] != NULL) {
            tb[j].texture = keys[j - 2];
        }
    }
    if (tex != NULL) {
        tb[0].texture = tex;
    }
    tb[1].texture = pal_tex;
    for (j = 0; j < BINDINGS; j++) {
        same &= tb[j].texture == bound_tex[j];
        bound_tex[j] = tb[j].texture;
    }
    if (!same) {
        SDL_BindGPUFragmentSamplers(rp, 0, tb, BINDINGS);
    }
}

/* a layer's keys, in one draw */
static void draw_keys(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* rp, int mode, SDL_GPUTexture* const* keys) {
    const BATCH* first = &batches[RD_TRANS][0];
    const BATCH* last = &batches[RD_TRANS][nbatches[RD_TRANS] - 1];
    SDL_GPUGraphicsPipeline* p = pipeline(P_KEY, 0, 0);
    DRAW d;
    if (p == NULL) {
        return;
    }
    SDL_BindGPUGraphicsPipeline(rp, p);
    bind_textures(rp, NULL, keys);
    memset(&d, 0, sizeof d);
    d.mode = mode;
    SDL_PushGPUFragmentUniformData(cmd, 0, &d, sizeof d);
    SDL_DrawGPUPrimitives(rp, (Uint32)(last->first + last->count - first->first), 1, (Uint32)first->first, 0);
}

static void draw_list(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* rp, int list, int kind, int mode,
                      SDL_GPUTexture* const* keys) {
    int i;
    SDL_GPUGraphicsPipeline* bound = NULL;
    for (i = 0; i < nbatches[list]; i++) {
        const BATCH* b = &batches[list][i];
        const RD_STATE* s = &rd_frame->states[b->state];
        SDL_GPUGraphicsPipeline* p;
        DRAW d;
        if (kind == P_OPAQUE) {
            p = pipeline(P_OPAQUE, s->depth, s->zwrite);
        } else {
            p = pipeline(kind, kind == P_PUNCH ? s->src : 0, kind == P_PUNCH ? s->dst : 0);
        }
        if (p == NULL) {
            continue;
        }
        if (p != bound) {
            SDL_BindGPUGraphicsPipeline(rp, p);
            bound = p;
        }
        bind_textures(rp, state_tex[b->state], keys);
        draw_values(&d, s, list, mode);
        SDL_PushGPUFragmentUniformData(cmd, 0, &d, sizeof d);
        SDL_DrawGPUPrimitives(rp, (Uint32)b->count, 1, (Uint32)b->first, 0);
    }
}

static SDL_GPURenderPass* begin(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* color, int clear, float value, int clear_depth) {
    SDL_GPUColorTargetInfo ct;
    SDL_GPUDepthStencilTargetInfo dt;
    SDL_GPUBufferBinding vb;
    SDL_GPURenderPass* rp;
    memset(&ct, 0, sizeof ct);
    ct.texture = color;
    ct.load_op = clear ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    ct.clear_color.r = ct.clear_color.g = ct.clear_color.b = ct.clear_color.a = value;
    memset(&dt, 0, sizeof dt);
    dt.texture = depth_tex;
    dt.load_op = clear_depth ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
    dt.store_op = SDL_GPU_STOREOP_STORE;
    dt.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    dt.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    dt.clear_depth = 0.0f;
    rp = SDL_BeginGPURenderPass(cmd, &ct, 1, &dt);
    memset(bound_tex, 0, sizeof bound_tex);
    vb.buffer = vbuf;
    vb.offset = 0;
    SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
    return rp;
}

/* the frame, filtered to out_tex when the window is smaller */
static SDL_GPUTexture* finish(SDL_GPUCommandBuffer* cmd) {
    SDL_GPUColorTargetInfo ct;
    SDL_GPUTextureSamplerBinding tb;
    SDL_GPURenderPass* rp;
    float ratio[4];
    if (out_tex == NULL) {
        return frame_tex[0];
    }
    memset(&ct, 0, sizeof ct);
    ct.texture = out_tex;
    ct.load_op = SDL_GPU_LOADOP_DONT_CARE;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    rp = SDL_BeginGPURenderPass(cmd, &ct, 1, NULL);
    if (rp == NULL) {
        return frame_tex[0];
    }
    ratio[0] = (float)(RD_WIDTH * scale) / (float)out_w, ratio[1] = (float)(RD_HEIGHT * scale) / (float)out_h;
    ratio[2] = ratio[3] = 0;
    tb.texture = frame_tex[0], tb.sampler = point;
    SDL_BindGPUGraphicsPipeline(rp, resolve_pipe);
    SDL_BindGPUFragmentSamplers(rp, 0, &tb, 1);
    SDL_PushGPUFragmentUniformData(cmd, 0, ratio, sizeof ratio);
    SDL_DrawGPUPrimitives(rp, 3, 1, 0, 0);
    SDL_EndGPURenderPass(rp);
    return out_tex;
}

SDL_GPUTexture* rd_gpu_end_frame(SDL_GPUCommandBuffer* cmd) {
    SDL_GPUCopyPass* copy;
    SDL_GPURenderPass* rp;
    FRAMEU fu;
    float zscale;
    int n, i, layer, f = 0;
    /* the translucent layers peeled */
    int layers = layers_env ? layers_env : rd_q.nosort ? 4 : 12;
    frame_no++;
    if (frame_no % 60 == 0) {
        evict();
    }
    fit_view();
    n = build(&zscale);
    for (i = 0; i < rd_frame->nstates; i++) {
        const RD_TEXTURE* t = rd_frame->states[i].tex;
        state_tex[i] = t == NULL ? NULL : i > 0 && rd_frame->states[i - 1].tex == t ? state_tex[i - 1] : texture(t);
    }
    copy = SDL_BeginGPUCopyPass(cmd);
    {
        SDL_GPUTransferBufferLocation src = { vtrans, 0 };
        SDL_GPUBufferRegion dst = { vbuf, 0, (Uint32)(n * sizeof(GVTX)) };
        SDL_UploadToGPUBuffer(copy, &src, &dst, true);
    }
    fog_on = 0;
    for (i = 0; i < 128; i++) {
        fog_on |= rd_fog.table[i] != 0.0f;
    }
    upload_pending(copy, rd_palette_serial != pal_serial ? rd_palette : NULL);
    pal_serial = rd_palette_serial;
    SDL_EndGPUCopyPass(copy);

    memset(&fu, 0, sizeof fu);
    memcpy(fu.fog_table, rd_fog.table, sizeof fu.fog_table);
    fu.fog_color[0] = (float)(rd_fog.color >> 24), fu.fog_color[1] = (float)(rd_fog.color >> 16 & 255);
    fu.fog_color[2] = (float)(rd_fog.color >> 8 & 255), fu.fog_color[3] = (float)(rd_fog.color & 255);
    fu.fog_density = rd_fog.density;
    fu.inv_scale = 1.0f / (float)scale;
    fu.sprite_mode = sprite_mode;
    SDL_PushGPUFragmentUniformData(cmd, 1, &fu, sizeof fu);

    rp = begin(cmd, frame_tex[0], 1, 0.0f, 1);
    {
        SDL_GPUGraphicsPipeline* p = pipeline(P_BACKGROUND, 0, 0);
        DRAW d;
        memset(&d, 0, sizeof d);
        d.flags = F_USE_ALPHA;
        if (p != NULL) {
            SDL_BindGPUGraphicsPipeline(rp, p);
            bind_textures(rp, NULL, NULL);
            SDL_PushGPUFragmentUniformData(cmd, 0, &d, sizeof d);
            SDL_DrawGPUPrimitives(rp, 6, 1, 0, 0);
        }
    }
    draw_list(cmd, rp, RD_OPAQUE, P_OPAQUE, 0, NULL);
    draw_list(cmd, rp, RD_PUNCH, P_PUNCH, 0, NULL);
    SDL_EndGPURenderPass(rp);

    if (nbatches[RD_TRANS] == 0) {
        return finish(cmd);
    }
    /* the translucent list, far to near */
    {
        SDL_GPUColorTargetInfo ct[2];
        memset(ct, 0, sizeof ct);
        for (i = 0; i < 2; i++) {
            ct[i].texture = i == 0 ? key_q[1] : key_o[1];
            ct[i].load_op = SDL_GPU_LOADOP_CLEAR;
            ct[i].store_op = SDL_GPU_STOREOP_STORE;
            ct[i].clear_color.r = -3.0e38f;
        }
        SDL_EndGPURenderPass(SDL_BeginGPURenderPass(cmd, ct, 2, NULL));
    }
    for (layer = 0; layer < layers; layer++) {
        int cur = layer & 1, last = cur ^ 1;
        SDL_GPUTexture* keys[5] = { NULL };
        /* the nearest 1/w past the last layer */
        keys[0] = key_q[last], keys[1] = key_o[last];
        rp = begin(cmd, key_q[cur], 1, 3.0e38f, 0);
        draw_keys(cmd, rp, 1, keys);
        SDL_EndGPURenderPass(rp);
        /* the first triangle at it */
        keys[2] = key_q[cur];
        rp = begin(cmd, key_o[cur], 1, 3.0e38f, 0);
        draw_keys(cmd, rp, 2, keys);
        SDL_EndGPURenderPass(rp);
        /* its fragment, blended by the shader over a copy of the frame */
        copy = SDL_BeginGPUCopyPass(cmd);
        {
            SDL_GPUTextureLocation from, to;
            memset(&from, 0, sizeof from);
            memset(&to, 0, sizeof to);
            from.texture = frame_tex[f], to.texture = frame_tex[f ^ 1];
            SDL_CopyGPUTextureToTexture(copy, &from, &to, (Uint32)(RD_WIDTH * scale), (Uint32)(RD_HEIGHT * scale), 1, false);
        }
        SDL_EndGPUCopyPass(copy);
        keys[0] = keys[1] = NULL, keys[3] = key_o[cur], keys[4] = frame_tex[f];
        f ^= 1;
        rp = begin(cmd, frame_tex[f], 0, 0.0f, 0);
        draw_list(cmd, rp, RD_TRANS, P_PEEL, 3, keys);
        SDL_EndGPURenderPass(rp);
    }
    if (f != 0) {
        /* always the same texture, for the caller */
        SDL_GPUTextureLocation from, to;
        copy = SDL_BeginGPUCopyPass(cmd);
        memset(&from, 0, sizeof from);
        memset(&to, 0, sizeof to);
        from.texture = frame_tex[1], to.texture = frame_tex[0];
        SDL_CopyGPUTextureToTexture(copy, &from, &to, (Uint32)(RD_WIDTH * scale), (Uint32)(RD_HEIGHT * scale), 1, false);
        SDL_EndGPUCopyPass(copy);
    }
    return finish(cmd);
}

/* t's w by h pixels, RGBA, mapped from the transfer buffer: waits for the GPU. NULL when it fails */
static const uint8_t* download(SDL_GPUTexture* t, int w, int h) {
    SDL_GPUCommandBuffer* cmd;
    SDL_GPUCopyPass* copy;
    SDL_GPUTextureRegion src;
    SDL_GPUTextureTransferInfo dst;
    SDL_GPUFence* fence;
    uint32_t size = (uint32_t)(w * h * 4);
    if (tb != NULL && tb_size < size) {
        SDL_ReleaseGPUTransferBuffer(dev, tb);
        tb = NULL;
    }
    if (tb == NULL) {
        SDL_GPUTransferBufferCreateInfo ti = { SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, size, 0 };
        tb = SDL_CreateGPUTransferBuffer(dev, &ti);
        tb_size = tb != NULL ? size : 0;
    }
    cmd = tb != NULL ? SDL_AcquireGPUCommandBuffer(dev) : NULL;
    if (cmd == NULL) {
        return NULL;
    }
    copy = SDL_BeginGPUCopyPass(cmd);
    memset(&src, 0, sizeof src);
    src.texture = t;
    src.w = (Uint32)w, src.h = (Uint32)h, src.d = 1;
    memset(&dst, 0, sizeof dst);
    dst.transfer_buffer = tb;
    SDL_DownloadFromGPUTexture(copy, &src, &dst);
    SDL_EndGPUCopyPass(copy);
    fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence == NULL) {
        return NULL;
    }
    SDL_WaitForGPUFences(dev, true, &fence, 1);
    SDL_ReleaseGPUFence(dev, fence);
    return SDL_MapGPUTransferBuffer(dev, tb, false);
}

/* the last frame at the game's size, each pixel the average of those drawn for it: slow, for screenshots */
bool rd_gpu_read(uint32_t* argb) {
    const uint8_t* p = download(frame_tex[0], RD_WIDTH * scale, RD_HEIGHT * scale);
    int x, y, i, j, c, n = scale * scale, pitch = RD_WIDTH * scale * 4;
    if (p == NULL) {
        return false;
    }
    for (y = 0; y < RD_HEIGHT; y++) {
        for (x = 0; x < RD_WIDTH; x++) {
            uint32_t sum[4] = { 0, 0, 0, 0 };
            for (j = 0; j < scale; j++) {
                const uint8_t* q = p + (size_t)(y * scale + j) * pitch + (size_t)x * scale * 4;
                for (i = 0; i < scale * 4; i++) {
                    sum[i & 3] += q[i];
                }
            }
            for (c = 0; c < 4; c++) {
                sum[c] = (sum[c] + n / 2) / n;
            }
            argb[y * RD_WIDTH + x] = sum[3] << 24 | sum[0] << 16 | sum[1] << 8 | sum[2];
        }
    }
    SDL_UnmapGPUTransferBuffer(dev, tb);
    return true;
}

bool rd_gpu_read_view(uint32_t* argb) {
    int w, h, i;
    const uint8_t* p;
    rd_gpu_frame_size(&w, &h);
    p = download(out_tex != NULL ? out_tex : frame_tex[0], w, h);
    if (p == NULL) {
        return false;
    }
    for (i = 0; i < w * h; i++) {
        argb[i] = (uint32_t)p[4 * i + 3] << 24 | (uint32_t)p[4 * i] << 16 | (uint32_t)p[4 * i + 1] << 8 | p[4 * i + 2];
    }
    SDL_UnmapGPUTransferBuffer(dev, tb);
    return true;
}
