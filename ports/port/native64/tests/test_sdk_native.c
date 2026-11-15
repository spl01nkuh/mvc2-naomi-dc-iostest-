/* Deterministic SDK timing conversions and real model-backend initialization.
 * No game data and no replacement SDK entry points are linked. */
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sdk_native.h"
#include "nlsystem.h"
#include "host_matrix.h"
#include "rd_list.h"

/* A deterministic monotonic clock double; production links rd_posix.c. */
static uint64_t counter;
uint64_t rd_counter(void) { return counter; }

typedef struct { float x, y, z; } Vec3;
extern void nlObjPutSetGlobalAmbience(float ambience);
extern void nlObjPutSetLightDirectionV(const Vec3* direction);
extern MVC2HostMatrix* nlInitMatrix(MVC2HostMatrix* matrix, int count);
extern void nlObjPut(int* object);
extern float nl_cnv_proj_pers[16];

static void timing(void) {
    assert(nlSysMetricsCpuPowerGet() == 0);
    counter = UINT64_C(10000000000);
    nlSysMetricsInit();
    counter += 999;
    assert(nlSysMetricsCpuPowerGet() == 0);
    counter += 1;
    assert(nlSysMetricsCpuPowerGet() == 1);
    counter += UINT64_C(14999000);
    assert(nlSysMetricsCpuPowerGet() == 15000);
    assert(nlSysMetricsCpuPowerEnd() == 15000);
    counter += 1000;
    assert(nlSysMetricsCpuPowerEnd() == 15001);
    nlSysMetricsCpuPowerStart();
    assert(nlSysMetricsCpuPowerGet() == 0);
    counter += (uint64_t)INT32_MAX * 1000 + 1000;
    assert(nlSysMetricsCpuPowerGet() == INT32_MAX);
    for (int clock = -1; clock < 6; ++clock) {
        nlSysMetricsInitSetClock(clock);
        counter += 12345000;
        assert(nlSysMetricsCpuPowerGet() == 12345);
    }
}

static void angles(void) {
    assert(nlAtan256(0, 0) == 0);
    assert(nlAtan256(0, 1) == 0);
    assert(nlAtan256(1, 1) == 32);
    assert(nlAtan256(1, 0) == 64);
    assert(nlAtan256(1, -1) == 96);
    assert(nlAtan256(0, -1) == 128);
    assert(nlAtan256(-1, -1) == 160);
    assert(nlAtan256(-1, 0) == 192);
    assert(nlAtan256(-1, 1) == 224);
    assert(nlAtan256(-.001f, 1) == 256);
    assert(nlAtan256(NAN, 1) == 0);
    assert(nlAtan256(INFINITY, INFINITY) == 32);
    /* This boundary distinguishes the original rounded-ratio lookup from
     * simply rounding the continuous atan2 result. */
    assert(nlAtan256(1.4999f / 128, 1) == 0);
    assert(nlAtan256(1.5001f / 128, 1) == 1);
    for (int y = -256; y <= 256; ++y) {
        for (int x = -256; x <= 256; ++x) {
            if (!x && !y) continue;
            double expected = atan2((double)y, (double)x) * (128.0 / 3.14159265358979323846);
            double error = fabs((double)(nlAtan256((float)y, (float)x) & 255) - expected);
            error = fmod(error, 256.0);
            if (error > 128.0) error = 256.0 - error;
            assert(error < .7);
        }
    }
}

static void word(uint8_t* target, uint32_t value) { memcpy(target, &value, 4); }
static void scalar(uint8_t* target, float value, int tag) {
    uint32_t bits;
    memcpy(&bits, &value, 4);
    word(target, bits | (tag ? 1u : 0u));
}

static void draw_initialization(void) {
    _Alignas(32) MVC2HostMatrix stack[3];
    _Alignas(4) uint8_t object[212] = {0};
    uint8_t* group = object + 24;
    uint8_t* primitive = group + 80;
    assert(nlInitMatrix(stack, 2) == stack);
    memset(nl_cnv_proj_pers, 0, sizeof(float) * 16);
    nl_cnv_proj_pers[12] = 1; /* w = 1, screen x/y = model x/y */
    nl_cnv_proj_pers[1] = nl_cnv_proj_pers[6] = 1;
    word(group, 0x20); /* floating material color, untextured opaque */
    word(group + 4, (uint32_t)RD_ALWAYS << 29);
    word(group + 8, (uint32_t)RD_ONE << 29 | (uint32_t)RD_FOG_NONE << 22);
    /* Bounds enclosing the generated screen-space triangle, now required by
     * upstream's sphere culling before vertex/light processing. */
    scalar(group + 16, 60, 0);
    scalar(group + 20, 60, 0);
    scalar(group + 28, 60, 0);
    scalar(group + 40, 1, 0);
    for (unsigned i = 0; i < 4; ++i) scalar(group + 44 + 4 * i, 1, 0);
    word(group + 76, 104);
    word(primitive, 0x48); word(primitive + 4, 1);
    for (unsigned i = 0; i < 3; ++i) {
        uint8_t* vertex = primitive + 8 + i * 32;
        scalar(vertex, i == 1 ? 100 : 20, 1);
        scalar(vertex + 4, i == 2 ? 100 : 20, 0);
        scalar(vertex + 16, 1, 0); /* normal (0, 1, 0) */
    }
    Vec3 light = {0, 1, 0};
    nlObjPutSetLightDirectionV(&light);
    nlObjPutSetGlobalAmbience(0);
    rd_begin_frame();
    nlObjPut((int*)object);
    assert(rd_frame->ntris[RD_OPAQUE] == 1);
    assert(rd_frame->tris[RD_OPAQUE][0].v[0].base == 0xffffffff);

    NLSystemWork_.vertex_depth = 7;
    NLSystemWork_.znear = .3f;
    nlInitDrawModel(ScreenVertexBuffer, LightFragmentBuffer);
    assert(NLSystemWork_.vertex_depth == 0);
    assert(NLSystemWork_.znear == .3f);
    assert(NLSystemWork_.vertex_work == ScreenVertexBuffer);
    assert(NLSystemWork_.vertex_ptr == ScreenVertexBuffer);
    assert(NLSystemWork_.vertex_end_ptr == ScreenVertexBuffer + MVC2_NATIVE_SCREEN_WORK_FLOATS);
    assert(NLSystemWork_.lit_work == LightFragmentBuffer);
    assert(NLSystemWork_.lit_ptr == LightFragmentBuffer);
    assert(NLSystemWork_.lit_end_ptr == LightFragmentBuffer + MVC2_NATIVE_LIGHT_WORK_WORDS);
    nlInitDrawModel(NULL, NULL);
    nlLightInit();
    rd_begin_frame();
    nlObjPut((int*)object);
    assert(rd_frame->ntris[RD_OPAQUE] == 1);
    assert(rd_frame->tris[RD_OPAQUE][0].v[0].base == 0xff4d4d4d);
}

int main(void) {
    timing();
    angles();
    draw_initialization();
    puts("Native SDK timing, angle lookup and model/light initialization passed.");
    return 0;
}
