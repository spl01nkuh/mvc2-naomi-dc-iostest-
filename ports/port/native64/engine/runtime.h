#ifndef MVC2_NATIVE_GAMEPLAY_RUNTIME_H
#define MVC2_NATIVE_GAMEPLAY_RUNTIME_H

#include <stddef.h>
#include <stdint.h>
#include "naomi_input.h"

#ifdef __cplusplus
extern "C" {
#endif

/* All calls belong to one permanent OS thread, including stop and restart.
 * Start and step return 1 on success, 0 with a terminated error on failure.
 * ROM files are imported assets; execution always uses the linked native C.
 * The save directory must already exist. */
int mvc2_gameplay_start(const char* rom_directory, const char* save_directory,
                        char* error, size_t capacity);
int mvc2_gameplay_step(const HOST_INPUT* input, char* error, size_t capacity);
/* Owning thread, between steps. Expands the camera view horizontally; keeps
 * the game's original HUD safe area and pixel scale. Applied at next step. */
void mvc2_gameplay_set_widescreen(int enabled);
/* Borrowed numeric 0xAARRGGBB pixels, tightly packed at frame_width() stride,
 * valid until the next step/stop. Dimensions are zero when no frame exists. */
const uint32_t* mvc2_gameplay_frame(void);
int mvc2_gameplay_frame_width(void);
int mvc2_gameplay_frame_height(void);
/* Borrowed interleaved stereo PCM at 44100 Hz; returns sample frames. */
int mvc2_gameplay_audio(const int16_t** output);
void mvc2_gameplay_stop(void);

#ifdef __cplusplus
}
#endif
#endif
