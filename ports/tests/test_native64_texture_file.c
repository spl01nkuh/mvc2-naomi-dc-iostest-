/* Generated font/sprite texture files through the real port loaders. Link
 * nl_tex/nl_sprite/nl_strip/naomi_board with dead stripping; no SDK stubs. */
#include "texture_file.h"
#include "nl_port.h"
#include "system_textures.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int nlSpriteLoadTexture(const NLthd* headers, const int32_t* palettes);
extern int nlSpriteLoadTextureOnce(const NLthd* headers, const int32_t* palettes, int index);
extern int nlDispStripLoadTexture(int index, const NLthd* header);

enum { FILE_BYTES = 75, DIRECT = 64, PALETTE = 73 };

static void half(uint8_t* bytes, uint16_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}
static void word(uint8_t* bytes, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[i] = (uint8_t)(value >> (i * 8));
}
static void header(uint8_t* bytes, uint16_t width, uint16_t height,
                   uint32_t type, uint32_t address, uint32_t index) {
    half(bytes, width); half(bytes + 2, height);
    word(bytes + 4, type); word(bytes + 8, address); word(bytes + 12, index);
}
static void fixture(uint8_t* bytes) {
    memset(bytes, 0, FILE_BYTES);
    header(bytes, 2, 2, 0x901, DIRECT, 17);
    header(bytes + 16, 2, 2, 0x502, PALETTE, UINT32_C(0xfffffff7));
    memset(bytes + 32, 0xff, 16);
    half(bytes + DIRECT, 0xf800); half(bytes + DIRECT + 2, 0x07e0);
    half(bytes + DIRECT + 4, 0x001f); half(bytes + DIRECT + 6, 0xffff);
    bytes[PALETTE] = 0x21; bytes[PALETTE + 1] = 0x43;
}
static MVC2NativeTextureFile* decode(const void* bytes, size_t size) {
    MVC2NativeTextureFile* file = NULL;
    MVC2NativeTextureFileStatus status = mvc2_native_texture_file_decode(bytes, size, &file);
    if (status != MVC2_NATIVE_TEXTURE_FILE_OK) {
        fprintf(stderr, "standalone texture decode: %s\n", mvc2_native_texture_file_status_string(status));
        assert(0);
    }
    assert(file);
    return file;
}
static void reject(const void* bytes, size_t size) {
    MVC2NativeTextureFile* file = (MVC2NativeTextureFile*)(uintptr_t)1;
    assert(mvc2_native_texture_file_decode(bytes, size, &file) != MVC2_NATIVE_TEXTURE_FILE_OK);
    assert(file == (MVC2NativeTextureFile*)(uintptr_t)1);
}
static void test_font_and_sprite_backends(void) {
    uint8_t bytes[FILE_BYTES], before[FILE_BYTES];
    fixture(bytes); memcpy(before, bytes, sizeof bytes);
    MVC2NativeTextureFile* file = decode(bytes, sizeof bytes);
    assert(memcmp(bytes, before, sizeof bytes) == 0);
    assert(mvc2_native_texture_file_count(file) == 2);
    const NLthd* headers = mvc2_native_texture_file_headers(file);
    assert(headers == mvc2_native_texture_file_header(file, 0));
    assert(headers + 1 == mvc2_native_texture_file_header(file, 1));
    assert(!mvc2_native_texture_file_header(file, 2));
    assert(mvc2_native_texture_file_source_bytes(file, 0) == 8);
    assert(mvc2_native_texture_file_source_bytes(file, 1) == 2);
    assert(mvc2_native_texture_file_source_bytes(file, 2) == 0);
    assert(headers[0].width == 2 && headers[0].height == 2 && headers[0].type == 0x901);
    assert(headers[0].globl_index == 17 && headers[1].globl_index == -9);
    assert((uint8_t*)headers[1].address - (uint8_t*)headers[0].address == PALETTE - DIRECT);
    assert(headers[2].width == -1 && headers[2].height == -1 && headers[2].type == -1);
    assert(headers[2].address == NULL && headers[2].globl_index == -1);
    memset(bytes, 0xcc, sizeof bytes); /* Input can be reused before loading. */
    assert(nlDispStripLoadTexture(0, headers) == 0); /* system font path */
    assert(nl_tex[0].loaded && nl_tex[0].tex.format == RD_RGB565);
    assert(memcmp(nl_tex[0].tex.texels, before + DIRECT, 8) == 0);
    nl_tex_offset = 1;
    const int32_t palettes[] = {5, 7};
    assert(nlSpriteLoadTextureOnce(headers, palettes, 1) == 0);
    assert(nl_tex[2].palette == 7 && nl_tex[2].tex.format == RD_PAL4);
    const uint8_t expected[] = {1, 3, 2, 4};
    assert(memcmp(nl_tex[2].tex.texels, expected, sizeof expected) == 0);
    nl_tex[3].loaded = 0;
    assert(nlSpriteLoadTexture(headers, palettes) == 0); /* system sprite path */
    assert(nl_tex[1].loaded && nl_tex[2].loaded && !nl_tex[3].loaded);
    mvc2_native_texture_file_free(file);
    /* Real backend loads own their decoded texels after the file is gone. */
    assert(memcmp(nl_tex[1].tex.texels, before + DIRECT, 8) == 0);
    assert(memcmp(nl_tex[2].tex.texels, expected, sizeof expected) == 0);
    nl_tex_free_all();
}
static void test_sentinels(void) {
    uint8_t bytes[FILE_BYTES];
    for (unsigned negative = 0; negative < 2; ++negative) {
        fixture(bytes);
        /* thd_file_load also accepts address zero; sprite loaders still need
         * the native negative type sentinel to stop before a third entry. */
        memset(bytes + 32, negative ? 0xff : 0, 16);
        MVC2NativeTextureFile* file = decode(bytes, sizeof bytes);
        const NLthd* headers = mvc2_native_texture_file_headers(file);
        assert(headers[2].type == -1 && headers[2].width == -1);
        nl_tex_offset = 100;
        assert(nlSpriteLoadTexture(headers, NULL) == 0);
        assert(nl_tex[100].loaded && nl_tex[101].loaded && !nl_tex[102].loaded);
        mvc2_native_texture_file_free(file);
        nl_tex_free_all();
    }
    uint8_t empty[16] = {0};
    const uint32_t terminals[] = {0, UINT32_C(0xffffffff), UINT32_C(0x80000000)};
    for (size_t i = 0; i < sizeof terminals / sizeof terminals[0]; ++i) {
        word(empty + 8, terminals[i]);
        MVC2NativeTextureFile* file = decode(empty, sizeof empty);
        assert(mvc2_native_texture_file_count(file) == 0);
        assert(nlSpriteLoadTexture(mvc2_native_texture_file_headers(file), NULL) == 0);
        mvc2_native_texture_file_free(file);
    }
}
static void test_rejections_and_unaligned_input(void) {
    uint8_t bytes[FILE_BYTES];
    fixture(bytes);
    for (size_t size = 0; size < sizeof bytes; ++size) reject(bytes, size);
    reject(NULL, sizeof bytes);
    reject(bytes, (size_t)64 * 1024 * 1024 + 1);
    assert(mvc2_native_texture_file_decode(bytes, sizeof bytes, NULL) == MVC2_NATIVE_TEXTURE_FILE_ARGUMENT);
    const uint32_t bad_offsets[] = {1, 16, 32, 47, 65, FILE_BYTES, UINT32_C(0x7fffffff)};
    for (size_t i = 0; i < sizeof bad_offsets / sizeof bad_offsets[0]; ++i) {
        fixture(bytes); word(bytes + 8, bad_offsets[i]); reject(bytes, sizeof bytes);
    }
    fixture(bytes); half(bytes, 0); reject(bytes, sizeof bytes);
    fixture(bytes); half(bytes, 1025); reject(bytes, sizeof bytes);
    fixture(bytes); half(bytes + 2, UINT16_MAX); reject(bytes, sizeof bytes);
    fixture(bytes); word(bytes + 4, 0xa01); reject(bytes, sizeof bytes);
    fixture(bytes); word(bytes + 4, 0x10901); reject(bytes, sizeof bytes);
    fixture(bytes); word(bytes + 4, UINT32_MAX); reject(bytes, sizeof bytes);
    fixture(bytes); word(bytes + 40, DIRECT); reject(bytes, sizeof bytes); /* invalid active record */
    uint8_t* unaligned = malloc(sizeof bytes + 1);
    assert(unaligned);
    fixture(unaligned + 1);
    MVC2NativeTextureFile* file = decode(unaligned + 1, sizeof bytes);
    assert((uintptr_t)mvc2_native_texture_file_headers(file)[0].address % 2 == 0);
    assert(nlDispStripLoadTexture(0, mvc2_native_texture_file_headers(file)) == 0);
    mvc2_native_texture_file_free(file); free(unaligned); nl_tex_free_all();
}
static void test_supported_layout_source_extents(void) {
    const struct { uint32_t type; uint16_t width, height; size_t extent; } layouts[] = {
        {0x101, 2, 2, 8}, {0x201, 2, 2, 12}, {0x301, 2, 2, 2049}, {0x401, 2, 2, 2050},
        {0x502, 2, 2, 2}, {0x602, 2, 2, 4}, {0x702, 2, 2, 4}, {0x802, 2, 2, 8},
        {0x901, 2, 2, 8}, {0xb01, 2, 2, 8}, {0xd01, 2, 2, 8},
        {0x101, 1, 1, 2}, {0x201, 1, 1, 4}, {0x301, 1, 1, 2049}, {0x401, 1, 1, 2049},
        {0x502, 1, 1, 1}, {0x602, 1, 1, 2}, {0x702, 1, 1, 1}, {0x802, 1, 1, 4},
        {0x901, 3, 2, 12}, {0xb01, 3, 2, 12}
    };
    for (size_t i = 0; i < sizeof layouts / sizeof layouts[0]; ++i) {
        size_t size = 32 + layouts[i].extent;
        uint8_t* bytes = calloc(1, size + 1);
        assert(bytes);
        header(bytes, layouts[i].width, layouts[i].height, layouts[i].type, 32, 0);
        memset(bytes + 16, 0xff, 16);
        MVC2NativeTextureFile* file = decode(bytes, size);
        assert(mvc2_native_texture_file_source_bytes(file, 0) == layouts[i].extent);
        assert(nlDispStripLoadTexture(0, mvc2_native_texture_file_headers(file)) == 0);
        assert(nl_tex[0].loaded);
        mvc2_native_texture_file_free(file);
        reject(bytes, size - 1); /* required final source byte must be present */
        word(bytes + 8, 33);
        if ((layouts[i].type >> 8) < 5 || (layouts[i].type >> 8) > 8)
            reject(bytes, size + 1); /* valid extent, invalid direct/VQ alignment */
        else {
            file = decode(bytes, size + 1);
            assert(nlDispStripLoadTexture(0, mvc2_native_texture_file_headers(file)) == 0);
            mvc2_native_texture_file_free(file);
        }
        free(bytes); nl_tex_free_all();
    }
    mvc2_native_texture_file_free(NULL);
    assert(mvc2_native_texture_file_count(NULL) == 0);
    assert(!mvc2_native_texture_file_headers(NULL));
    assert(!mvc2_native_texture_file_header(NULL, 0));
    assert(mvc2_native_texture_file_source_bytes(NULL, 0) == 0);
}
static void test_table_limit(void) {
    const size_t count = NL_MAX_TEX, table = (count + 1) * 16, size = table + 8;
    uint8_t* bytes = calloc(1, size);
    assert(bytes);
    for (size_t i = 0; i < count; ++i) header(bytes + i * 16, 2, 2, 0x901, (uint32_t)table, 0);
    memset(bytes + count * 16, 0xff, 16);
    MVC2NativeTextureFile* file = decode(bytes, size);
    assert(mvc2_native_texture_file_count(file) == count);
    const NLthd* headers = mvc2_native_texture_file_headers(file);
    assert(headers[0].address == headers[count - 1].address && headers[count].type == -1);
    mvc2_native_texture_file_free(file);
    /* Turning the last sentinel into another active record exceeds the count
     * limit before any payload can be treated as another native descriptor. */
    header(bytes + count * 16, 2, 2, 0x901, (uint32_t)table, 0);
    reject(bytes, size);
    free(bytes);
}
static void test_system_texture_owners(void) {
    uint8_t bytes[FILE_BYTES];
    fixture(bytes);
    mvc2_system_textures_reset();
    assert(sys_font_thd[0].type == -1 && sys_spr_thd[0].type == -1);
    assert(!mvc2_system_textures_load(3, bytes, sizeof bytes));
    assert(mvc2_system_textures_load(1, bytes, sizeof bytes));
    assert(mvc2_system_textures_load(2, bytes, sizeof bytes));
    NLthd kept[3];
    memcpy(kept, sys_font_thd, sizeof kept);
    assert(!mvc2_system_textures_load(1, bytes, DIRECT + 1));
    assert(memcmp(kept, sys_font_thd, sizeof kept) == 0);
    memset(bytes + DIRECT, 0, FILE_BYTES - DIRECT);
    assert(nlDispStripLoadTexture(0, sys_font_thd) == 0);
    const uint16_t red = 0xf800;
    assert(memcmp(nl_tex[0].tex.texels, &red, sizeof red) == 0);
    fixture(bytes);
    memset(bytes + 16, 0xff, 16); /* replacement with just one active texture */
    assert(mvc2_system_textures_load(1, bytes, sizeof bytes));
    assert(sys_font_thd[0].width == 2 && sys_font_thd[1].type == -1);
    assert(sys_font_thd[2].address == NULL); /* old longer table cannot dangle */
    assert(sys_spr_thd[1].width == 2 && sys_spr_thd[2].type == -1);
    mvc2_system_textures_reset();
    mvc2_system_textures_reset();
    assert(sys_font_thd[0].type == -1 && sys_spr_thd[0].type == -1);
    assert(sys_font_thd[1].address == NULL && sys_spr_thd[1].address == NULL);
    assert(memcmp(nl_tex[0].tex.texels, &red, sizeof red) == 0); /* backend owns texels */
    nl_tex_free_all();
}
int main(void) {
    test_font_and_sprite_backends();
    test_sentinels();
    test_rejections_and_unaligned_input();
    test_supported_layout_source_extents();
    test_table_limit();
    test_system_texture_owners();
    puts("Standalone texture files: owned headers/payloads, font/sprite backends, sentinels and bounds passed.");
    return 0;
}
