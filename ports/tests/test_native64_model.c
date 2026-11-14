/* Decoder fixtures exercise real nl_tex_load, not a replacement texture loader.
 * Link nl_tex.c and naomi_board.c with function/data sections and dead stripping
 * so the real host_log is retained without unrelated board/game entry points.
 * Fixtures contain generated metadata/pixels only, never commercial assets. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "model.h"
#include "player.h"

enum { MODEL_SIZE = 512, TEX_SIZE = 10, OBJECT_OFFSET = 128,
       MESH_OFFSET = OBJECT_OFFSET + 24, PRIMITIVE_OFFSET = MESH_OFFSET + 80,
       OBJECT_END = PRIMITIVE_OFFSET + 104, BASE = 0x0c600000, TEX_BASE = 0x0d000000 };

static unsigned high_address_cases;

static void word(uint8_t* p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (i * 8));
}
static uint32_t read_word(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void half(uint8_t* p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}
static void thd(uint8_t* p, unsigned w, unsigned h, uint32_t type, uint32_t address, int32_t index) {
    half(p, (uint16_t)w);
    half(p + 2, (uint16_t)h);
    word(p + 4, type);
    word(p + 8, address);
    word(p + 12, (uint32_t)index);
}
static void fixture(uint8_t* model, uint8_t* textures) {
    memset(model, 0, MODEL_SIZE);
    memset(textures, 0, TEX_SIZE);
    word(model, BASE + 16);
    word(model + 4, 3);
    word(model + 8, BASE + 64);
    word(model + 16, BASE + OBJECT_OFFSET);
    word(model + 20, BASE + OBJECT_OFFSET); /* shared object reference */
    word(model + 24, 0); /* explicitly absent object */
    thd(model + 64, 2, 2, 0x901, TEX_BASE, 7);
    thd(model + 80, 2, 2, 0x502, TEX_BASE + 8, -9);
    word(model + MESH_OFFSET, 0x02000008);
    word(model + MESH_OFFSET + 32, 0);
    word(model + MESH_OFFSET + 76, 104);
    word(model + PRIMITIVE_OFFSET, 0x48); /* one Gouraud triangle */
    word(model + PRIMITIVE_OFFSET + 4, 1);
    for (unsigned i = 0; i < 3; ++i) {
        uint8_t* vertex = model + PRIMITIVE_OFFSET + 8 + i * 32;
        word(vertex, 0x3f800001); /* full-vertex tag in the position's low bit */
        word(vertex + 4, 0x40000000);
        word(vertex + 8, 0x40400000);
        word(vertex + 24, 0x3f000000);
        word(vertex + 28, 0x3f800000);
    }
    assert(OBJECT_END + 4 <= MODEL_SIZE);
    half(textures, 0xf800); half(textures + 2, 0x07e0);
    half(textures + 4, 0x001f); half(textures + 6, 0xffff);
    textures[8] = 0x21; textures[9] = 0x43;
}

static MVC2NativeModel* decode(const uint8_t* bytes, size_t size,
                               const uint8_t* textures, size_t texture_size) {
    MVC2NativeModel* model = NULL;
    MVC2NativeModelStatus status = mvc2_native_model_decode(bytes, size, textures, texture_size, &model);
    if (status != MVC2_NATIVE_MODEL_OK) {
        fprintf(stderr, "decode failed: %s\n", mvc2_native_model_status_string(status));
        assert(0);
    }
    assert(model != NULL);
    return model;
}
static void reject(const uint8_t* bytes, size_t size,
                    const uint8_t* textures, size_t texture_size) {
    MVC2NativeModel* output = (MVC2NativeModel*)(uintptr_t)1;
    MVC2NativeModelStatus status = mvc2_native_model_decode(bytes, size, textures, texture_size, &output);
    assert(status != MVC2_NATIVE_MODEL_OK);
    assert(output == (MVC2NativeModel*)(uintptr_t)1);
}

static void test_owned_descriptors_and_real_backend(void) {
    uint8_t model[MODEL_SIZE], textures[TEX_SIZE], before[MODEL_SIZE], before_tex[TEX_SIZE];
    fixture(model, textures);
    memcpy(before, model, sizeof model);
    memcpy(before_tex, textures, sizeof textures);
    MVC2NativeModel* native = decode(model, sizeof model, textures, sizeof textures);
    assert(mvc2_native_model_object_count(native) == 3);
    assert(mvc2_native_model_texture_count(native) == 2);
    size_t length = 0;
    int* first = mvc2_native_model_object(native, 0, &length);
    assert(first != NULL && length == OBJECT_END + 4 - OBJECT_OFFSET);
    assert(first == mvc2_native_model_object(native, 1, NULL));
    assert(mvc2_native_model_object(native, 2, &length) == NULL && length == 0);
    assert(mvc2_native_model_objects(native)[0] == first);
    const NLthd* headers = mvc2_native_model_textures(native);
    OBJ_FILE* game_file = mvc2_native_model_file(native);
    assert(game_file->cnt == 3 && game_file->obj[0] == first && game_file->thd == headers);
    assert(headers[0].width == 2 && headers[0].height == 2 && headers[0].type == 0x901);
    assert(headers[0].globl_index == 7 && headers[1].globl_index == -9);
    assert(headers[2].width == 0); /* native terminator, not 16-byte stride arithmetic */
    assert(mvc2_native_model_texture_source_bytes(native, 0) == 8);
    assert(mvc2_native_model_texture_source_bytes(native, 1) == 2);
    if (sizeof(void*) > 4) {
        /* Ordinary native allocation is permitted anywhere. Record actual
         * high-address coverage without imposing an allocator ABI. */
        if ((uintptr_t)headers[0].address > UINT32_MAX) ++high_address_cases;
        if ((uintptr_t)first > UINT32_MAX) ++high_address_cases;
        assert(sizeof(NLthd) > 16);
    }
    assert(!mvc2_native_model_load_texture(native, 2, &nl_tex[12]));
    assert(!mvc2_native_model_load_texture(native, 0, NULL));
    assert(mvc2_native_model_load_texture(native, 0, &nl_tex[12]));
    assert(nl_tex[12].loaded && nl_tex[12].tex.format == RD_RGB565);
    assert(nl_tex[12].tex.width == 2 && nl_tex[12].tex.height == 2);
    assert(memcmp(nl_tex[12].tex.texels, textures, 8) == 0);
    assert(mvc2_native_model_load_texture(native, 1, &nl_tex[12]));
    const uint8_t expected[] = {1, 3, 2, 4};
    assert(nl_tex[12].tex.format == RD_PAL4);
    assert(memcmp(nl_tex[12].tex.texels, expected, sizeof expected) == 0);
    /* The game may update the mutable material copy without altering ROM data. */
    first[9] = 0x12345678;
    assert(memcmp(model, before, sizeof model) == 0);
    assert(memcmp(textures, before_tex, sizeof textures) == 0);
    /* Descriptor data remains owned after the source buffers are reused. */
    memset(model, 0xcc, sizeof model);
    memset(textures, 0xcc, sizeof textures);
    assert(mvc2_native_model_load_texture(native, 0, &nl_tex[12]));
    assert(memcmp(nl_tex[12].tex.texels, before_tex, 8) == 0);
    nl_tex_free_all();
    mvc2_native_model_free(native);
}

static void test_relative_vertex_references(void) {
    uint8_t model[MODEL_SIZE], textures[TEX_SIZE];
    fixture(model, textures);
    word(model + 20, BASE + 352); /* another object with cross-object references */
    const size_t mesh = 352 + 24, prim = mesh + 80;
    word(model + mesh, 8);
    word(model + mesh + 32, 0);
    word(model + mesh + 76, 32);
    word(model + prim, 0x48);
    word(model + prim + 4, 1);
    size_t target = PRIMITIVE_OFFSET + 8;
    for (size_t i = 0; i < 3; ++i) {
        size_t at = prim + 8 + i * 8;
        word(model + at, 0);
        word(model + at + 4, (uint32_t)((int32_t)target - (int32_t)(at + 8)));
    }
    MVC2NativeModel* native = decode(model, sizeof model, textures, sizeof textures);
    uint8_t* first = (uint8_t*)mvc2_native_model_object(native, 0, NULL);
    uint8_t* second = (uint8_t*)mvc2_native_model_object(native, 1, NULL);
    uint8_t* short_record = second + 24 + 80 + 8;
    int32_t reference = (int32_t)read_word(short_record + 4);
    assert(short_record + 8 + reference == first + 24 + 80 + 8);
    mvc2_native_model_free(native);
    word(model + prim + 12, 0x7fffffff);
    reject(model, sizeof model, textures, sizeof textures);
    word(model + prim + 12, 0x80000000);
    reject(model, sizeof model, textures, sizeof textures);
    word(model + prim + 12, (uint32_t)((int32_t)target - (int32_t)(prim + 16) + 1));
    reject(model, sizeof model, textures, sizeof textures);
}

static void test_truncations_and_bad_metadata(void) {
    uint8_t model[MODEL_SIZE], textures[TEX_SIZE];
    fixture(model, textures);
    for (size_t size = 0; size < OBJECT_END + 4; ++size) reject(model, size, textures, sizeof textures);
    for (size_t size = 0; size < TEX_SIZE; ++size) reject(model, sizeof model, textures, size);
    uint8_t* unaligned = malloc(MODEL_SIZE + 1);
    assert(unaligned != NULL);
    memcpy(unaligned + 1, model, MODEL_SIZE);
    MVC2NativeModel* native = decode(unaligned + 1, MODEL_SIZE, textures, sizeof textures);
    mvc2_native_model_free(native);
    free(unaligned);

    word(model, 15); reject(model, sizeof model, textures, sizeof textures);
    fixture(model, textures); word(model + 4, 16385); reject(model, sizeof model, textures, sizeof textures);
    fixture(model, textures); word(model + 8, BASE - 1); reject(model, sizeof model, textures, sizeof textures);
    fixture(model, textures); word(model + 16, BASE + MODEL_SIZE); reject(model, sizeof model, textures, sizeof textures);
    fixture(model, textures); word(model + 16, BASE + 129); reject(model, sizeof model, textures, sizeof textures);
    fixture(model, textures); half(model + 64, 0xffff); reject(model, sizeof model, textures, sizeof textures);
    fixture(model, textures); half(model + 66, 1025); reject(model, sizeof model, textures, sizeof textures);
    fixture(model, textures); word(model + 88, TEX_BASE - 1); reject(model, sizeof model, textures, sizeof textures);
    fixture(model, textures); word(model + 72, 0xfffffffe); reject(model, sizeof model, textures, sizeof textures);
    fixture(model, textures); word(model + 68, 0xa01); reject(model, sizeof model, textures, sizeof textures);
    fixture(model, textures); word(model + MESH_OFFSET + 32, 2); reject(model, sizeof model, textures, sizeof textures);
    fixture(model, textures); word(model + MESH_OFFSET + 76, UINT32_MAX); reject(model, sizeof model, textures, sizeof textures);
    fixture(model, textures); word(model + PRIMITIVE_OFFSET + 4, 0x7fffffff); reject(model, sizeof model, textures, sizeof textures);
    fixture(model, textures); word(model + MESH_OFFSET + 76, 16); reject(model, sizeof model, textures, sizeof textures);
    fixture(model, textures); word(model + MESH_OFFSET + 76, 109); reject(model, sizeof model, textures, sizeof textures);
    fixture(model, textures); word(model + PRIMITIVE_OFFSET, 0x40); word(model + PRIMITIVE_OFFSET + 4, 4097);
    reject(model, sizeof model, textures, sizeof textures);
    fixture(model, textures); word(model, UINT32_MAX); reject(model, sizeof model, textures, sizeof textures);
    reject(model, (size_t)64 * 1024 * 1024 + 1, textures, sizeof textures);
    reject(model, sizeof model, textures, (size_t)64 * 1024 * 1024 + 1);
    assert(mvc2_native_model_decode(NULL, 0, NULL, 0, &native) == MVC2_NATIVE_MODEL_ARGUMENT);
    assert(mvc2_native_model_decode(model, sizeof model, textures, sizeof textures, NULL) == MVC2_NATIVE_MODEL_ARGUMENT);
}

static void test_texture_source_alignment(void) {
    uint8_t model[MODEL_SIZE], textures[TEX_SIZE];
    fixture(model, textures);
    /* The 16-bit loader would dereference a misaligned uint16_t here; copying
       the whole texture blob to malloc memory does not fix an odd suboffset. */
    thd(model + 80, 2, 2, 0x101, TEX_BASE + 1, 0);
    reject(model, sizeof model, textures, sizeof textures);
    thd(model + 80, 2, 2, 0x901, TEX_BASE + 1, 0);
    reject(model, sizeof model, textures, sizeof textures);
    /* Four-bit index payloads use byte reads, so an odd offset remains valid. */
    thd(model + 80, 2, 2, 0x502, TEX_BASE + 7, 0);
    MVC2NativeModel* native = decode(model, sizeof model, textures, sizeof textures);
    assert(mvc2_native_model_load_texture(native, 1, &nl_tex[14]));
    const uint8_t expected[] = {15, 1, 15, 2};
    assert(memcmp(nl_tex[14].tex.texels, expected, sizeof expected) == 0);
    nl_tex_free_all();
    mvc2_native_model_free(native);
}

static void test_source_extents(void) {
    struct Case { uint32_t type; unsigned w, h; size_t needed; } cases[] = {
        {0x101, 2, 2, 8}, {0x201, 2, 2, 12}, {0x502, 2, 2, 2},
        {0x602, 2, 2, 4}, {0x702, 2, 2, 4}, {0x802, 2, 2, 8},
        {0x301, 2, 2, 2049}, {0x401, 4, 4, 2054},
        {0x901, 3, 2, 12}, {0xb01, 3, 2, 12}, {0xd02, 4, 2, 16}
    };
    uint8_t model[128];
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        struct Case c = cases[i];
        memset(model, 0, sizeof model);
        word(model, BASE + 16); word(model + 8, BASE + 32);
        thd(model + 32, c.w, c.h, c.type, TEX_BASE, 0);
        uint8_t* pixels = calloc(c.needed, 1);
        assert(pixels != NULL);
        MVC2NativeModel* native = decode(model, sizeof model, pixels, c.needed);
        assert(mvc2_native_model_texture_source_bytes(native, 0) == c.needed);
        assert(mvc2_native_model_load_texture(native, 0, &nl_tex[13]));
        mvc2_native_model_free(native);
        reject(model, sizeof model, pixels, c.needed - 1);
        free(pixels);
        nl_tex_free_all();
    }
    /* Empty texture/object tables need only the serialized width terminator. */
    memset(model, 0, sizeof model);
    word(model, BASE + 16); word(model + 8, BASE + 16);
    MVC2NativeModel* empty = decode(model, 18, NULL, 0);
    assert(mvc2_native_model_object_count(empty) == 0);
    assert(mvc2_native_model_texture_count(empty) == 0);
    assert(mvc2_native_model_textures(empty)[0].width == 0);
    mvc2_native_model_free(empty);
    mvc2_native_model_free(NULL);
    assert(mvc2_native_model_file(NULL) == NULL);
    assert(mvc2_native_model_texture(NULL, 0) == NULL);
    assert(mvc2_native_model_objects(NULL) == NULL);
    assert(mvc2_native_model_texture_source_bytes(NULL, SIZE_MAX) == 0);
}

int main(void) {
    test_owned_descriptors_and_real_backend();
    test_relative_vertex_references();
    test_truncations_and_bad_metadata();
    test_source_extents();
    test_texture_source_alignment();
    if (sizeof(void*) > 4)
        printf("Native model high-address allocation cases exercised: %u (allocator-dependent).\n", high_address_cases);
    puts("Native model descriptors: bounded references, owned copies and real texture backend passed.");
    return 0;
}
