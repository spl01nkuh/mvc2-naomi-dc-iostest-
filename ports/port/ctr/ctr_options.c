/*
 * ctr_options.c  The bottom screen while the game runs: the options' buttons, drawn when something changes, and the
 * screens they open (the version chooser, the controls)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <3ds.h>

#include "naomi_host.h"
#include "naomi_input.h"
#include "ctr_front.h"
#include "ctr_render.h"
#include "ctr_audio.h"
#include "options_bg_lz.h"
#include "native_lz.h"
#include "stretch_lz.h"
#include "switch_lz.h"
#include "controls_lz.h"
#include "unlock_lz.h"

volatile int ctr_bottom_lost;

enum { BUTTON_W = 116, BUTTON_H = 29, DIM = 110 };
enum { B_SCALE, B_VERSION, B_CONTROLS, B_UNLOCK, BUTTONS };
static const struct {
    int x, y;
} place[BUTTONS] = { { 28, 84 }, { 176, 84 }, { 102, 137 }, { 102, 190 } };

static int shown;           /* the screen is ours: drawn once, and again only when it changes */
static int pressed = -1;    /* the button under a touch held */
static int stretch;
static int apt_hooked;
static aptHookCookie apt_cookie;

static void apt_event(APT_HookType type, void* param) {
    (void)param;
    if (type == APTHOOK_ONRESTORE || type == APTHOOK_ONWAKEUP) {
        ctr_bottom_lost = 1;
    }
}

static int arcade(void) {
    return strstr(game_title(), "Dreamcast") == NULL;
}

static int visible(int b) {
    return b != B_CONTROLS || arcade();
}

static int at_button(int x, int y) {
    int b;
    for (b = 0; b < BUTTONS; b++) {
        if (visible(b) && x >= place[b].x && x < place[b].x + BUTTON_W && y >= place[b].y && y < place[b].y + BUTTON_H) {
            return b;
        }
    }
    return -1;
}

static u16* bottom(void) {
    return (u16*)gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
}

static void flush_bottom(void) {
    GSPGPU_FlushDataCache(bottom(), 320 * 240 * 2);
}

/* src over dst (0xRRGGBB), a tex3ds texel R G B A, the source dimmed to dim / 256 */
static u32 blend(u32 dst, u32 t, int dim) {
    int a = (int)(t & 0xFF), c, out = 0;
    for (c = 0; c < 3; c++) {
        int s = (int)(t >> (24 - 8 * c) & 0xFF) * dim / 256, d = (int)(dst >> (16 - 8 * c) & 0xFF);
        out |= (d + (s - d) * a / 255) << (16 - 8 * c);
    }
    return (u32)out;
}

static void put(u16* fb, int x, int y, u32 c) {
    fb[x * 240 + 239 - y] = (u16)((c >> 8 & 0xF800) | (c >> 5 & 0x07E0) | (c >> 3 & 0x1F));
}

static u32 get(const u16* fb, int x, int y) {
    u16 p = fb[x * 240 + 239 - y];
    return (u32)(p >> 11) << 19 | (u32)(p >> 5 & 0x3F) << 10 | (u32)(p & 0x1F) << 3;
}

/* the background, made once as the screen has it; 0 when there is no memory for it */
static int draw_back(u16* fb) {
    static u16* back;
    if (back == NULL) {
        u16* bg = ctr_art_unpack(options_bg_lz, options_bg_lz_size, 512 * 256 * 2);     /* RGB565 */
        int x, y;
        back = bg != NULL ? host_try_malloc(320 * 240 * 2) : NULL;
        for (x = 0; x < 320 && back != NULL; x++) {
            for (y = 0; y < 240; y++) {
                back[x * 240 + 239 - y] = bg[ctr_art_texel(x, y, 512)];
            }
        }
        free(bg);
        if (back == NULL) {
            return 0;
        }
    }
    memcpy(fb, back, 320 * 240 * 2);
    return 1;
}

/* the keys that closed a screen let go before the game sees them */
static void wait_release(void) {
    do {
        gspWaitForVBlank();
        ctr_render_alive();
        hidScanInput();
    } while (aptMainLoop() && (hidKeysHeld() & (0xFFFF | KEY_TOUCH)) != 0);     /* not the circle pads */
}

static void draw_button(u16* fb, int b, int dim) {
    static const struct {
        const unsigned char* lz;
        const unsigned int* size;
    } art[] = { { native_lz, &native_lz_size }, { stretch_lz, &stretch_lz_size }, { switch_lz, &switch_lz_size },
                { controls_lz, &controls_lz_size }, { unlock_lz, &unlock_lz_size } };
    int i = b == B_SCALE ? stretch : b + 1, x, y;
    u32* px = ctr_art_unpack(art[i].lz, *art[i].size, 128 * 32 * 4);
    if (px == NULL) {
        return;
    }
    for (x = 0; x < BUTTON_W; x++) {
        for (y = 0; y < BUTTON_H; y++) {
            int sx = place[b].x + x, sy = place[b].y + y;
            put(fb, sx, sy, blend(get(fb, sx, sy), px[ctr_art_texel(x, y, 128)], dim));
        }
    }
    free(px);
}

static void draw(void) {
    u16* fb = bottom();
    int b;
    if (!draw_back(fb)) {
        consoleClear();
        printf("Marvel vs. Capcom 2\n\nSELECT coin, SELECT+START other version\nSELECT+L test, SELECT+B quit\n"
               "SELECT+Y the sound's channels to the log\n");
        shown = 0;
        return;
    }
    for (b = 0; b < BUTTONS; b++) {
        if (visible(b)) {
            draw_button(fb, b, b == pressed || (b == B_UNLOCK && host_unlock) ? DIM : 256);
        }
    }
    flush_bottom();
    shown = 1;
}

/* the controls: each of the six buttons on one of the 3DS's, kept in mvc2.cfg (keys = Y,X,L,B,A,R) */
static const struct {
    const char* name;
    uint32_t bit;
} actions[] = { { "LIGHT PUNCH", JVS_B1 }, { "HEAVY PUNCH", JVS_B2 }, { "ASSIST 1", JVS_B3 },
                { "LIGHT KICK", JVS_B4 }, { "HEAVY KICK", JVS_B5 }, { "ASSIST 2", JVS_B6 } };
enum { ACTIONS = sizeof actions / sizeof actions[0] };
static const struct {
    const char* name;
    u32 key;
} pad[] = { { "A", KEY_A }, { "B", KEY_B }, { "X", KEY_X }, { "Y", KEY_Y },
            { "L", KEY_L }, { "R", KEY_R }, { "ZL", KEY_ZL }, { "ZR", KEY_ZR } };
enum { PAD = sizeof pad / sizeof pad[0] };
static const int default_keys[ACTIONS] = { 3, 2, 4, 1, 0, 5 };     /* Y X L B A R */
static int keys[ACTIONS];
static int custom = -1;     /* -1: not read yet */

static void read_keys(void) {
    const char* v = config_get("keys");
    int i, k;
    memcpy(keys, default_keys, sizeof keys);
    for (i = 0; v != NULL && i < ACTIONS; i++) {
        size_t n = strcspn(v, ",");
        for (k = 0; k < PAD; k++) {
            if (strlen(pad[k].name) == n && strncmp(v, pad[k].name, n) == 0) {
                keys[i] = k;
            }
        }
        v = v[n] == ',' ? v + n + 1 : NULL;
    }
    custom = memcmp(keys, default_keys, sizeof keys) != 0;     /* the first ones saved keep ZL and ZR */
}

static void save_keys(void) {
    char v[64];
    int i, n = 0;
    for (i = 0; i < ACTIONS; i++) {
        n += snprintf(v + n, sizeof v - n, "%s%s", i ? "," : "", pad[keys[i]].name);
    }
    config_set("keys", v);
    custom = memcmp(keys, default_keys, sizeof keys) != 0;
}

uint32_t ctr_options_keys(uint32_t held) {
    const int* k;
    uint32_t bits = 0;
    int i;
    if (custom < 0) {
        read_keys();
    }
    /* the arcade's own controls, if set; else the first ones, with ZL and ZR the assists as well */
    k = custom && arcade() ? keys : default_keys;
    for (i = 0; i < ACTIONS; i++) {
        if (held & pad[k[i]].key) {
            bits |= actions[i].bit;
        }
    }
    if (k == default_keys) {
        bits |= (held & KEY_ZL ? JVS_B3 : 0) | (held & KEY_ZR ? JVS_B6 : 0);
    }
    return bits;
}

/* the console's 8x8 font, in colour c with a dark shadow */
static void text(u16* fb, int x, int y, const char* s, u32 c) {
    const ConsoleFont* font = &consoleGetDefault()->font;
    for (; *s != 0; s++, x += 8) {
        int ch = (unsigned char)*s - font->asciiOffset, r, b;
        if (ch < 0 || ch >= font->numChars) {
            continue;
        }
        for (r = 0; r < 8; r++) {
            u8 bits = font->gfx[ch * 8 + r];
            for (b = 0; b < 8; b++) {
                if (bits & (0x80 >> b) && x + b + 1 < 320 && y + r + 1 < 240) {
                    put(fb, x + b + 1, y + r + 1, 0);
                    put(fb, x + b, y + r, c);
                }
            }
        }
    }
}

enum { ROW_Y = 64, ROW_H = 18, ROWS_X = 40, ROWS_W = 240 };

static void draw_controls(int chosen) {
    u16* fb = bottom();
    int i, x, y;
    draw_back(fb);
    for (y = ROW_Y - 6; y < ROW_Y + ACTIONS * ROW_H + 4; y++) {
        for (x = ROWS_X - 8; x < ROWS_X + ROWS_W + 8; x++) {
            int in = y >= ROW_Y + chosen * ROW_H - 5 && y < ROW_Y + chosen * ROW_H + 13;
            u32 c = get(fb, x, y);
            put(fb, x, y, in ? 0x6040A0 : (c >> 2 & 0x3F3F3F));
        }
    }
    for (i = 0; i < ACTIONS; i++) {
        u32 c = i == chosen ? 0xFFFFFF : 0xFFE040;
        text(fb, ROWS_X, ROW_Y + i * ROW_H, actions[i].name, c);
        text(fb, ROWS_X + ROWS_W - 8 * (int)strlen(pad[keys[i]].name), ROW_Y + i * ROW_H, pad[keys[i]].name, c);
    }
    text(fb, 24, 186, "Choose a row, then press a button", 0xFFFFFF);
    text(fb, 24, 202, "START done   SELECT the first ones", 0xFFFFFF);
    flush_bottom();
}

static void controls(void) {
    int chosen = 0, drawn = -1, i, k;
    if (custom < 0) {
        read_keys();
    }
    while (aptMainLoop()) {
        u32 down;
        hidScanInput();
        down = hidKeysDown();
        if (down & KEY_START) {
            break;
        }
        if (down & KEY_SELECT) {
            memcpy(keys, default_keys, sizeof keys);
            drawn = -1;
        }
        if (down & (KEY_UP | KEY_DOWN)) {
            chosen = (chosen + (down & KEY_UP ? ACTIONS - 1 : 1)) % ACTIONS;
        }
        if (down & KEY_TOUCH) {
            touchPosition t;
            hidTouchRead(&t);
            if (t.py >= ROW_Y - 5 && t.py < ROW_Y + ACTIONS * ROW_H - 5) {
                chosen = (t.py - ROW_Y + 5) / ROW_H;
            }
        }
        for (k = 0; k < PAD; k++) {
            if (down & pad[k].key) {
                for (i = 0; i < ACTIONS; i++) {
                    if (keys[i] == k) {
                        keys[i] = keys[chosen];     /* the two swap */
                    }
                }
                keys[chosen] = k;
                chosen = (chosen + 1) % ACTIONS;
                drawn = -1;
                break;
            }
        }
        if (chosen != drawn || ctr_bottom_lost) {
            ctr_bottom_lost = 0;
            draw_controls(chosen);
            drawn = chosen;
        }
        gspWaitForVBlank();
        ctr_render_alive();
    }
    save_keys();
}

void ctr_options_show(void) {
    if (!apt_hooked) {
        aptHook(&apt_cookie, apt_event, NULL);
        apt_hooked = 1;
        stretch = config_get("scale") != NULL && strcmp(config_get("scale"), "stretch") == 0;
        if (getenv("MVC2_STRETCH") != NULL) {
            stretch = strcmp(getenv("MVC2_STRETCH"), "0") != 0;
        }
    }
    pressed = -1;
    draw();
}

/* a button acts when the touch on it is let go there; the screens it opens hold the game until they close */
int ctr_options_input(void) {
    static int touched = -1;
    int act = OPT_NONE;
    if (hidKeysHeld() & KEY_TOUCH) {
        touchPosition t;
        int b;
        hidTouchRead(&t);
        b = at_button(t.px, t.py);
        if (hidKeysDown() & KEY_TOUCH) {
            touched = b;
        }
        b = b == touched ? b : -1;
        if (b != pressed && shown) {
            pressed = b;
            draw();
        }
        return OPT_NONE;
    }
    if (touched >= 0 && pressed == touched) {
        switch (touched) {
        case B_SCALE:
            stretch = !stretch;
            config_set("scale", stretch ? "stretch" : "aspect");
            ctr_render_set_stretch(stretch);
            break;
        case B_VERSION: {
            int now = arcade() ? 0 : 1, want;
            ctr_audio_idle();
            want = ctr_ask_bottom(now);
            wait_release();
            if (want >= 0 && want != now) {
                config_set("mode", want ? "dc" : "arcade");
                act = OPT_SWITCH;
            }
            break;
        }
        case B_CONTROLS:
            ctr_audio_idle();
            controls();
            wait_release();
            break;
        case B_UNLOCK:
            if (!host_unlock) {
                config_set("unlock", "all");
                act = OPT_RESTART;
            }
            break;
        }
    }
    touched = -1;
    if (pressed >= 0 || ctr_bottom_lost) {
        ctr_bottom_lost = 0;
        pressed = -1;
        if (shown) {
            draw();
        }
    }
    return act;
}
