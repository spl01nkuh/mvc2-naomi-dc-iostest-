/* The game's static buffer tables and checked serialized references must use
 * the same native storage without a fixed virtual address mapping. */
#include "game_memory.h"
#include "host_memory.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static unsigned char *const buffers[] = {
    HOST_NATIVE64_RAM_AT(UINT32_C(0x0c000000)),
    HOST_NATIVE64_RAM_AT(UINT32_C(0x0c001234)),
    HOST_NATIVE64_RAM_AT(UINT32_C(0x0dffffff))
};

static void test_runtime_addresses(void) {
    const uint32_t bases[] = {UINT32_C(0x0c000000), UINT32_C(0x8c000000), UINT32_C(0xac000000)};
    for (size_t i = 0; i < sizeof bases / sizeof bases[0]; ++i) {
        uint32_t base = bases[i];
        assert(host_native64_ram_pointer(base, MVC2_NATIVE_RAM_SIZE) == host_native64_ram);
        assert(host_native64_ram_pointer(base + 0x1234, 1) == buffers[1]);
        assert(host_native64_ram_pointer(base + MVC2_NATIVE_RAM_SIZE - 1, 1) == buffers[2]);
        assert(host_native64_ram_pointer(base + MVC2_NATIVE_RAM_SIZE, 0) == host_native64_ram + MVC2_NATIVE_RAM_SIZE);
        assert(!host_native64_ram_pointer(base + MVC2_NATIVE_RAM_SIZE, 1));
        assert(!host_native64_ram_pointer(base + MVC2_NATIVE_RAM_SIZE - 1, 2));
        assert(!host_native64_ram_pointer(base, MVC2_NATIVE_RAM_SIZE + 1));
        assert(!host_native64_ram_pointer(base + 1, UINT32_MAX));
        assert(!host_native64_ram_pointer(base - 1, 1));
        assert(!host_native64_ram_pointer(base + MVC2_NATIVE_RAM_SIZE + 1, 0));
    }
    const uint32_t invalid[] = {0, UINT32_C(0x2c000000), UINT32_C(0x4c000000),
        UINT32_C(0x6c000000), UINT32_C(0xcc000000), UINT32_C(0xec000000), UINT32_MAX};
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i)
        assert(!host_native64_ram_pointer(invalid[i], 0));
}

int main(void) {
    assert((uintptr_t)host_native64_ram % 64 == 0);
    assert(buffers[0] == host_native64_ram && buffers[2] == host_native64_ram + MVC2_NATIVE_RAM_SIZE - 1);
    test_runtime_addresses(); /* Helpers work without an allocated session. */
    mvc2_game_memory_reset();
    buffers[1][0] = 0x5a;
    MVC2Memory *memory = mvc2_game_memory_open();
    assert(memory && mvc2_game_memory_open() == memory);
    assert(buffers[1][0] == 0x5a); /* Opening preserves initial bytes. */
    assert(mvc2_memory_ram_pointer(memory, UINT32_C(0xac001234), 4) == buffers[1]);
    assert(mvc2_memory_write_u32(memory, UINT32_C(0x8c001234), UINT32_C(0x12345678)) == MVC2_MEMORY_OK);
    assert(buffers[1][0] == 0x78 && buffers[1][3] == 0x12);
    unsigned char *runtime = host_native64_ram_pointer(UINT32_C(0xac001234), 4);
    runtime[1] = 0xab;
    uint32_t value = 0;
    assert(mvc2_memory_read_u32(memory, UINT32_C(0x0c001234), &value) == MVC2_MEMORY_OK);
    assert(value == UINT32_C(0x1234ab78));
    int object = 42;
    assert(mvc2_memory_bind_object(memory, UINT32_C(0x0c001234), 4, &object, sizeof object) == MVC2_MEMORY_OK);
    mvc2_game_memory_close();
    mvc2_game_memory_close();
    assert(runtime == buffers[1] && runtime[1] == 0xab && object == 42);
    runtime[0] = 0x9a;
    memory = mvc2_game_memory_open();
    assert(memory && runtime[0] == 0x9a);
    void *resolved = NULL;
    assert(mvc2_memory_resolve_object(memory, UINT32_C(0x0c001234), 4, &resolved, NULL) == MVC2_MEMORY_NOT_BOUND);
    assert(mvc2_memory_bind_object(memory, UINT32_C(0x0c001234), 4, &object, sizeof object) == MVC2_MEMORY_OK);
    mvc2_game_memory_reset();
    assert(mvc2_game_memory_open() == memory);
    assert(runtime[0] == 0 && buffers[0][0] == 0 && buffers[2][0] == 0 && object == 42);
    assert(mvc2_memory_resolve_object(memory, UINT32_C(0x0c001234), 4, &resolved, NULL) == MVC2_MEMORY_NOT_BOUND);
    mvc2_game_memory_close();
    buffers[0][0] = 1; buffers[2][0] = 2;
    mvc2_game_memory_reset();
    assert(buffers[0][0] == 0 && buffers[2][0] == 0);
    puts("Native game RAM: constant/runtime storage, mirrors, bounds and borrowed lifetime passed.");
    return 0;
}
