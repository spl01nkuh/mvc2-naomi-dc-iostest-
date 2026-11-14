/* Portable collection contract for the future iOS front end. The existing
 * native runtime consumes JVS masks through HOST_INPUT, not a game struct
 * overlay or a replacement player-input implementation. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "input/input.h"
#include "naomi_input.h"

enum { ATTACKS = JVS_B1 | JVS_B2 | JVS_B3 | JVS_B4 | JVS_B5 | JVS_B6 };

static void test_sources_and_edges(void) {
    mvc2_input_state state;
    mvc2_input_init(&state);
    assert(mvc2_input_set(&state, 0, 0, JVS_B1));
    assert(mvc2_input_set(&state, 0, 1, JVS_B1 | JVS_B2));
    assert(mvc2_input_set(&state, 1, 31, JVS_START | JVS_B6));
    mvc2_input_begin_frame(&state);
    assert(state.switches[0].now == (JVS_B1 | JVS_B2));
    assert(state.switches[0].on == (JVS_B1 | JVS_B2));
    assert(state.switches[0].old == 0 && state.switches[0].off == 0);
    assert(mvc2_input_release_source(&state, 0, 1));
    mvc2_input_begin_frame(&state);
    assert(state.switches[0].now == JVS_B1);
    assert(state.switches[0].off == JVS_B2);
    assert(state.switches[0].on == 0);
    mvc2_input_release_all(&state);
    mvc2_input_begin_frame(&state);
    assert(state.switches[0].now == 0 && state.switches[0].off == JVS_B1);
    assert(state.switches[1].off == (JVS_START | JVS_B6));
}

static void test_direction_conflicts(void) {
    mvc2_input_state state;
    mvc2_input_init(&state);
    mvc2_input_set(&state, 0, 0, JVS_LEFT | JVS_UP);
    mvc2_input_set(&state, 0, 1, JVS_RIGHT | JVS_B4);
    mvc2_input_begin_frame(&state);
    assert(state.switches[0].now == (JVS_UP | JVS_B4));
    mvc2_input_set(&state, 0, 1, JVS_DOWN | JVS_RIGHT | JVS_B4);
    mvc2_input_begin_frame(&state);
    assert(state.switches[0].now == JVS_B4);
    assert(state.switches[0].off == JVS_UP);
    mvc2_input_release_source(&state, 0, 1);
    mvc2_input_begin_frame(&state);
    assert(state.switches[0].now == (JVS_LEFT | JVS_UP));
    assert(state.switches[0].on == (JVS_LEFT | JVS_UP));
    mvc2_input_set(&state, 0, 0, 0xffff);
    mvc2_input_begin_frame(&state);
    assert(state.switches[0].now == (JVS_START | ATTACKS));
}

static void test_repeats(void) {
    mvc2_input_state state;
    mvc2_input_init(&state);
    mvc2_input_set(&state, 0, 0, JVS_DOWN);
    mvc2_input_begin_frame(&state);
    assert(state.switches[0].repeat_count == 0);
    for (unsigned unchanged = 1; unchanged < 36; ++unchanged) {
        mvc2_input_begin_frame(&state);
        assert(state.switches[0].repeat == 0);
    }
    mvc2_input_begin_frame(&state);
    assert(state.switches[0].repeat == JVS_DOWN);
    assert(state.switches[0].repeat_count == 30);
    for (unsigned tick = 0; tick < 5; ++tick) {
        mvc2_input_begin_frame(&state);
        assert(state.switches[0].repeat == 0);
    }
    mvc2_input_begin_frame(&state);
    assert(state.switches[0].repeat == JVS_DOWN);
    mvc2_input_set(&state, 0, 0, JVS_DOWN | JVS_B1);
    mvc2_input_begin_frame(&state);
    assert(state.switches[0].repeat_count == 0);
    assert(state.switches[0].on == JVS_B1);
}

static void test_stick(void) {
    assert(mvc2_input_stick_buttons(0, 0, 1, 0) == 0);
    assert(mvc2_input_stick_buttons(0.35f, 0, 0, .15f) == 0);
    assert(mvc2_input_stick_buttons(0.35f, 0, 1, .15f) == JVS_RIGHT);
    assert(mvc2_input_stick_buttons(-1, 0, .5f, .15f) == JVS_LEFT);
    assert(mvc2_input_stick_buttons(0, -1, .5f, .15f) == JVS_UP);
    assert(mvc2_input_stick_buttons(0, 1, .5f, .15f) == JVS_DOWN);
    assert(mvc2_input_stick_buttons(1, 1, .5f, .15f) == (JVS_RIGHT | JVS_DOWN));
    assert(mvc2_input_stick_buttons(-1, -1, .5f, .15f) == (JVS_LEFT | JVS_UP));
    assert(mvc2_input_stick_buttons(1, .2f, .5f, .15f) == JVS_RIGHT);
    assert(mvc2_input_stick_buttons(.2f, -1, .5f, .15f) == JVS_UP);
    assert(mvc2_input_stick_buttons(NAN, 0, .5f, .15f) == 0);
    assert(mvc2_input_stick_buttons(0, INFINITY, .5f, .15f) == 0);
    assert(mvc2_input_stick_buttons(4, 0, NAN, NAN) == JVS_RIGHT);
    assert(mvc2_input_stick_buttons(.3f, 0, 1, .35f) == 0);
}

static void test_invalid_and_coins(void) {
    mvc2_input_state state;
    mvc2_input_init(&state);
    assert(!mvc2_input_set(&state, 2, 0, JVS_START));
    assert(!mvc2_input_set(&state, 0, 32, JVS_START));
    assert(!mvc2_input_set(NULL, 0, 0, JVS_START));
    assert(mvc2_input_switches(&state, 2) == NULL);
    assert(mvc2_input_insert_coin(&state, 0));
    assert(mvc2_input_insert_coin(&state, 0));
    assert(mvc2_input_take_coins(&state, 0) == 2);
    assert(mvc2_input_take_coins(&state, 0) == 0);
    assert(mvc2_input_take_coins(&state, 1) == 0);
    state.coin_pending[0] = UINT32_MAX;
    assert(!mvc2_input_insert_coin(&state, 0));
    assert(!mvc2_input_insert_coin(&state, 2));
    mvc2_input_begin_frame(&state);
    assert(state.switches[0].now == 0);
    mvc2_input_release_all(NULL);
    mvc2_input_begin_frame(NULL);
}

int main(void) {
    test_sources_and_edges();
    test_direction_conflicts();
    test_repeats();
    test_stick();
    test_invalid_and_coins();
    puts("iOS input: JVS source merging, edges, repeats, stick and coin events pass");
    return 0;
}
