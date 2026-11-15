/* Real select/decompression/texture and stage morph routines on native RAM.
 * Generated inputs contain serialized 32-bit offsets; no game assets, fixed
 * process mappings, or substitute SDK implementations are used. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "player.h"
#include "game_memory.h"
#include "host_memory.h"
#include "nl_port.h"

/* Include the units to exercise their private shape/numeric boundaries too. */
#include "../../src/sel_sub.c"
#include "../../src/set03_00.c"
#include "../../src/pls03_02.c"
#include "../../src/sound.c"

static void native_pointer(const void *pointer) {
    assert(sizeof(void *) == 8 && (uintptr_t)pointer > UINT32_MAX);
}
static void filled(const void *bytes, size_t size, uint8_t value) {
    const uint8_t *p = bytes;
    for (size_t i = 0; i < size; ++i) assert(p[i] == value);
}
static void literals(uint16_t *destination, uint16_t value) {
    *destination++ = 0; /* sixteen literal words */
    for (int i = 0; i < 16; ++i) *destination++ = value;
    *destination++ = 0x8000; /* zero-length copy ends the stream */
    *destination++ = 0;
    *destination = 0;
}
static void test_faces_and_names(void) {
    uint8_t *source = host_native64_ram_pointer(0x0c800000, 0x400000);
    assert(source);
    Sys = &sys_work;
    memset(Sys, 0, sizeof *Sys);
    Sys->tex_buff = source;
    memset(source, 0x4d, 0x400000);
    memset(FACE_TEX, 0xa5, 0x40000);
    native_pointer(FACE_TEX); native_pointer(FACE2_TEX);
    pl_work[2].load_type = 2;
    face_tex_set(2);
    /* Face 2 occupies four 64x64 cells, and its name eight 32x32 cells. */
    size_t copied = 0;
    for (size_t i = 0; i < 0x40000; ++i) {
        assert(FACE_TEX[i] == 0xa5 || FACE_TEX[i] == 0x4d);
        copied += FACE_TEX[i] == 0x4d;
    }
    assert(copied == 4 * 0x2000 + 8 * 0x800);
    for (int side = 0; side < 2; ++side) {
        int pl = side, character = side ? 27 : 26;
        int source_character = character >= 27 ? character - 3 : character;
        pl_work[pl].load_type = character;
        memset(source + 0x2e0000 + source_character * 0x4800, 0x31 + side, 0x4800);
        face2_tex_set(pl);
        filled(FACE2_TEX + side * 0x4800, 0x4800, 0x31 + side);
    }
    const int characters[] = {0, 26, 27, 58};
    for (size_t i = 0; i < sizeof characters / sizeof *characters; ++i) {
        int no = characters[i], n = no >= 27 ? no - 3 : no;
#ifdef TARGET_DC
        Sint32 *table = NAME_PIC_FILE;
        table[n] = 0x200 + n * 0x80;
        uint16_t *packed = (uint16_t *)((uint8_t *)table + table[n]);
#else
        const FILE_DATA *table = NAME_PIC_TBL;
        uint16_t *packed = host_native64_ram_pointer(
            table[n].adrs - table[0].adrs + 0x0c520000, table[n].size);
#endif
        assert(packed); native_pointer(packed);
        literals(packed, (uint16_t)(0x1230 + i));
        for (int side = 0; side < 2; ++side) {
            uint16_t *destination = host_native64_ram_pointer(0x0c720000 + side * 0x20000, 0x20000);
            memset(destination, 0xa5, 0x20000);
            name_pic_set(side, no);
            for (int j = 0; j < 16; ++j) assert(destination[j] == 0x1230 + i);
            filled(destination + 16, 0x20000 - 32, 0xa5);
        }
    }
}
static void prepare_texture(int index) {
    uint16_t blank[4] = {0};
    NLthd header = {2, 2, 0x901, blank, index};
    assert(nl_tex_load(&nl_tex[index], &header));
}
static void request(int number, int index, uint32_t address, uint16_t value) {
    uint16_t *source = host_native64_ram_pointer(address, 8);
    assert(source); native_pointer(source);
    for (int i = 0; i < 4; ++i) source[i] = value;
    tex_req_flag = 1; tex_req_no = number;
    tex_req_send();
    assert(!tex_req_flag && !tex_req_no);
    assert(memcmp(nl_tex[index].tex.texels, source, 8) == 0);
}
static void test_texture_requests(void) {
    const int indices[] = {0x810, 0x811, 0x81c, 0x81d, 0x81e, 0x81f, 0x820, 0x821, 0x914, 0xc99, 0xca7};
    for (size_t i = 0; i < sizeof indices / sizeof *indices; ++i) prepare_texture(indices[i]);
    for (int no = 4; no <= 5; ++no)
        request(no, 0x81c, 0x0c420000 + (no - 4) * 0x80000, (uint16_t)(0x4000 + no));
    for (int no = 6; no <= 10; ++no)
        request(no, no + 0x817, 0x0c720000 + (no - 6) * 0x20000, (uint16_t)(0x6000 + no));
    for (int stage = 0; stage < 16; ++stage) {
        stage_no = stage;
        request(29, 0x914, 0x0cc60000 + stage * 0x8000, (uint16_t)(0x2900 + stage));
    }
    memset(FACE_TEX, 0x51, 8); memset(FACE2_TEX, 0x61, 8);
    face_tex_send();
    filled(nl_tex[0x810].tex.texels, 8, 0x51);
    filled(nl_tex[0x81c].tex.texels, 8, 0x61);
    memset(FACE_TEX + 0x20000, 0x52, 8); memset(FACE2_TEX + 0x4800, 0x62, 8);
    face_tex_send_2p();
    filled(nl_tex[0x811].tex.texels, 8, 0x52);
    filled(nl_tex[0x81d].tex.texels, 8, 0x62);
    _Alignas(4) uint8_t picture[256] = {0};
    int32_t offsets[3] = {0, 16, 128};
    memcpy(picture, offsets, sizeof offsets);
    PLAYER_WORK player = {0};
    player.pic_adrs = (Sint32 *)(void *)picture; player.work_no = 2;
    literals((uint16_t *)(void *)(picture + offsets[1]), 0x7171);
    literals((uint16_t *)(void *)(picture + offsets[2]), 0x7272);
    pl_pic_send(&player); filled(nl_tex[0xc99].tex.texels, 8, 0x71);
    pl_pic_send2(&player); filled(nl_tex[0xca7].tex.texels, 8, 0x72);
    nl_tex_free_all();
}
static void morph_at(uint32_t address) {
    int32_t *file = host_native64_ram_pointer(address, 64);
    assert(file); native_pointer(file);
    const int32_t header[4] = {2, 0, 1, 5};
    const VEC3 shapes[4] = {{2, 4, 6}, {8, 10, 12}, {10, 20, 30}, {40, 50, 60}};
    memcpy(file, header, sizeof header); memcpy(file + 4, shapes, sizeof shapes);
    int32_t *mesh = calloc(16, sizeof *mesh);
    assert(mesh); native_pointer(mesh);
    PLAYER_WORK player = {0}; player.obj = mesh;
    set03_00_morph(&player, 0.25f, 0, 1);
    const VEC3 expected[2] = {{4, 8, 12}, {16, 20, 24}};
    for (int i = 0; i < 2; ++i) {
        uint32_t words[3]; memcpy(words, &expected[i], sizeof words); words[0] |= 1;
        assert(memcmp(mesh + header[i + 2], words, sizeof words) == 0);
    }
    assert(mesh[0] == 0 && mesh[4] == 0 && mesh[8] == 0);
    free(mesh);
}
static void test_numeric_priority(void) {
    uint8_t *rows = malloc(512);
    assert(rows); native_pointer(rows);
    /* Exercise every possible byte, including negative Sint8 results, using
       actual native addresses. This does not transport/reconstruct pointers. */
    for (size_t i = 0; i < 256; ++i)
        assert((uint8_t)pls03_02_address_priority(rows + i) == (uint8_t)(uintptr_t)(rows + i));
    free(rows);
}
int main(void) {
    mvc2_game_memory_reset();
    test_faces_and_names(); test_texture_requests();
#ifdef TARGET_DC
    stage_no = 3; morph_at(0x0cec5020);
    stage_no = 12; morph_at(0x0cec4918);
    assert(EX_SOUND_ADRS == HOST_NATIVE64_RAM_AT(0x0ccde000));
#else
    morph_at(0x0d852038);
    assert(EX_SOUND_ADRS == HOST_NATIVE64_RAM_AT(0x0c420000));
#endif
    assert((void *)SOUND_RAM_AT(0x0cc00000) == host_native64_ram_pointer(0x0cc00000, 1));
    test_numeric_priority();
    puts("Native raw gameplay: select copies, real name/picture unpacking, texture requests, stage morphs and numeric priorities passed.");
    return 0;
}
