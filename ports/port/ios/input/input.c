#include "input.h"
#include "naomi_input.h"

enum {
    MVC2_INPUT_LEVER = JVS_UP | JVS_DOWN | JVS_LEFT | JVS_RIGHT,
    MVC2_INPUT_ATTACKS = JVS_B1 | JVS_B2 | JVS_B3 | JVS_B4 | JVS_B5 | JVS_B6
};

#include <math.h>
#include <string.h>

_Static_assert(sizeof(mvc2_switch_state) == 20,
               "Native switch state must retain the game's scalar layout");

static int valid_slot(const mvc2_input_state *state, unsigned side,
                      unsigned source) {
    return state != NULL && side < MVC2_INPUT_SIDES &&
           source < MVC2_INPUT_SOURCES;
}

static uint16_t cleaned_mask(uint16_t mask) {
    mask &= JVS_START | MVC2_INPUT_LEVER | MVC2_INPUT_ATTACKS;
    if ((mask & (JVS_LEFT | JVS_RIGHT)) == (JVS_LEFT | JVS_RIGHT))
        mask &= (uint16_t)~(JVS_LEFT | JVS_RIGHT);
    if ((mask & (JVS_UP | JVS_DOWN)) == (JVS_UP | JVS_DOWN))
        mask &= (uint16_t)~(JVS_UP | JVS_DOWN);
    return mask;
}

void mvc2_input_init(mvc2_input_state *state) {
    if (state != NULL) memset(state, 0, sizeof(*state));
}

int mvc2_input_set(mvc2_input_state *state, unsigned side, unsigned source,
                   uint16_t buttons) {
    if (!valid_slot(state, side, source)) return 0;
    /* Clean opposing directions only after merging every source. */
    state->sources[side][source] = buttons & (JVS_START | MVC2_INPUT_LEVER | MVC2_INPUT_ATTACKS);
    return 1;
}

int mvc2_input_release_source(mvc2_input_state *state, unsigned side,
                              unsigned source) {
    return mvc2_input_set(state, side, source, 0);
}

void mvc2_input_release_all(mvc2_input_state *state) {
    if (state != NULL) memset(state->sources, 0, sizeof(state->sources));
}

void mvc2_input_begin_frame(mvc2_input_state *state) {
    if (state == NULL) return;
    for (unsigned side = 0; side < MVC2_INPUT_SIDES; ++side) {
        uint16_t merged = 0;
        mvc2_switch_state *sw = &state->switches[side];
        for (unsigned source = 0; source < MVC2_INPUT_SOURCES; ++source)
            merged |= state->sources[side][source];
        sw->old = sw->now;
        sw->now = cleaned_mask(merged);
        sw->on = sw->now & (uint16_t)~sw->old;
        sw->off = sw->old & (uint16_t)~sw->now;
        /* src/system.c:key_repeat: first repeat after 36 unchanged frames,
         * then every six. Preserve its counter behavior, including idle. */
        sw->repeat = 0;
        if (sw->old != sw->now) sw->repeat_count = 0;
        else {
            ++sw->repeat_count;
            if (sw->repeat_count > 35) {
                sw->repeat_count = 30;
                sw->repeat = sw->now;
            }
        }
    }
}

const mvc2_switch_state *mvc2_input_switches(const mvc2_input_state *state,
                                           unsigned side) {
    if (state == NULL || side >= MVC2_INPUT_SIDES) return NULL;
    return &state->switches[side];
}

int mvc2_input_insert_coin(mvc2_input_state *state, unsigned side) {
    if (state == NULL || side >= MVC2_INPUT_SIDES ||
        state->coin_pending[side] == UINT32_MAX) return 0;
    ++state->coin_pending[side];
    return 1;
}

uint32_t mvc2_input_take_coins(mvc2_input_state *state, unsigned side) {
    if (state == NULL || side >= MVC2_INPUT_SIDES) return 0;
    const uint32_t count = state->coin_pending[side];
    state->coin_pending[side] = 0;
    return count;
}

static float clampf(float value, float low, float high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

uint16_t mvc2_input_stick_buttons(float x, float y, float sensitivity,
                                 float dead_zone) {
    if (!isfinite(x) || !isfinite(y)) return 0;
    if (!isfinite(sensitivity)) sensitivity = 0.5f;
    if (!isfinite(dead_zone)) dead_zone = 0.15f;
    x = clampf(x, -1.0f, 1.0f);
    y = clampf(y, -1.0f, 1.0f);
    sensitivity = clampf(sensitivity, 0.0f, 1.0f);
    dead_zone = clampf(dead_zone, 0.0f, 0.95f);
    const float radius = hypotf(x, y);
    const float threshold = dead_zone + (1.0f - dead_zone) *
        (0.8f - 0.65f * sensitivity);
    if (radius < threshold) return 0;

    /* tan(pi/8): a 45-degree cardinal sector and 45-degree diagonal sector.
     * Use comparisons instead of a host-dependent atan rounding boundary. */
    const float sector_edge = 0.4142135623730950f;
    const float ax = fabsf(x), ay = fabsf(y);
    uint16_t result = 0;
    if (ax >= ay * sector_edge)
        result |= x < 0.0f ? JVS_LEFT : JVS_RIGHT;
    if (ay >= ax * sector_edge)
        result |= y < 0.0f ? JVS_UP : JVS_DOWN;
    return result;
}
