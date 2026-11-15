#include "engine_state.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

/* A host value outside the engine sections must survive both restores. */
static int outside_control = 3;

#if defined(__APPLE__)
#include "engine_sections.h"
#define ENGINE_DATA
#define ENGINE_BSS
#else
/* The fixture uses explicit sections under GCC as well, so the production
 * section boundary/copy algorithm can be exercised on the diagnostic host. */
#define ENGINE_DATA __attribute__((section("mvc2_data")))
#define ENGINE_BSS __attribute__((section("mvc2_bss")))
#endif

static int initialized ENGINE_DATA = 17;
static float fractional ENGINE_DATA = 1.25f;
static int* native_pointer ENGINE_DATA = &initialized;
static unsigned char zeroed[512] ENGINE_BSS;

int main(int argc, char** argv) {
    (void)argv;
    if (argc > 1) {
        zeroed[sizeof zeroed - 1] = 1;
        assert(!mvc2_engine_checkpoint());
        return 0;
    }
    assert((uintptr_t)native_pointer > UINT32_MAX);
    assert(mvc2_engine_checkpoint());
    initialized = 99;
    fractional = -0.5f;
    native_pointer = &outside_control;
    outside_control = 44;
    memset(zeroed, 0x5a, sizeof zeroed);
    assert(mvc2_engine_checkpoint());
    mvc2_engine_restore();
    assert(initialized == 17 && fractional == 1.25f);
    assert(native_pointer == &initialized && outside_control == 44);
    assert((uintptr_t)native_pointer > UINT32_MAX);
    for (unsigned index = 0; index < sizeof zeroed; ++index) assert(zeroed[index] == 0);
    initialized = 123;
    zeroed[7] = 7;
    outside_control = 55;
    mvc2_engine_restore();
    assert(initialized == 17 && zeroed[7] == 0);
    assert(native_pointer == &initialized && outside_control == 55);
    return 0;
}
