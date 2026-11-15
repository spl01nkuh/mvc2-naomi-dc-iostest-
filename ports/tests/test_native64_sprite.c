/* Generated cells through the original sprite/palette/decompression routines,
 * the real NL texture/sprite backend, and the software renderer. No game assets
 * or SDK substitutes are used. This checks a component, not native gameplay. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "player.h"
#include "game_memory.h"
#include "host_memory.h"
#include "nl_port.h"
#include "rd_list.h"

extern void sprite_init(void);
extern void font_tex_load(void);
extern void col_set(void* colors, int line, int count);
extern void col_send(void);
extern int sprite_disp(PLAYER_WORK* work);
extern int sprite_set_load(PLAYER_WORK* work, int id);
extern void sprite_set_free(PLAYER_WORK* work, int id);
extern Sint16 spr_set_cnt_tbl[];

enum { SYS_PIXELS = 0x0ce60000, SYS_LIST = 0x0ce60c00,
       FONT_PIXELS = 0x0cc00000, SET_BYTES = 0x3000, GUARD_BYTES = 64 };

static void half(uint8_t* bytes, uint16_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}
static void word(uint8_t* bytes, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[i] = (uint8_t)(value >> (i * 8));
}
static void filled(const uint8_t* bytes, size_t size, uint8_t value) {
    for (size_t i = 0; i < size; ++i) assert(bytes[i] == value);
}
static void native_pointer(const void* pointer) {
    assert(sizeof(void*) == 8 && (uintptr_t)pointer > UINT32_MAX);
}
/* lz_unpack uses eight literal commands per zero flag byte. */
static size_t literals(uint8_t* output, size_t count, uint8_t value) {
    assert(count % 8 == 0);
    size_t written = 0;
    while (count) {
        output[written++] = 0;
        memset(output + written, value, 8);
        written += 8;
        count -= 8;
    }
    return written;
}
static void part(uint8_t* bytes, int16_t x, int16_t y, uint16_t palette, uint16_t cell) {
    half(bytes, (uint16_t)x); half(bytes + 2, (uint16_t)y);
    half(bytes + 4, palette); half(bytes + 6, cell);
}
static void test_system_textures(void) {
    uint8_t* pixels = host_native64_ram_pointer(SYS_PIXELS, 0xc00);
    uint8_t* old_headers = host_native64_ram_pointer(SYS_LIST, 0x5000);
    assert(pixels && old_headers);
    native_pointer(pixels);
    memset(pixels, 0x3c, 0xc00);
    memset(old_headers, 0xa5, 0x5000);
    sprite_keep_flag = 0;
    sprite_init();
    filled(pixels, 0xc00, 0);
    /* Runtime descriptors must not be widened into this serialized scratch. */
    filled(old_headers, 0x5000, 0xa5);
    assert(nl_tex_offset == 0 && !nl_tex[15].loaded);
    for (unsigned i = 16; i < 16 + 0x300; ++i) {
        assert(nl_tex[i].loaded && nl_tex[i].tex.width == 32 && nl_tex[i].tex.height == 32);
        assert(nl_tex[i].tex.format == RD_PAL4 && nl_tex[i].palette == 0);
        filled(nl_tex[i].tex.texels, 32 * 32, 0);
    }
    assert(!nl_tex[16 + 0x300].loaded);
    pixels[0] = 0x77;
    sprite_keep_flag = 1;
    sprite_init();
    assert(pixels[0] == 0x77);
    sprite_keep_flag = 0;
    sprite_init();
    filled(old_headers, 0x5000, 0xa5);

    uint8_t* font = host_native64_ram_pointer(FONT_PIXELS, 128);
    assert(font);
    memset(font, 0x22, 128);
    font_tex_load();
    assert(nl_tex_offset == 0 && nl_tex[14].loaded);
    assert(nl_tex[14].tex.width == 16 && nl_tex[14].tex.height == 16);
    filled(nl_tex[14].tex.texels, 16 * 16, 2);
    memset(font, 0, 128);
    filled(nl_tex[14].tex.texels, 16 * 16, 2); /* backend owns its texels */
}
static void test_palettes(void) {
    memset(&sys_work, 0, sizeof sys_work);
    Sys = &sys_work;
    memset(col_work, 0, 128 * sizeof *col_work);
    for (unsigned i = 0; i < 128; ++i) {
        col_work[i].pal_no = i % 64;
        col_work[i].cnt = 16;
    }
    nlInitPalette(NULL, 0);
    nlSetPaletteMode(2); /* ARGB4444 */
    uint16_t colors[16] = {0};
    colors[1] = 0xff00;
    col_set(colors, 2, 1);
    assert(col_work[2].trans_flag == 1);
    col_send();
    nlLoadCorePaletteData();
    assert(col_work[2].trans_flag == 0 && col_send_buff[2 * 16 + 1] == 0xff00);
    assert(rd_palette[2 * 16 + 1] == UINT32_C(0xffff0000));
    col_work[2].trans_flag = 2;
    col_send();
    nlLoadCorePaletteData();
    assert(col_work[2].trans_flag == 0 && col_send_buff[2 * 16 + 1] == 0xf700);
    assert(rd_palette[2 * 16 + 1] == UINT32_C(0xff770000));
    col_set(colors, 2, 1);
    col_send();
    nlLoadCorePaletteData();
    assert(rd_palette[2 * 16 + 1] == UINT32_C(0xffff0000));
    assert(prio_z[0] == 15 && prio_z[7] == 29 && prio_z[15] == 33);
}
static void test_frame_sprite(void) {
    uint8_t* sprites = calloc(1, 14);
    uint8_t* cells = calloc(1, 4 + 4 + 512 + 64);
    assert(sprites && cells);
    native_pointer(sprites); native_pointer(cells);
    word(sprites, 4); half(sprites + 4, 1);
    part(sprites + 6, 0, 0, 2 * 16, 0);
    word(cells, 4);
    memset(cells + 4, 4, 4); /* displayed and stored size, in units of eight */
    assert(literals(cells + 8, 512, 0x11) == 576);
    PLAYER_WORK work = {0};
    work.spr_adrs = sprites; work.cell_adrs = cells;
    work.spr_no = 0; work.disp_x = work.disp_y = 100.75f;
    work.disp_z = 15; work.disp_zm_x = work.disp_zm_y = work.alpha = 1;
    sprite_init();
    col_send(); /* load bank 400, display bank 16 */
    assert(sprite_tex_set(&work) == 1 && work.tile_top == 0);
    assert(nl_tex[400].palette == 2 && nl_tex[400].loaded);
    filled(nl_tex[400].tex.texels, 32 * 32, 1);
    memset(cells + 8, 0, 576); /* the loaded pixels have their own backing */
    col_send(); /* draw the completed bank 400 */
    uint32_t* frame = malloc((size_t)RD_WIDTH * RD_HEIGHT * sizeof *frame);
    assert(frame);
    rd_quality("exact");
    rd_begin_frame(); rd_background(0xff102030);
    assert(sprite_disp(&work) == 1);
    assert(rd_frame->ntris[RD_TRANS] == 2 && rd_frame->nstates == 1);
    assert(rd_frame->states[0].tex == &nl_tex[400].tex);
    assert(rd_frame->states[0].palette == 2 * 16);
    rd_end_frame(frame, RD_WIDTH);
    assert(frame[110 * RD_WIDTH + 110] == UINT32_C(0xffff0000));
    assert(frame[99 * RD_WIDTH + 99] == UINT32_C(0xff102030));
    assert(frame[140 * RD_WIDTH + 140] == UINT32_C(0xff102030));
    work.spr_no = -1;
    assert(sprite_tex_set(&work) == 0 && sprite_disp(&work) == 0);
    free(frame); free(cells); free(sprites);
}
static void test_sprite_sets(void) {
    enum { FIGHTER = 27, SPRITES = 8, PARTS = 32, CELLS = 256,
           SPRITE_BYTES = SPRITES * 4 + SPRITES * (2 + PARTS * 8),
           CELL_BYTES = CELLS * 4 + CELLS * 40 };
    assert(spr_set_cnt_tbl[FIGHTER] == SPRITES);
    uint8_t* sprites = calloc(1, SPRITE_BYTES);
    uint8_t* cells = calloc(1, CELL_BYTES);
    uint8_t* buffers[2] = {malloc(SET_BYTES + 2 * GUARD_BYTES), malloc(SET_BYTES + 2 * GUARD_BYTES)};
    assert(sprites && cells && buffers[0] && buffers[1]);
    native_pointer(sprites); native_pointer(cells);
    for (unsigned i = 0; i < SPRITES; ++i) {
        unsigned offset = SPRITES * 4 + i * (2 + PARTS * 8);
        word(sprites + i * 4, offset); half(sprites + offset, PARTS);
        for (unsigned j = 0; j < PARTS; ++j)
            part(sprites + offset + 2 + j * 8, 0, 0, 2 * 16, (uint16_t)(i * PARTS + j));
    }
    for (unsigned i = 0; i < CELLS; ++i) {
        unsigned offset = CELLS * 4 + i * 40;
        word(cells + i * 4, offset);
        memset(cells + offset, 1, 4);
        assert(literals(cells + offset + 4, 32, (uint8_t)((i % 15 + 1) * 0x11)) == 36);
    }
    PLAYER_WORK work[2] = {{0}, {0}};
    const int ids[] = {0, 7};
    for (unsigned slot = 0; slot < 2; ++slot) {
        memset(buffers[slot], 0xa5, SET_BYTES + 2 * GUARD_BYTES);
        work[slot].spr_adrs = sprites; work[slot].cell_adrs = cells;
        work[slot].spr_set_buff = buffers[slot] + GUARD_BYTES;
        work[slot].load_type = FIGHTER; work[slot].work_no = ids[slot];
        native_pointer(work[slot].spr_set_buff);
        assert(sprite_set_load(&work[slot], ids[slot]) == CELLS * 32);
        assert(spr_set_tex_cnt[ids[slot]] == CELLS && spr_set_size[ids[slot]] == CELLS * 32);
        assert(nl_tex_offset == 0);
        uint16_t* top = (uint16_t*)(void*)work[slot].spr_set_buff;
        uint8_t* map = work[slot].spr_set_buff + 0x220;
        int32_t* palettes = (int32_t*)(void*)(work[slot].spr_set_buff + 0xa20);
        for (unsigned i = 0; i < SPRITES; ++i) assert(top[i] == i * PARTS);
        for (unsigned i = 0; i < CELLS; ++i) {
            unsigned texture = ids[slot] * 256 + 0x390 + i;
            assert(map[i] == i && palettes[i] == 2);
            assert(nl_tex[texture].loaded && nl_tex[texture].tex.width == 8 && nl_tex[texture].tex.height == 8);
            filled(nl_tex[texture].tex.texels, 8 * 8, (uint8_t)(i % 15 + 1));
        }
        filled(buffers[slot], GUARD_BYTES, 0xa5);
        filled(work[slot].spr_set_buff + SET_BYTES, GUARD_BYTES, 0xa5);
        work[slot].spr_no = SPR_NO_SET;
        work[slot].disp_z = 15; work[slot].disp_zm_x = work[slot].disp_zm_y = work[slot].alpha = 1;
        rd_begin_frame();
        assert(sprite_disp(&work[slot]) == PARTS);
        assert(rd_frame->ntris[RD_TRANS] == PARTS * 2);
    }
    for (unsigned slot = 0; slot < 2; ++slot) {
        sprite_set_free(&work[slot], ids[slot]);
        sprite_set_free(&work[slot], ids[slot]); /* repeated frees must remain safe */
        assert(nl_tex_offset == 0);
        for (unsigned i = 0; i < CELLS; ++i) assert(!nl_tex[ids[slot] * 256 + 0x390 + i].loaded);
        if (!slot) assert(nl_tex[ids[1] * 256 + 0x390].loaded);
        assert(sprite_set_load(&work[slot], ids[slot]) == CELLS * 32);
        sprite_set_free(&work[slot], ids[slot]);
        filled(buffers[slot], GUARD_BYTES, 0xa5);
        filled(work[slot].spr_set_buff + SET_BYTES, GUARD_BYTES, 0xa5);
        free(buffers[slot]);
    }
    free(cells); free(sprites);
}
int main(void) {
    assert(mvc2_game_memory_open());
    mvc2_game_memory_reset();
    nlInitSprite(NULL, 0x1000, 0x1000);
    test_system_textures();
    test_palettes();
    test_frame_sprite();
    test_sprite_sets();
    rd_soft_shutdown();
    nl_tex_free_all();
    mvc2_game_memory_close();
    puts("Native sprite component: backing, serialized cells, real palettes, 256-cell sets and rendered pixels passed.");
    return 0;
}
