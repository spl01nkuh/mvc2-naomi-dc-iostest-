/* Synthetic assets only: exercise the production projection, model, sprite,
 * strip and software renderer paths used by the native iOS game. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "player.h"
#include "model.h"
#include "nl_port.h"
#include "rd_list.h"

extern void nlMakeModelProjectMatrix(void);
enum { MODEL_BYTES = 280, OBJECT = 64, MESH = OBJECT + 24,
       PRIM = MESH + 80, PITCH = RD_MAX_WIDTH + 8 };

static void word(uint8_t* bytes, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[i] = (uint8_t)(value >> (i * 8));
}
static void scalar(uint8_t* bytes, float value, int tag) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    word(bytes, tag ? bits | 1u : bits);
}
static MVC2NativeModel* fixture(void) {
    uint8_t bytes[MODEL_BYTES] = {0}, texels[8];
    enum { BASE = 0x0c600000, PIXELS = 0x0d000000 };
    word(bytes, BASE + 16); word(bytes + 4, 1); word(bytes + 8, BASE + 24);
    word(bytes + 16, BASE + OBJECT);
    bytes[24] = bytes[26] = 2;
    word(bytes + 28, 0x901); word(bytes + 32, PIXELS);
    word(bytes + MESH, 0x28);
    word(bytes + MESH + 4, (uint32_t)RD_ALWAYS << 29);
    word(bytes + MESH + 8, (uint32_t)RD_ONE << 29 | (uint32_t)RD_FOG_NONE << 22);
    word(bytes + MESH + 12, (uint32_t)RD_RGB565 << 27);
    scalar(bytes + MESH + 24, -2, 0);
    scalar(bytes + MESH + 28, 1, 0);
    bytes[MESH + 36] = 0xff;
    for (unsigned i = 0; i < 4; ++i) scalar(bytes + MESH + 44 + i * 4, 1, 0);
    word(bytes + MESH + 76, 104);
    word(bytes + PRIM, 0x48); word(bytes + PRIM + 4, 1);
    const float positions[3][3] = {{-.8f, -.6f, -2}, {.8f, -.6f, -2}, {0, .6f, -2}};
    for (unsigned i = 0; i < 3; ++i) {
        uint8_t* vertex = bytes + PRIM + 8 + i * 32;
        for (unsigned axis = 0; axis < 3; ++axis)
            scalar(vertex + axis * 4, positions[i][axis], axis == 0);
        scalar(vertex + 24, .5f, 0); scalar(vertex + 28, .5f, 0);
    }
    for (unsigned i = 0; i < 4; ++i) texels[i * 2] = 0, texels[i * 2 + 1] = 0xf8;
    MVC2NativeModel* model = NULL;
    assert(mvc2_native_model_decode(bytes, sizeof bytes, texels, sizeof texels,
                                   &model) == MVC2_NATIVE_MODEL_OK);
    return model;
}

static void hud_sprite(void) {
    uint16_t pixels[32 * 8];
    for (unsigned i = 0; i < 32 * 8; ++i) pixels[i] = 0x07e0;
    NLthd header = {32, 8, 0x901, pixels, 0};
    assert(nl_tex_load(&nl_tex[500], &header));
    NLsprarg sprite = {0};
    sprite.sprno = 500; sprite.x = 16; sprite.y = 24; sprite.z = .25f;
    sprite.zm_x = sprite.zm_y = sprite.u1 = sprite.v1 = sprite.trnsl = 1;
    sprite.attr = NLSPR_DISP_LT;
    assert(nlSprPut(&sprite) == 0);
}

static void guard_pitch(const uint32_t* frame, int width) {
    for (unsigned y = 0; y < RD_HEIGHT; ++y)
        for (unsigned x = (unsigned)width; x < PITCH; ++x)
            assert(frame[y * PITCH + x] == 0xdeadbeef);
}

static void draw_scene(MVC2HostObjectFile* model, uint32_t* frame,
                       int wide, const char* threads) {
    assert(!setenv("MVC2_RDTHREADS", threads, 1));
    rd_set_widescreen(wide);
    assert(rd_width() == (wide ? 854 : 640) && rd_height() == 480);
    _Alignas(32) MATRIX stack[9];
    assert(nlInitMatrix(stack, 8) == stack);
    nlInitLibrary(0, 0, 0);
    nlInitSprite(NULL, 0, NL_MAX_TEX);
    nlSpriteSetImaginaryScreenSize(640, 480);
    nlMatrixMode(3);
    nlPerspective(0x4000, 4.0f / 3.0f, 1, 100);
    nlMatrixMode(1);
    nlTranslate(0, 0, -6);
    nlMakeModelProjectMatrix();
    nl_tex_offset = 5;
    assert(!nlObjPutLoadTexture(model->obj[0], model->thd));
    for (size_t i = 0; i < (size_t)PITCH * RD_HEIGHT; ++i) frame[i] = 0xdeadbeef;
    rd_quality("exact");
    rd_begin_frame();
    rd_background(0xff223344);
    nlObjPut(model->obj[0]);
    assert(rd_frame->ntris[RD_OPAQUE] == 1);
    /* At z=-8 these two models project to logical x=-60 and x=700.
     * Their complete bounding spheres are outside the original frustum. */
    nlTranslate(-38.0f / 3.0f, 0, 0);
    nlObjPut(model->obj[0]);
    nlTranslate(76.0f / 3.0f, 0, 0);
    nlObjPut(model->obj[0]);
    assert(rd_frame->ntris[RD_OPAQUE] == (wide ? 3 : 1));
    hud_sprite();
    rd_end_frame(frame, PITCH);
    int margin = wide ? 107 : 0;
    assert(frame[240 * PITCH + 320 + margin] == 0xffff0000);
    /* HUD remains 32x8 pixels and stays in its original centred safe area. */
    for (int y = 24; y < 32; ++y)
        for (int x = 16; x < 48; ++x)
            assert(frame[y * PITCH + x + margin] == 0xff00ff00);
    assert(frame[24 * PITCH + 15 + margin] == 0xff223344);
    assert(frame[24 * PITCH + 48 + margin] == 0xff223344);
    if (wide) {
        assert(frame[240 * PITCH + 47] == 0xffff0000);
        assert(frame[240 * PITCH + 807] == 0xffff0000);
    }
    guard_pitch(frame, rd_width());
    nl_tex_free_all();
}

static void fade_covers_viewport(uint32_t* frame, const char* quality) {
    rd_set_widescreen(1);
    rd_quality(quality);
    rd_begin_frame();
    rd_background(0xff223344);
    NLVERTEX00 vertices[4] = {
        {0, 0, .1f, 0xff000000}, {0, 480, .1f, 0xff000000},
        {640, 0, .1f, 0xff000000}, {640, 480, .1f, 0xff000000}};
    nlDispStripInit();
    nlDispStripTrnsl(4, vertices);
    for (size_t i = 0; i < (size_t)PITCH * RD_HEIGHT; ++i) frame[i] = 0xdeadbeef;
    rd_end_frame(frame, PITCH);
    for (int y = 0; y < rd_height(); ++y)
        for (int x = 0; x < rd_width(); ++x)
            assert(frame[y * PITCH + x] == 0xff000000);
    guard_pitch(frame, rd_width());
    /* An undersized output stride is rejected without touching the caller. */
    frame[0] = 0xabcdef01;
    rd_end_frame(frame, RD_WIDTH);
    assert(frame[0] == 0xabcdef01);
}

static void artifact(const char* directory, const char* name,
                     const uint32_t* frame, int width) {
    if (!directory) return;
    char path[4096];
    assert(snprintf(path, sizeof path, "%s/%s", directory, name) < (int)sizeof path);
    FILE* file = fopen(path, "wb");
    assert(file);
    fprintf(file, "P6\n%d %d\n255\n", width, RD_HEIGHT);
    for (int y = 0; y < RD_HEIGHT; ++y)
        for (int x = 0; x < width; ++x) {
            uint32_t color = frame[y * PITCH + x];
            unsigned char rgb[3] = {(unsigned char)(color >> 16),
                                    (unsigned char)(color >> 8), (unsigned char)color};
            assert(fwrite(rgb, 1, 3, file) == 3);
        }
    assert(!fclose(file));
}

int main(int argc, char** argv) {
    MVC2NativeModel* owner = fixture();
    MVC2HostObjectFile* model = mvc2_native_model_file(owner);
    size_t bytes = (size_t)PITCH * RD_HEIGHT * sizeof(uint32_t);
    uint32_t* normal = malloc(bytes), *wide = malloc(bytes), *again = malloc(bytes);
    assert(normal && wide && again);
    draw_scene(model, normal, 0, "1");
    draw_scene(model, wide, 1, "1");
    for (int y = 0; y < RD_HEIGHT; ++y)
        assert(!memcmp(normal + y * PITCH, wide + y * PITCH + 107, RD_WIDTH * sizeof(uint32_t)));
    draw_scene(model, again, 0, "1");
    assert(!memcmp(normal, again, bytes));
    rd_soft_shutdown();
    for (int i = 0; i < 2; ++i) {
        draw_scene(model, again, 1, i ? "4" : "2");
        assert(!memcmp(wide, again, bytes));
        rd_soft_shutdown();
    }
    const char* directory = argc > 1 ? argv[1] : NULL;
    artifact(directory, "synthetic-native-4x3.ppm", normal, RD_WIDTH);
    artifact(directory, "synthetic-native-16x9.ppm", wide, RD_MAX_WIDTH);
    fade_covers_viewport(again, "exact");
    fade_covers_viewport(again, "half");
    rd_soft_shutdown();
    rd_set_widescreen(0);
    free(normal); free(wide); free(again); mvc2_native_model_free(owner);
    puts("Native widescreen: extra world, unchanged centre/HUD pixels, full-width fades, worker parity, mode switching and pitch guards passed.");
    return 0;
}
