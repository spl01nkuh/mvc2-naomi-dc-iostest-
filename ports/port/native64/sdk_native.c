#ifndef HOST_NATIVE64
#error sdk_native.c is for the native engine only
#endif

#include "sdk_native.h"
#include "nlsystem.h"
#include "rd_posix.h"

#include <limits.h>
#include <math.h>

_Alignas(64) float ScreenVertexBuffer[MVC2_NATIVE_SCREEN_WORK_FLOATS];
_Alignas(64) int32_t LightFragmentBuffer[MVC2_NATIVE_LIGHT_WORK_WORDS];

/* Implemented beside the renderer's typed scratch and lighting state. */
extern void nl_native_reset_draw_work(void);
extern void nl_native_reset_light(void);

static uint64_t metrics_start;
static int metrics_ready;

void nlSysMetricsInit(void) {
    metrics_start = rd_counter();
    metrics_ready = 1;
}

void nlSysMetricsInitSetClock(int32_t clock) {
    /* The SDK clock chooses a hardware tick resolution, but Get returns
     * microseconds at every setting. The host monotonic clock has a fixed
     * nanosecond unit and needs no clock-dependent conversion. */
    (void)clock;
    nlSysMetricsInit();
}

void nlSysMetricsCpuPowerStart(void) {
    nlSysMetricsInit();
}

int32_t nlSysMetricsCpuPowerGet(void) {
    uint64_t elapsed;
    if (!metrics_ready) {
        nlSysMetricsInit();
        return 0;
    }
    elapsed = (rd_counter() - metrics_start) / UINT64_C(1000);
    return elapsed > INT32_MAX ? INT32_MAX : (int32_t)elapsed;
}

int32_t nlSysMetricsCpuPowerEnd(void) {
    /* Like the SDK, End samples without stopping or restarting the timer. */
    return nlSysMetricsCpuPowerGet();
}

/* Transcribed from src/lib/libnaomi/nlmatha.src. atan2f followed by rounding
 * is observably different near the SDK table's quantization boundaries. */
static const uint8_t atan256_table[129] = {
    0,0,1,1,1,2,2,2,3,3,3,3,4,4,4,5,
    5,5,6,6,6,7,7,7,8,8,8,8,9,9,9,10,
    10,10,11,11,11,11,12,12,12,13,13,13,13,14,14,14,
    15,15,15,15,16,16,16,17,17,17,17,18,18,18,18,19,
    19,19,19,20,20,20,20,21,21,21,21,22,22,22,22,23,
    23,23,23,23,24,24,24,24,25,25,25,25,25,26,26,26,
    26,26,27,27,27,27,27,28,28,28,28,28,29,29,29,29,
    29,29,30,30,30,30,30,31,31,31,31,31,31,32,32,32,
    32
};

int32_t nlAtan256(float y, float x) {
    static const uint16_t octant[8] = {
        0x0000, 0x0101, 0x0081, 0x0080,
        0x0201, 0x0100, 0x0180, 0x0181
    };
    float ay = fabsf(y), ax = fabsf(x), ratio;
    unsigned sector = (y < 0.0f ? 4u : 0u) | (x < 0.0f ? 1u : 0u);
    unsigned index;
    int32_t angle;
    uint16_t base;
    if (isnan(x) || isnan(y) || (ax == 0.0f && ay == 0.0f)) return 0;
    if (ay > ax) {
        ratio = ax / ay;
        sector |= 2;
    } else {
        ratio = ay / ax;
    }
    /* The finite directions are also well-defined when either coordinate
     * is infinite; two infinities describe a diagonal. */
    if (isinf(ax) && isinf(ay)) ratio = 1.0f;
    index = (unsigned)(ratio * 128.0f + 0.5f);
    if (index > 128) index = 128;
    angle = atan256_table[index];
    base = octant[sector];
    if (base & 1) angle = -angle;
    /* The original may return 256 on the fourth quadrant's x axis. Callers
     * already mask to a byte; retain its result rather than canonicalize. */
    return (int32_t)(base >> 1) + angle;
}

void nlInitDrawModel(float* draw_vertex_work, int32_t* draw_light_work) {
    if (!draw_vertex_work) draw_vertex_work = ScreenVertexBuffer;
    if (!draw_light_work) draw_light_work = LightFragmentBuffer;
    NLSystemWork_.vertex_depth = 0;
    NLSystemWork_.vertex_work = draw_vertex_work;
    NLSystemWork_.vertex_ptr = draw_vertex_work;
    NLSystemWork_.vertex_end_ptr = draw_vertex_work + MVC2_NATIVE_SCREEN_WORK_FLOATS;
    NLSystemWork_.lit_work = draw_light_work;
    NLSystemWork_.lit_ptr = draw_light_work;
    NLSystemWork_.lit_end_ptr = draw_light_work + MVC2_NATIVE_LIGHT_WORK_WORDS;
    nl_native_reset_draw_work();
}

void nlLightInit(void) {
    nl_native_reset_light();
}
