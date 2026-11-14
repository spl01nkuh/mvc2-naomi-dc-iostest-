/* Native board lifecycle fixture, not a replacement game. Exercises actual
 * coroutine/frame/input/renderer/cleanup implementations with owned RAM.
 * The two host_* functions here implement the front-end contract under test. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "naomi_host.h"
#include "naomi_input.h"
#include "host_memory.h"
#include "game_memory.h"
#include "rd.h"

extern void nlWaitVSync(void);
extern char nlaJAMMAInput[31][512];
static HOST_INPUT controls;
static uint32_t frame[RD_WIDTH * RD_HEIGHT];
static unsigned presented, stopped, hooked;

const HOST_INPUT* host_input(void) { return &controls; }
void host_present(void) {
    rd_end_frame(frame, RD_WIDTH);
    assert(frame[0] == (0xff000000u | host_frame()));
    assert(frame[RD_WIDTH * RD_HEIGHT - 1] == frame[0]);
    ++presented;
}
static void stop_hook(void) { ++stopped; }
static void frame_hook(void) { ++hooked; }
static void entry(void) {
    uint32_t* counter = host_native64_ram_pointer(0x0c420000, 4);
    assert(counter);
    for (;;) {
        assert(host_frame() >= 1);
        assert((uint8_t)nlaJAMMAInput[1][2] == controls.player[0] >> 8);
        assert((uint8_t)nlaJAMMAInput[1][3] == (controls.player[0] & 255));
        ++*counter;
        rd_begin_frame();
        rd_background(0xff000000u | host_frame());
        nlWaitVSync();
    }
}
int main(void) {
    assert(naomi_map_ram());
    mvc2_game_memory_reset();
    naomi_run_frame(); /* a stopped native board cannot jump through NULL */
    assert(host_frame() == 0);
    HOST_PROGRAM invalid = {0};
    assert(!board_start(&invalid));
    HOST_RELOC relocations[] = {{(char*)frame, (char*)frame}, {NULL, NULL}};
    invalid.main = entry;
    invalid.reloc = relocations;
    assert(!board_start(&invalid)); /* no truncating relocation stores */

    const HOST_PROGRAM program = {entry, NULL, {NULL, NULL, NULL, NULL}};
    for (unsigned session = 0; session < 2; ++session) {
        assert(board_start(&program));
        assert(!board_start(&program)); /* an active coroutine is retained */
        assert(host_frame() == 0);
        board_at_stop(stop_hook);
        board_at_frame(frame_hook);
        for (unsigned index = 1; index <= 3; ++index) {
            controls.player[0] = index & 1 ? JVS_UP | JVS_B1 : JVS_RIGHT | JVS_B4;
            naomi_run_frame();
            assert(host_frame() == index);
            assert(*(uint32_t*)host_native64_ram_pointer(0x8c420000, 4) == index);
        }
        board_stop();
        board_stop();
        assert(stopped == session + 1);
        assert(*(uint32_t*)host_native64_ram_pointer(0xac420000, 4) == 0);
        naomi_run_frame();
        assert(host_frame() == 3);
    }
    assert(presented == 6 && hooked == 6);
    rd_soft_shutdown();
    mvc2_game_memory_close();
    puts("Native board handoff: real ARM64 coroutine, JVS input, rendered frames, owned RAM and restart passed.");
    return 0;
}
