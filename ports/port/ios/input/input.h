/* Native input collection. Game masks match port/include/naomi_input.h.
 * The owner must serialize calls; the iOS UI wrapper supplies its lock.
 */
#ifndef MVC2_NATIVE_INPUT_H
#define MVC2_NATIVE_INPUT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { MVC2_INPUT_SIDES = 2, MVC2_INPUT_SOURCES = 32 };

typedef struct mvc2_switch_state {
    uint16_t now, old, on, off;
    uint16_t trigger_right, trigger_left;
    int16_t stick_x, stick_y;
    uint16_t repeat, repeat_count;
} mvc2_switch_state;

typedef struct mvc2_input_state {
    uint16_t sources[MVC2_INPUT_SIDES][MVC2_INPUT_SOURCES];
    mvc2_switch_state switches[MVC2_INPUT_SIDES];
    uint32_t coin_pending[MVC2_INPUT_SIDES];
} mvc2_input_state;

void mvc2_input_init(mvc2_input_state *state);
/* A source owns its whole mask: updating or releasing one source cannot
 * release a button held by another finger, a custom chord, or a controller.
 * Invalid sides/sources are rejected without modifying state. */
int mvc2_input_set(mvc2_input_state *state, unsigned side, unsigned source,
                   uint16_t buttons);
int mvc2_input_release_source(mvc2_input_state *state, unsigned side,
                              unsigned source);
/* Release sources while preserving the previous snapshot, so the next
 * begin_frame emits release edges. Use init to discard all history. */
void mvc2_input_release_all(mvc2_input_state *state);
/* Call exactly once per game frame. Input polling does not advance repeats. */
void mvc2_input_begin_frame(mvc2_input_state *state);
const mvc2_switch_state *mvc2_input_switches(const mvc2_input_state *state,
                                           unsigned side);

/* Coin insertion belongs to the credit system, not to the player switch
 * word. The front end consumes the queued count as individual HOST_INPUT.coin
 * frame events; a nonzero HOST_INPUT.coin adds one credit, not count credits. */
int mvc2_input_insert_coin(mvc2_input_state *state, unsigned side);
uint32_t mvc2_input_take_coins(mvc2_input_state *state, unsigned side);

/* Digital eight-way response for normalized analog axes: right and down are
 * positive. Sensitivity 0..1 lowers the activation threshold from 0.80 to
 * 0.15 of the travel outside the radial dead zone. Diagonals occupy 45-degree
 * sectors. NaN/Inf input is neutral; configuration values are clamped.
 * This affects activation travel, not the game's movement speed. */
uint16_t mvc2_input_stick_buttons(float x, float y, float sensitivity,
                                 float dead_zone);

#ifdef __cplusplus
}
#endif
#endif
