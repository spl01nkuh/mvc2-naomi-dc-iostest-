/* Native counterparts of the game's Kamui configuration and system font.
 * The CPU renderer owns typed triangle pools, so SH-4 DMA buffer addresses
 * are kept as configuration rather than dereferenced as command streams. */
#ifndef HOST_NATIVE64
#error sdk_graphics.c requires the native game ABI
#endif

#include "types.h"
#include "host_assets.h"
typedef struct VEC3 VEC3;
typedef struct VEC4 VEC4;
typedef struct MATRIX MATRIX;
#include "nl.h"
#include "naomi_host.h"
#include "nl_port.h"
#include "rd_list.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct {
    uint32_t texture_bytes;
    uint32_t vertex_bytes;
    uint32_t punch_words;
    void* vertex_base;
    int latency;
    int direct_list;
    int render_timeout, vertex_timeout;
    int auto_sort;
    uint32_t border_color;
} graphics = { 16u << 20, 0, 0, NULL, 3, 0, 10, 10, 1, 0xff000000u };

void nlSysSetTextureSize(int words) {
    graphics.texture_bytes = words <= 0 ? 0 :
        (uint32_t)words > UINT32_MAX / 4u ? UINT32_MAX : (uint32_t)words * 4u;
}

Uint32 nlCommonTextureGetFreeMem(void) {
    uint64_t used = 0;
    for (int i = 0; i < NL_MAX_TEX; ++i)
        if (nl_tex[i].loaded) used += nl_tex[i].bytes;
    return used >= graphics.texture_bytes ? 0 : graphics.texture_bytes - (uint32_t)used;
}

void nlSysUseLatency2V(void) {
    graphics.latency = 2;
}

void nlSysSetDirectListTypeLatency2V(int list) {
    graphics.direct_list = list == 2 || list == 4 ? list : 0;
}

void nlSysSetVertexBufferBaseAddress(void* address) {
    graphics.vertex_base = address;
}

void nlSysCreatePunchBuffer(int words) {
    graphics.punch_words = words > 0 ? (uint32_t)words : 0;
}

int nlCreateVertexBuffer(int opaque, int opaque_modifier, int translucent, int translucent_modifier) {
    if (opaque < 0 || opaque_modifier < 0 || translucent < 0 || translucent_modifier < 0)
        return -1;
    uint64_t words = (uint64_t)opaque + (uint64_t)opaque_modifier +
        (uint64_t)translucent + (uint64_t)translucent_modifier + graphics.punch_words;
    if (graphics.latency == 3) words *= 2;
    if (words > UINT32_MAX / 4u) return -1;
    graphics.vertex_bytes = (uint32_t)words * 4u;
    /* rd_begin_frame initializes the real renderer's list counters and state
     * tables. Its native capacity is independent of Kamui's word budgets. */
    rd_begin_frame();
    nlSetWindowSize(0.0f, 0.0f, RD_WIDTH, RD_HEIGHT);
    host_log("native graphics: %u Kamui vertex bytes at %p, latency %d, direct list %d; %d triangles/list",
        graphics.vertex_bytes, graphics.vertex_base, graphics.latency,
        graphics.direct_list, RD_MAX_TRIS);
    return 0;
}

Uint32 nlSysGetVertexBufferSize(void) {
    return graphics.vertex_bytes;
}

void nlSysSetTimeOutCount(int render, int vertex) {
    /* Native rendering is synchronous at presentation. Preserve the SDK's
     * diagnostic thresholds; there are no hardware DMA timeout registers. */
    graphics.render_timeout = render;
    graphics.vertex_timeout = vertex;
}

Sint32 nlSysMetricsGetRenderTimeOutCount(void) {
    return graphics.render_timeout;
}

Sint32 nlSysMetricsGetVertexTimeOutCount(void) {
    return graphics.vertex_timeout;
}

void nlSysSetAutoSortMode(Sint32 enable) {
    /* The reconstructed SDK's function also leaves hardware sorting intact.
     * The native renderer keeps its PowerVR translucent ordering enabled. */
    graphics.auto_sort = enable;
}

void nlSetBorderColor(Uint32 color) {
    graphics.border_color = color | 0xff000000u;
}

uint32_t mvc2_native_border_color(void) {
    return graphics.border_color;
}

/* A compact native system font. Rows use the low five bits, left to right.
 * Lowercase has the same readable shapes as uppercase; non-ASCII bytes use
 * '?'. This is host-owned diagnostic artwork, not a ROM font or asset. */
static const uint8_t font_rows[64][7] = {
    ['!' - 32] = {4,4,4,4,4,0,4},
    ['"' - 32] = {10,10,10,0,0,0,0},
    ['#' - 32] = {10,31,10,10,31,10,0},
    ['$' - 32] = {4,15,20,14,5,30,4},
    ['%' - 32] = {24,25,2,4,8,19,3},
    ['&' - 32] = {12,18,20,8,21,18,13},
    ['\'' - 32] = {4,4,8,0,0,0,0},
    ['(' - 32] = {2,4,8,8,8,4,2},
    [')' - 32] = {8,4,2,2,2,4,8},
    ['*' - 32] = {0,21,14,31,14,21,0},
    ['+' - 32] = {0,4,4,31,4,4,0},
    [',' - 32] = {0,0,0,0,4,4,8},
    ['-' - 32] = {0,0,0,31,0,0,0},
    ['.' - 32] = {0,0,0,0,0,4,4},
    ['/' - 32] = {1,2,2,4,8,8,16},
    ['0' - 32] = {14,17,19,21,25,17,14},
    ['1' - 32] = {4,12,4,4,4,4,14},
    ['2' - 32] = {14,17,1,2,4,8,31},
    ['3' - 32] = {30,1,1,14,1,1,30},
    ['4' - 32] = {2,6,10,18,31,2,2},
    ['5' - 32] = {31,16,16,30,1,1,30},
    ['6' - 32] = {6,8,16,30,17,17,14},
    ['7' - 32] = {31,1,2,4,8,8,8},
    ['8' - 32] = {14,17,17,14,17,17,14},
    ['9' - 32] = {14,17,17,15,1,2,12},
    [':' - 32] = {0,4,4,0,4,4,0},
    [';' - 32] = {0,4,4,0,4,4,8},
    ['<' - 32] = {1,2,4,8,4,2,1},
    ['=' - 32] = {0,0,31,0,31,0,0},
    ['>' - 32] = {16,8,4,2,4,8,16},
    ['?' - 32] = {14,17,1,2,4,0,4},
    ['@' - 32] = {14,17,23,21,23,16,14},
    ['A' - 32] = {14,17,17,31,17,17,17},
    ['B' - 32] = {30,17,17,30,17,17,30},
    ['C' - 32] = {14,17,16,16,16,17,14},
    ['D' - 32] = {30,17,17,17,17,17,30},
    ['E' - 32] = {31,16,16,30,16,16,31},
    ['F' - 32] = {31,16,16,30,16,16,16},
    ['G' - 32] = {14,17,16,23,17,17,15},
    ['H' - 32] = {17,17,17,31,17,17,17},
    ['I' - 32] = {14,4,4,4,4,4,14},
    ['J' - 32] = {7,2,2,2,2,18,12},
    ['K' - 32] = {17,18,20,24,20,18,17},
    ['L' - 32] = {16,16,16,16,16,16,31},
    ['M' - 32] = {17,27,21,21,17,17,17},
    ['N' - 32] = {17,25,21,19,17,17,17},
    ['O' - 32] = {14,17,17,17,17,17,14},
    ['P' - 32] = {30,17,17,30,16,16,16},
    ['Q' - 32] = {14,17,17,17,21,18,13},
    ['R' - 32] = {30,17,17,30,20,18,17},
    ['S' - 32] = {15,16,16,14,1,1,30},
    ['T' - 32] = {31,4,4,4,4,4,4},
    ['U' - 32] = {17,17,17,17,17,17,14},
    ['V' - 32] = {17,17,17,17,17,10,4},
    ['W' - 32] = {17,17,17,21,21,21,10},
    ['X' - 32] = {17,17,10,4,10,17,17},
    ['Y' - 32] = {17,17,10,4,4,4,4},
    ['Z' - 32] = {31,1,2,4,8,16,31},
    ['[' - 32] = {14,8,8,8,8,8,14},
    ['\\' - 32] = {16,8,8,4,2,2,1},
    [']' - 32] = {14,2,2,2,2,2,14},
    ['^' - 32] = {4,10,17,0,0,0,0},
    ['_' - 32] = {0,0,0,0,0,0,31}
};

static uint16_t font_pixels[128 * 32];
static RD_TEXTURE font_texture = { .width = 128, .height = 32,
    .format = RD_ARGB1555, .texels = font_pixels, .serial = 1 };
static int font_ready;
static int font_width = 8, font_height = 8;
static int cursor_x, cursor_y, indent_x, indent_on;
static uint32_t print_color = 0xffffffffu;

static void font_init(void) {
    if (font_ready) return;
    for (unsigned glyph = 0; glyph < 64; ++glyph) {
        unsigned left = (glyph & 15) * 8, top = (glyph >> 4) * 8;
        for (unsigned y = 0; y < 7; ++y)
            for (unsigned x = 0; x < 5; ++x)
                if (font_rows[glyph][y] & (16u >> x))
                    font_pixels[(top + y) * 128 + left + x + 1] = 0xffffu;
    }
    font_ready = 1;
}

void nlInitPrint(void) {
    font_init();
    cursor_x = cursor_y = indent_x = indent_on = 0;
    font_width = font_height = 8;
    print_color = 0xffffffffu;
}

void nlLocate(Sint32 x, Sint32 y) {
    cursor_x = x;
    cursor_y = y;
    indent_x = x;
}

void nlPrintColor(Uint32 color) {
    print_color = color;
}

void nlSetFontSize(Sint32 width, Sint32 height) {
    font_width = width > 0 ? width : 8;
    font_height = height > 0 ? height : 8;
}

void nlPrnIndentOn(void) {
    indent_on = 1;
}

static void print_character(unsigned char character) {
    if (character == '\n') {
        cursor_x = indent_on ? indent_x : 0;
        ++cursor_y;
        return;
    }
    if (character == '\r') { cursor_x = indent_on ? indent_x : 0; return; }
    if (character == '\t') { cursor_x = (cursor_x + 4) & ~3; return; }
    if (character >= 'a' && character <= 'z') character -= 'a' - 'A';
    if (character < 32 || character > 95) character = '?';
    if (character != ' ') {
        float x = (float)cursor_x * font_width, y = (float)cursor_y * font_height;
        if (x < RD_WIDTH && x + font_width > 0 && y < RD_HEIGHT && y + font_height > 0) {
            unsigned glyph = character - 32;
            float u = (float)((glyph & 15) * 8) / 128.0f;
            float v = (float)((glyph >> 4) * 8) / 32.0f;
            RD_STATE state = { .list = RD_TRANS, .depth = RD_ALWAYS,
                .src = RD_SRC_ALPHA, .dst = RD_INV_SRC_ALPHA,
                .shading = RD_MODULATE_ALPHA, .use_alpha = 1, .tex_alpha = 1,
                .fog = RD_FOG_NONE, .tex = &font_texture };
            RD_VTX vertices[4] = {
                { x, y, 100.0f, u, v, print_color, 0 },
                { x + font_width, y, 100.0f, u + 8.0f / 128.0f, v, print_color, 0 },
                { x + font_width, y + font_height, 100.0f,
                    u + 8.0f / 128.0f, v + 8.0f / 32.0f, print_color, 0 },
                { x, y + font_height, 100.0f, u, v + 8.0f / 32.0f, print_color, 0 }
            };
            rd_quad(&state, vertices);
        }
    }
    ++cursor_x;
}

void nlPuts(char* string) {
    if (!string) return;
    font_init();
    for (const unsigned char* at = (const unsigned char*)string; *at; ++at)
        print_character(*at);
}

int nlPrintf(char* format, ...) {
    if (!format) return -1;
    va_list arguments, copy;
    va_start(arguments, format);
    va_copy(copy, arguments);
    int count = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (count < 0) { va_end(arguments); return count; }
    char local[512];
    char* text = (size_t)count < sizeof local ? local : malloc((size_t)count + 1);
    if (!text) { va_end(arguments); return -1; }
    vsnprintf(text, (size_t)count + 1, format, arguments);
    va_end(arguments);
    nlPuts(text);
    if (text != local) free(text);
    return count;
}
