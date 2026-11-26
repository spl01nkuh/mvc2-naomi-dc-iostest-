/* The actual native game and software renderer, hosted without SDL or JIT. */
#include "runtime.h"
#include "engine_state.h"
#include "game_memory.h"
#include "naomi_host.h"
#include "rd.h"
#include "snd.h"
#include "../../ios/assets/mvc2_rom.h"

#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static pthread_mutex_t ownership_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t owner;
static int owner_set;
static int active;
static int has_frame;
static int wants_widescreen;
static int presented_width, presented_height;
static HOST_INPUT controls;
static uint32_t pixels[RD_MAX_WIDTH * RD_HEIGHT];
static const int16_t* audio_samples;
static int audio_frames;
static const char* frame_failure;
static char save_directory[PATH_MAX];
static char save_file[PATH_MAX];
static void* kept_block;
static size_t kept_size;

static int failure(char* error, size_t capacity, const char* message) {
    if (error && capacity) snprintf(error, capacity, "%s", message);
    return 0;
}

static int on_owner(int establish) {
    int result;
    pthread_mutex_lock(&ownership_lock);
    if (!owner_set && establish) {
        owner = pthread_self();
        owner_set = 1;
    }
    result = owner_set && pthread_equal(owner, pthread_self());
    pthread_mutex_unlock(&ownership_lock);
    return result;
}

int mvc2_gameplay_start(const char* rom, const char* saves, char* error, size_t capacity) {
    struct stat status;
    if (error && capacity) error[0] = 0;
    if (!on_owner(1)) return failure(error, capacity, "The native game must stay on its owning thread.");
    if (active) return failure(error, capacity, "Stop the current native game before starting another session.");
    if (!rom || !*rom || !saves || !*saves)
        return failure(error, capacity, "The imported ROM and save directories are required.");
    if (strlen(saves) >= sizeof save_directory - 128 || stat(saves, &status) || !S_ISDIR(status.st_mode))
        return failure(error, capacity, "The native game could not open its save directory.");
    /* This runs on the gameplay thread, including on restored imports. A stale
     * manifest must never send corrupt or incomplete assets into the game. */
    if (!mvc2_rom_validate_directory(rom, error, capacity)) return 0;
    if (!mvc2_engine_checkpoint())
        return failure(error, capacity, "The native engine could not preserve its initial state.");
    memcpy(save_directory, saves, strlen(saves) + 1);
    memset(&controls, 0, sizeof controls);
    has_frame = audio_frames = 0;
    presented_width = presented_height = 0;
    audio_samples = NULL;
    rd_quality("exact");
    rd_set_widescreen_preference(wants_widescreen);
    /* The cartridge reader can retain the entire 136 MiB image on desktop.
     * Keep a bounded working set on iOS as stage and character loads overlap. */
    naomi_rom_cache_bytes = 32u << 20;
    host_unlock = 1;
    rd_begin_frame();
    rd_background(UINT32_C(0xff000000));
    if (!naomi_init(&arc_program, rom)) {
        board_stop();
        rd_soft_shutdown();
        mvc2_game_memory_close();
        mvc2_engine_restore();
        save_directory[0] = 0;
        return failure(error, capacity, "The native engine could not load this NAOMI cartridge's program data.");
    }
    active = 1;
    host_log("native iOS gameplay: upstream 0.2.1, linked ARM64 C, software rendering");
    return 1;
}

int mvc2_gameplay_step(const HOST_INPUT* input, char* error, size_t capacity) {
    if (error && capacity) error[0] = 0;
    if (!on_owner(0)) return failure(error, capacity, "A native frame was requested from another thread.");
    if (!active) return failure(error, capacity, "The native game has not been started.");
    controls = input ? *input : (HOST_INPUT){0};
    has_frame = 0;
    audio_frames = 0;
    audio_samples = NULL;
    frame_failure = NULL;
    naomi_run_frame();
    if (frame_failure) return failure(error, capacity, frame_failure);
    snd_prepare();
    audio_frames = snd_frame(&audio_samples);
    if (!has_frame) return failure(error, capacity, "The native game did not present a frame.");
    return 1;
}

void mvc2_gameplay_fail_frame(const char* message) {
    if (!frame_failure) frame_failure = message;
}

const uint32_t* mvc2_gameplay_frame(void) {
    return on_owner(0) && active && has_frame ? pixels : NULL;
}

void mvc2_gameplay_set_widescreen(int enabled) {
    if (!on_owner(0)) return;
    wants_widescreen = enabled != 0;
    if (active) rd_set_widescreen_preference(wants_widescreen);
}

int mvc2_gameplay_frame_width(void) {
    return on_owner(0) && active && has_frame ? presented_width : 0;
}

int mvc2_gameplay_frame_height(void) {
    return on_owner(0) && active && has_frame ? presented_height : 0;
}

int mvc2_gameplay_audio(const int16_t** output) {
    if (output) *output = NULL;
    if (!output || !on_owner(0) || !active) return 0;
    *output = audio_samples;
    return audio_frames;
}

void mvc2_gameplay_stop(void) {
    if (!on_owner(0) || !active) return;
    snd_close();
    board_stop();
    rd_soft_shutdown();
    mvc2_game_memory_close();
    mvc2_engine_restore();
    active = has_frame = audio_frames = 0;
    presented_width = presented_height = 0;
    audio_samples = NULL;
    memset(&controls, 0, sizeof controls);
    save_directory[0] = save_file[0] = 0;
    free(kept_block);
    kept_block = NULL;
    kept_size = 0;
}

const HOST_INPUT* host_input(void) { return &controls; }

void host_present(void) {
    presented_width = rd_width();
    presented_height = rd_height();
    rd_end_frame(pixels, presented_width);
    has_frame = 1;
    rd_begin_frame();
}

const char* host_save_path(const char* name) {
    if (!save_directory[0] || !name || !*name || strchr(name, '/') || strchr(name, '\\') || strstr(name, ".."))
        return NULL;
    int length = snprintf(save_file, sizeof save_file, "%s/%s", save_directory, name);
    return length > 0 && (size_t)length < sizeof save_file ? save_file : NULL;
}

void* host_keep_block(size_t size) {
    if (!size || size > (32u << 20)) return NULL;
    if (kept_block && kept_size == size) return kept_block;
    void* replacement = calloc(1, size);
    if (!replacement) return NULL;
    free(kept_block);
    kept_block = replacement;
    kept_size = size;
    return kept_block;
}
