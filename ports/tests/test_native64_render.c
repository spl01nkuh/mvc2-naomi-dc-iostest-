/* End-to-end generated model -> original texture/projection/model backend ->
 * real software-rendered frame. This fixture contains no MVC2 game assets. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "model.h"
#include "host_matrix.h"
#include "host_memory.h"
#include "model_store.h"
#include "rd_list.h"
#include "load.h"

extern MVC2HostMatrix* nlInitMatrix(MVC2HostMatrix* stack, int count);
extern void nlMatrixMode(int mode);
extern MVC2HostMatrix* nlGetMatrix(MVC2HostMatrix* matrix);
extern void nlLoadMatrix(const MVC2HostMatrix* matrix);
extern void nlTranslate(float x, float y, float z);
extern void nlScale(float x, float y, float z);
extern void nlInitLibrary(void);
extern void nlFrustum(float left, float right, float bottom, float top, float near, float far);
extern void nlMakeModelProjectMatrix(void);
extern void nlMakeModelViewMatrix(void);
extern void nlMakeModelViewProjectMatrix(void);
extern void nlMakeProjConvMat(MVC2HostMatrix* matrix);
extern void nlMakeFastModelProjectMatrix(void);
extern void nlMakeFastModelProjectMatrixVM(void);
extern float nl_projectMatrix[16];
extern MVC2HostMatrix nl_projConvMat;
typedef struct NLVECTOR2 { float x, y; } NLVECTOR2;
typedef struct NLVECTOR3 { float x, y, z; } NLVECTOR3;
extern void nlProjectScreen(NLVECTOR3* position, NLVECTOR2* screen);
extern void nlProjectScreen3D(NLVECTOR3* position, NLVECTOR3* view);
extern void nlScreenPoint(float x, float y, float z, float* sx, float* sy, float* sz);
extern void nlFastScreenPoint(NLVECTOR3* position, float* screen);
extern int nlObjPutLoadTexture(int* object, const NLthd* headers);
extern void nlObjPut(int* object);
extern void tex_load_0e(void);
extern void tex_set_select(void);
MVC2HostObjectFile* obj_dm0a;
MVC2HostObjectFile* obj_dm01;
static const char* frame_failure;
static char frame_failure_text[512];

void mvc2_gameplay_fail_frame(const char* message) {
    snprintf(frame_failure_text, sizeof frame_failure_text, "%s", message);
    frame_failure = frame_failure_text;
}

enum { MODEL_BYTES = 280, OBJECT = 64, MESH = OBJECT + 24, PRIM = MESH + 80,
       BASE = 0x0c600000, PIXELS = 0x0d000000, PITCH = RD_WIDTH + 8 };
static void word(uint8_t* bytes, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[i] = (uint8_t)(value >> (i * 8));
}
static void scalar(uint8_t* bytes, float value, int tag) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    word(bytes, tag ? bits | 1u : bits);
}
static MVC2NativeModel* fixture(void) {
    uint8_t bytes[MODEL_BYTES] = {0}, pixels[8];
    word(bytes, BASE + 16); word(bytes + 4, 1); word(bytes + 8, BASE + 24);
    word(bytes + 16, BASE + OBJECT);
    bytes[24] = bytes[26] = 2; /* 2x2 RGB565 linear texture */
    word(bytes + 28, 0x901); word(bytes + 32, PIXELS);
    word(bytes + MESH, 0x28); /* opaque, float material, textured */
    word(bytes + MESH + 4, (uint32_t)RD_ALWAYS << 29);
    word(bytes + MESH + 8, (uint32_t)RD_ONE << 29 | (uint32_t)RD_FOG_NONE << 22);
    word(bytes + MESH + 12, (uint32_t)RD_RGB565 << 27);
    /* Upstream 0.2.1 tests the serialized sphere before emitting a group.
     * This sphere encloses the triangle at z=-2, rather than the old fixture's
     * zero-radius sphere at the camera origin. */
    scalar(bytes + MESH + 24, -2, 0);
    scalar(bytes + MESH + 28, 1, 0);
    bytes[MESH + 36] = 0xff; /* unlit */
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
    for (unsigned i = 0; i < 4; ++i) pixels[i * 2] = 0, pixels[i * 2 + 1] = 0xf8;
    MVC2NativeModel* model = NULL;
    assert(mvc2_native_model_decode(bytes, sizeof bytes, pixels, sizeof pixels, &model) == MVC2_NATIVE_MODEL_OK);
    void* raw = host_native64_ram_pointer(BASE, sizeof bytes);
    void* texture = host_native64_ram_pointer(PIXELS, sizeof pixels);
    memcpy(raw, bytes, sizeof bytes);
    memcpy(texture, pixels, sizeof pixels);
    assert(mvc2_model_store_written(raw, sizeof bytes));
    assert(mvc2_model_store_get(raw) == NULL); /* texture file is not registered yet */
    assert(mvc2_model_store_written(texture, sizeof pixels));
    return model;
}
static void quad(float size, float depth, uint32_t color, int translucent) {
    RD_STATE state = {0};
    state.list = translucent ? RD_TRANS : RD_OPAQUE;
    state.depth = RD_GREATER;
    state.zwrite = !translucent;
    state.src = translucent ? RD_SRC_ALPHA : RD_ONE;
    state.dst = translucent ? RD_INV_SRC_ALPHA : RD_ZERO;
    state.use_alpha = 1;
    state.fog = RD_FOG_NONE;
    RD_VTX vertices[4] = {{0}};
    vertices[0].x = vertices[3].x = 20;
    vertices[1].x = vertices[2].x = size;
    vertices[0].y = vertices[1].y = 20;
    vertices[2].y = vertices[3].y = size;
    for (unsigned i = 0; i < 4; ++i) vertices[i].z = depth, vertices[i].base = color;
    rd_quad(&state, vertices);
}
static void close_to(float actual, float expected) {
    assert(fabsf(actual - expected) < .001f);
}
static void projection(void) {
    _Alignas(32) MVC2HostMatrix stack[9];
    assert(nlInitMatrix(stack, 8) == stack);
    nlInitLibrary();
    nlMatrixMode(3);
    nlFrustum(-1, 1, -.75f, .75f, 1, 100);
    nlMatrixMode(2);
    nlTranslate(-1, 2, -4);
    nlMatrixMode(1);
    nlTranslate(2, -3, -1);
    nlScale(2, 3, 4);
    MVC2HostMatrix model;
    nlGetMatrix(&model);
    NLVECTOR3 point = {.25f, .5f, -.5f}, view;
    /* The model maps the point to (2.5, -1.5, -3); the view then
     * translates it to (1.5, .5, -7). Both stack indices and the
     * multiplication order matter for these nonidentity transforms. */
    nlMakeModelViewMatrix();
    nlProjectScreen3D(&point, &view);
    close_to(view.x, 1.5f); close_to(view.y, .5f); close_to(view.z, -7);
    nlMakeModelViewProjectMatrix();
    NLVECTOR2 screen;
    nlProjectScreen(&point, &screen);
    close_to(screen.x, 320 + 480.0f / 7);
    close_to(screen.y, 240 - 160.0f / 7);
    nlMakeProjConvMat(&nl_projConvMat);
    float raster[3];
    nlScreenPoint(point.x, point.y, point.z, raster, raster + 1, raster + 2);
    close_to(raster[0], screen.x); close_to(raster[1], screen.y); close_to(raster[2], 1.0f / 7);
    nlMakeFastModelProjectMatrixVM();
    nlFastScreenPoint(&point, raster);
    close_to(raster[0], screen.x); close_to(raster[1], screen.y); close_to(raster[2], 1.0f / 7);
    nlLoadMatrix(&model);
    nlMakeFastModelProjectMatrix();
    nlFastScreenPoint(&point, raster);
    close_to(raster[0], 320 + 800.0f / 3); close_to(raster[1], 400); close_to(raster[2], 1.0f / 3);
}
static void render(MVC2HostObjectFile* model, uint32_t* frame, const char* threads) {
    assert(setenv("MVC2_RDTHREADS", threads, 1) == 0);
    _Alignas(32) MVC2HostMatrix stack[9];
    assert(nlInitMatrix(stack, 8) == stack);
    nlInitLibrary();
    nlMatrixMode(3);
    nlFrustum(-1, 1, -.75f, .75f, 1, 100);
    nlGetMatrix(NULL);
    nlMatrixMode(1);
    /* This uses projection stack[3]; a serialized 12-byte stack entry has
     * the wrong stride on ARM64 and used to read an unrelated pointer. */
    nlMakeModelProjectMatrix();
    assert(nl_projectMatrix[0] == 1 && nl_projectMatrix[11] == -1);
    int* object = model->obj[0];
    nl_tex_offset = 5;
    nlObjPutLoadTexture(object, model->thd);
    assert(nl_tex[5].loaded);
    for (size_t i = 0; i < (size_t)PITCH * RD_HEIGHT; ++i) frame[i] = 0xdeadbeef;
    rd_quality("exact");
    rd_begin_frame();
    rd_background(0xff112233);
    nlObjPut(object);
    assert(rd_frame->ntris[RD_OPAQUE] == 1);
    /* The same bounds translated out of view must be culled before the
     * software renderer receives another triangle. */
    nlTranslate(10000, 0, 0);
    nlObjPut(object);
    assert(rd_frame->ntris[RD_OPAQUE] == 1);
    nlTranslate(-10000, 0, 0);
    quad(110, 1, 0xffff0000, 0);
    quad(100, 2, 0xff00ff00, 0);
    quad(90, 3, 0x800000ff, 1);
    rd_end_frame(frame, PITCH);
    assert(frame[240 * PITCH + 320] == 0xffff0000); /* decoded 3D texture */
    assert(frame[470 * PITCH + 630] == 0xff112233);
    uint32_t blend = frame[50 * PITCH + 50];
    assert((blend & 0xff0000) == 0 && (blend >> 8 & 255) >= 126 && (blend & 255) >= 127);
    for (unsigned y = 0; y < RD_HEIGHT; ++y)
        for (unsigned x = RD_WIDTH; x < PITCH; ++x) assert(frame[y * PITCH + x] == 0xdeadbeef);
    rd_soft_shutdown();
    rd_soft_shutdown();
    nl_tex_free_all();
}
static void packed_notice(const uint8_t* model_file, uint32_t* frame, const uint32_t* expected) {
    /* A literal 16-bit texel plus a 3-word repeat, then the explicit end. */
    const uint8_t valid[] = {0, 0x60, 0, 0xf8, 1, 0x18, 0, 0, 0, 0};
    uint8_t* model = host_native64_ram_pointer(DM0A_OBJ_ADRS, MODEL_BYTES);
    uint8_t* pack = host_native64_ram_pointer(DM0A_TEX_ADRS, sizeof valid);
    assert(model && pack);
    memcpy(model, model_file, MODEL_BYTES);
    /* The serialized texture address deliberately identifies no RAM file.
     * The notice load should bind the unpacked bytes without changing it. */
    word(model + 32, UINT32_C(0x10000000));
    assert(mvc2_model_store_written(model, MODEL_BYTES));
    assert(!mvc2_model_store_get(model));
    memcpy(pack, valid, sizeof valid);
    assert(mvc2_model_store_written(pack, sizeof valid));
    tex_load_0e();
    MVC2HostObjectFile* notice = obj_dm0a;
    assert(notice && notice->cnt == 1);
    assert(model[35] == 0x10);
    assert(!memcmp(pack, valid, sizeof valid));
    render(notice, frame, "2");
    assert(!memcmp(frame, expected, (size_t)PITCH * RD_HEIGHT * sizeof(uint32_t)));
    tex_load_0e();
    assert(obj_dm0a == notice);
    /* A truncated terminator or a repeat before the first literal must
     * decline the notice without invalidating its prior decoded owner. */
    assert(mvc2_model_store_written(pack, sizeof valid - 2));
    tex_load_0e();
    assert(!obj_dm0a);
    pack[1] = 0xe0;
    pack[2] = 1;
    pack[3] = 0x08;
    assert(mvc2_model_store_written(pack, sizeof valid));
    tex_load_0e();
    assert(!obj_dm0a);
    render(notice, frame, "1");
    assert(!memcmp(frame, expected, (size_t)PITCH * RD_HEIGHT * sizeof(uint32_t)));
}

static void selection_textures(const uint8_t* model_file) {
    enum { OBJECTS = 181, TEXTURES = 7, HEADERS = 16 + OBJECTS * 4,
           SELECT_OBJECT = HEADERS + (TEXTURES + 1) * 16, OBJECT_BYTES = MODEL_BYTES - OBJECT,
           SELECT_BYTES = SELECT_OBJECT + TEXTURES * OBJECT_BYTES };
    uint8_t* model = host_native64_ram_pointer(DM01_OBJ_ADRS, SELECT_BYTES);
    uint8_t* bank = host_native64_ram_pointer(DM01_TEX_ADRS, 8);
    uint8_t* atlas = host_native64_ram_pointer(0x0cdb0000, 0x20000);
    uint8_t* portrait = host_native64_ram_pointer(0x0cdf4800, 0x4800);
    uint8_t* names = host_native64_ram_pointer(0x0c720000, 0x20000);
    uint8_t* ram_end = host_native64_ram_pointer(0x0dfffff8, 8);
    assert(model && bank && atlas && portrait && names && ram_end);
    memset(model, 0, SELECT_BYTES);
    word(model, DM01_OBJ_ADRS + 16);
    word(model + 4, OBJECTS);
    word(model + 8, DM01_OBJ_ADRS + HEADERS);
    for (unsigned i = 0; i < OBJECTS; ++i)
        word(model + 16 + i * 4, DM01_OBJ_ADRS + SELECT_OBJECT + (i % TEXTURES) * OBJECT_BYTES);
    for (unsigned i = 0; i < TEXTURES; ++i) {
        uint8_t* object = model + SELECT_OBJECT + i * OBJECT_BYTES;
        memcpy(object, model_file + OBJECT, OBJECT_BYTES);
        word(object + MESH - OBJECT + 32, i);
    }
    word(model + HEADERS, 256 | 256u << 16);
    word(model + HEADERS + 4, 0x901);
    word(model + HEADERS + 8, 0x0cdb0000);
    word(model + HEADERS + 16, 2 | 2u << 16);
    word(model + HEADERS + 20, 0x901);
    word(model + HEADERS + 24, DM01_TEX_ADRS);
    word(model + HEADERS + 32, 96 | 96u << 16);
    word(model + HEADERS + 36, 0xb01);
    word(model + HEADERS + 40, 0x0cdf4800);
    /* Name pictures use a different generated-RAM region than the faces.
       The board's cached/uncached aliases must share those same bytes. */
    for (unsigned i = 3; i < 6; ++i) {
        word(model + HEADERS + i * 16, 256 | 256u << 16);
        word(model + HEADERS + i * 16 + 4, 0x901);
        word(model + HEADERS + i * 16 + 8,
             0x0c720000u | (i == 4 ? 0x80000000u : i == 5 ? 0xa0000000u : 0));
    }
    word(model + HEADERS + 6 * 16, 2 | 2u << 16);
    word(model + HEADERS + 6 * 16 + 4, 0x901);
    word(model + HEADERS + 6 * 16 + 8, 0x0dfffff8);
    assert(mvc2_model_store_written(model, SELECT_BYTES));
    assert(mvc2_model_store_written(bank, 8));
    assert(!mvc2_model_store_get(model)); /* one ROM extent cannot resolve the live portraits */

    tex_set_select();
    assert(!frame_failure && obj_dm01 && obj_dm01->cnt == OBJECTS);
    assert(obj_dm01->thd[0].address == atlas);
    assert(obj_dm01->thd[1].address == bank);
    assert(obj_dm01->thd[2].address == portrait);
    for (unsigned i = 3; i < 6; ++i) assert(obj_dm01->thd[i].address == names);
    assert(obj_dm01->thd[6].address == ram_end);
    for (unsigned i = 0; i < TEXTURES; ++i) assert(nl_tex[0x810 + i].loaded);
    assert(nl_tex[0x910].loaded);
    atlas[0] = 0x9a;
    assert(*(uint8_t*)obj_dm01->thd[0].address == 0x9a);
    names[0] = 0x4c;
    for (unsigned i = 3; i < 6; ++i) assert(*(uint8_t*)obj_dm01->thd[i].address == 0x4c);
    assert(!*mvc2_model_store_last_error());
    MVC2HostObjectFile* selected = obj_dm01;
    tex_set_select();
    assert(!frame_failure && obj_dm01 == selected);

    /* A texture that would straddle the end of its backing RAM is rejected
       before the first selection effect can dereference the model. */
    word(model + HEADERS + 6 * 16 + 8, 0x0dfffffa);
    assert(mvc2_model_store_written(model, SELECT_BYTES));
    tex_set_select();
    assert(!obj_dm01 && frame_failure && strstr(frame_failure, "character selection"));
    assert(strstr(mvc2_model_store_last_error(), "0D0C6000"));
    assert(strstr(mvc2_model_store_last_error(), "serialized reference"));
    assert(selected->thd[2].address == portrait);
    nl_tex_free_all();
}

int main(void) {
    projection();
    MVC2NativeModel* model = fixture();
    size_t bytes = (size_t)PITCH * RD_HEIGHT * sizeof(uint32_t);
    uint32_t* serial = malloc(bytes);
    uint32_t* parallel = malloc(bytes);
    assert(serial && parallel);
    render(mvc2_native_model_file(model), serial, "1");
    render(mvc2_native_model_file(model), parallel, "4");
    assert(memcmp(serial, parallel, bytes) == 0);
    void* raw = host_native64_ram_pointer(BASE, MODEL_BYTES);
    MVC2HostObjectFile* stored = mvc2_model_store_get(raw);
    assert(stored && stored->cnt == 1);
    render(stored, parallel, "2");
    assert(memcmp(serial, parallel, bytes) == 0);
    assert(mvc2_model_store_get(raw) == stored);
    word((uint8_t*)raw + 4, UINT32_MAX);
    assert(mvc2_model_store_written(raw, MODEL_BYTES));
    assert(mvc2_model_store_get(raw) == NULL);
    render(stored, parallel, "1"); /* a failed refresh retains its previous owner */
    assert(memcmp(serial, parallel, bytes) == 0);
    word((uint8_t*)raw + 4, 1);
    assert(mvc2_model_store_written(raw, MODEL_BYTES));
    assert(mvc2_model_store_get(raw) == stored);
    packed_notice(raw, parallel, serial);
    selection_textures(raw);
    assert(mvc2_model_store_written(host_native64_ram_pointer(PIXELS, 8), 8));
    assert(mvc2_model_store_get(raw) == stored); /* stable descriptor after texture refresh */
    assert(mvc2_model_store_written((uint8_t*)raw + 1, 1));
    assert(mvc2_model_store_get(raw) == NULL); /* overlapping writes invalidate whole files */
    assert(mvc2_model_store_written(raw, MODEL_BYTES));
    assert(mvc2_model_store_get(raw) == stored);
    assert(!mvc2_model_store_written(NULL, 1));
    assert(!mvc2_model_store_written(host_native64_ram + MVC2_NATIVE_RAM_SIZE, 1));
    mvc2_model_store_reset();
    mvc2_model_store_reset();
    assert(mvc2_model_store_get(raw) == NULL);
    free(serial); free(parallel); mvc2_native_model_free(model);
    puts("Native model rendering: real projection/texture/raster path, depth, blend, pitch and worker restart passed.");
    return 0;
}
