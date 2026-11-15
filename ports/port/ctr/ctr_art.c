/*
 * ctr_art.c  The 3DS's own pictures (port/ctr/gfx): the version chooser, and the pillars beside the game
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <3ds.h>

#include "naomi_host.h"
#include "ctr_front.h"
#include "ctr_render.h"
#include "version_lz.h"
#include "left_lz.h"
#include "right_lz.h"

/* tex3ds's RGBA8 textures, padded to powers of two: rows of 8x8 tiles, each in Morton order, a texel
   R << 24 | G << 16 | B << 8 | A */
typedef struct {
    u32* px;
    int w;
} ART;

static int unpack(ART* a, const unsigned char* lz, unsigned size, int w, int h) {
    a->w = w;
    a->px = host_try_malloc((size_t)w * h * 4);
    if (a->px != NULL && !decompress(a->px, (size_t)w * h * 4, NULL, (void*)lz, size)) {
        free(a->px);
        a->px = NULL;
    }
    return a->px != NULL;
}

/* x, y's place in a tiled picture w wide */
static int tiled(int x, int y, int w) {
    int m = 0, b;
    for (b = 0; b < 3; b++) {
        m |= (x >> b & 1) << 2 * b | (y >> b & 1) << (2 * b + 1);
    }
    return ((y >> 3) * (w >> 3) + (x >> 3)) * 64 + m;
}

/* over black, and dimmed to dim / 256: 0xRRGGBB */
static u32 over_black(u32 t, int dim) {
    int a = (int)(t & 0xFF) * dim / 256;
    return ((t >> 24) * a / 255) << 16 | ((t >> 16 & 0xFF) * a / 255) << 8 | (t >> 8 & 0xFF) * a / 255;
}

enum { PANEL_H = 180, DIM = 90 };

/* the button at the panel's x, y: 0 NAOMI, 1 Dreamcast, -1 neither. Each is an arrow, with the cabinet or the
   console over its end */
static int button(int x, int y) {
    int t = abs(y - 107);
    if (y >= 79 && y <= 128 && x >= 12 && x <= 62) {
        return 0;
    }
    if (y >= 80 && y <= 116 && x >= 264 && x <= 310) {
        return 1;
    }
    if (y < 85 || y > 127) {
        return -1;
    }
    if (x >= 8 && x <= 160 - t * 4 / 5) {
        return 0;
    }
    if (x >= 160 + t * 4 / 5 && x <= 316 - abs(y - 104) * 3 / 4) {
        return 1;
    }
    return -1;
}

/* the panel at x, y, the button not chosen dimmed */
static u32 panel(const ART* v, int x, int y, int chosen) {
    int b = button(x, y);
    return over_black(v->px[tiled(x, y, v->w)], b >= 0 && b != chosen ? DIM : 256);
}

static void draw_bottom(const ART* v, int chosen) {
    u16* fb = (u16*)gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
    int x, y, y0 = (240 - PANEL_H) / 2;
    for (x = 0; x < 320; x++) {
        for (y = 0; y < 240; y++) {
            u32 c = y >= y0 && y < y0 + PANEL_H ? panel(v, x, y - y0, chosen) : 0;
            fb[x * 240 + 239 - y] = (u16)((c >> 8 & 0xF800) | (c >> 5 & 0x07E0) | (c >> 3 & 0x1F));
        }
    }
}

/* the panel between the pillars */
static void draw_top(const ART* v, const ART* pillar, int chosen) {
    u8* fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
    int x, y, y0 = (240 - PANEL_H) / 2;
    for (x = 0; x < 400; x++) {
        for (y = 0; y < 240; y++) {
            u8* p = fb + (x * 240 + 239 - y) * 3;
            u32 c = x < 40 ? over_black(pillar[0].px[tiled(x, y, pillar[0].w)], 256)
                  : x >= 360 ? over_black(pillar[1].px[tiled(x - 360, y, pillar[1].w)], 256)
                  : y >= y0 && y < y0 + PANEL_H ? panel(v, x - 40, y - y0, chosen) : 0;
            p[0] = (u8)c;
            p[1] = (u8)(c >> 8);
            p[2] = (u8)(c >> 16);
        }
    }
}

static int unpack_pillars(ART pillar[2]) {
    if (unpack(&pillar[0], left_lz, left_lz_size, 64, 256) && unpack(&pillar[1], right_lz, right_lz_size, 64, 256)) {
        return 1;
    }
    free(pillar[0].px);
    return 0;
}

/* the old prompt, when there is no memory for the pictures */
static int ask_text(const char* text) {
    consoleClear();
    printf("%s\n\nA: arcade   B: Dreamcast   START: quit\n", text);
    while (aptMainLoop()) {
        u32 down;
        hidScanInput();
        down = hidKeysDown();
        if (down & (KEY_A | KEY_B)) {
            return (down & KEY_A) ? 0 : 1;
        }
        if (down & KEY_START) {
            break;
        }
        gspWaitForVBlank();
    }
    return -1;
}

/* NAOMI or Dreamcast: a side touched, or left or right and A */
int ctr_ask(const char* text) {
    ART v, pillar[2];
    int chosen = 0, shown = -1, top_left = 0, touched = -1, result = -1;
    host_log("%s", text);
    if (!unpack(&v, version_lz, version_lz_size, 512, 256)) {
        return ask_text(text);
    }
    if (!unpack_pillars(pillar)) {
        free(v.px);
        return ask_text(text);
    }
    while (aptMainLoop()) {
        u32 down, held;
        hidScanInput();
        down = hidKeysDown();
        held = hidKeysHeld();
        if (down & KEY_START) {
            break;
        }
        if (down & KEY_A) {
            result = chosen;
            break;
        }
        if (down & (KEY_LEFT | KEY_RIGHT)) {
            chosen = (down & KEY_RIGHT) != 0;
        }
        if (held & KEY_TOUCH) {
            touchPosition t;
            hidTouchRead(&t);
            chosen = touched = t.px >= 160;
        } else if (touched >= 0) {
            result = touched;
            break;
        }
        if (chosen != shown) {
            draw_bottom(&v, chosen);
            shown = chosen;
            top_left = 2;   /* both of the top screen's buffers */
        }
        if (top_left > 0) {
            draw_top(&v, pillar, chosen);
            top_left--;
        }
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
    free(v.px);
    free(pillar[0].px);
    free(pillar[1].px);
    for (top_left = 0; top_left < 2; top_left++) {
        memset(gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL), 0, 400 * 240 * 3);
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
    consoleClear();
    return result;
}

void* ctr_art_unpack(const unsigned char* lz, unsigned size, size_t bytes) {
    void* px = host_try_malloc(bytes);
    if (px != NULL && !decompress(px, bytes, NULL, (void*)lz, size)) {
        free(px);
        px = NULL;
    }
    return px;
}

int ctr_art_texel(int x, int y, int w) {
    return tiled(x, y, w);
}

/* NAOMI or Dreamcast on the bottom screen alone, while the game waits: 0, 1, or -1 for neither (B, or a touch
   outside the panel) */
int ctr_ask_bottom(int current) {
    ART v;
    int chosen = current, shown = -1, touched = -2, result = -1;
    int y0 = (240 - PANEL_H) / 2;
    if (!unpack(&v, version_lz, version_lz_size, 512, 256)) {
        return -1;
    }
    while (aptMainLoop()) {
        u32 down;
        hidScanInput();
        down = hidKeysDown();
        if (down & KEY_B) {
            break;
        }
        if (down & KEY_A) {
            result = chosen;
            break;
        }
        if (down & (KEY_LEFT | KEY_RIGHT)) {
            chosen = (down & KEY_RIGHT) != 0;
        }
        if (hidKeysHeld() & KEY_TOUCH) {
            touchPosition t;
            hidTouchRead(&t);
            if (touched == -2) {
                touched = t.py >= y0 && t.py < y0 + PANEL_H ? 0 : -1;
            }
            if (touched >= 0) {
                chosen = touched = t.px >= 160;
            }
        } else if (touched != -2) {
            result = touched;
            break;
        }
        if (chosen != shown || ctr_bottom_lost) {
            ctr_bottom_lost = 0;
            draw_bottom(&v, chosen);
            GSPGPU_FlushDataCache(gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL), 320 * 240 * 2);
            shown = chosen;
        }
        gspWaitForVBlank();
        ctr_render_alive();
    }
    free(v.px);
    return result;
}

/* the pillars in the bars beside the game, as the render target has them: the screen's columns as its rows, scale
   to a column, in 8x8 tiles, R G B A; the left bar's rows, then the right's. NULL with pillars = off in mvc2.cfg
   (or MVC2_PILLARS=0), or when there is no memory */
u32* ctr_art_pillars(int target_w, int scale) {
    const char* off = getenv("MVC2_PILLARS") != NULL ? getenv("MVC2_PILLARS") : config_get("pillars");
    ART pillar[2];
    int bar = 40 * scale, side, r, c;
    u32* out;
    if ((off != NULL && (strcmp(off, "0") == 0 || strcmp(off, "off") == 0)) || !unpack_pillars(pillar)) {
        return NULL;
    }
    out = linearAlloc((size_t)2 * bar * target_w * 4);
    for (side = 0; side < 2 && out != NULL; side++) {
        for (r = 0; r < bar; r++) {
            for (c = 0; c < target_w; c++) {
                u32 rgb = over_black(pillar[side].px[tiled(r / scale, 239 - c / scale, pillar[side].w)], 256);
                out[side * bar * target_w + tiled(c, r, target_w)] = rgb << 8 | 0xFF;
            }
        }
    }
    if (out != NULL) {
        GSPGPU_FlushDataCache(out, (u32)(2 * bar * target_w * 4));
    }
    free(pillar[0].px);
    free(pillar[1].px);
    return out;
}
