/* Exercise serialized addresses at ordinary 64-bit heap locations. No ROM
 * images, fixed-address mappings, executable pages, or SDK stubs are used. */
#include "memory.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned high_address_cases;

static void test_addresses_and_words(void) {
    MVC2Memory *memory = mvc2_memory_create();
    assert(memory);
    unsigned char *ram = mvc2_memory_ram_pointer(memory, MVC2_NATIVE_RAM_BASE, MVC2_NATIVE_RAM_SIZE);
    assert(ram);
    if ((uintptr_t)ram > UINT32_MAX) ++high_address_cases;
    assert(ram[0] == 0 && ram[MVC2_NATIVE_RAM_SIZE - 1] == 0);
    assert(mvc2_memory_ram_pointer(memory, UINT32_C(0x8c000000), MVC2_NATIVE_RAM_SIZE) == ram);
    assert(mvc2_memory_ram_pointer(memory, UINT32_C(0xac000000), MVC2_NATIVE_RAM_SIZE) == ram);
    assert(mvc2_memory_write_u32(memory, UINT32_C(0x0c000001), UINT32_C(0x87654321)) == MVC2_MEMORY_OK);
    assert(ram[1] == 0x21 && ram[2] == 0x43 && ram[3] == 0x65 && ram[4] == 0x87);
    uint32_t value = 99;
    assert(mvc2_memory_read_u32(memory, UINT32_C(0x8c000001), &value) == MVC2_MEMORY_OK);
    assert(value == UINT32_C(0x87654321));
    assert(mvc2_memory_read_u32(memory, UINT32_C(0xac000001), &value) == MVC2_MEMORY_OK);
    assert(value == UINT32_C(0x87654321));
    assert(mvc2_memory_write_u32(memory, UINT32_C(0xadfffffc), UINT32_C(0x10203040)) == MVC2_MEMORY_OK);
    value = 11;
    assert(mvc2_memory_read_u32(memory, UINT32_C(0x0dfffffd), &value) == MVC2_MEMORY_OUT_OF_RANGE);
    assert(value == 11);
    assert(mvc2_memory_write_u32(memory, UINT32_C(0x0dfffffd), 0) == MVC2_MEMORY_OUT_OF_RANGE);
    assert(mvc2_memory_read_u32(memory, UINT32_C(0x0dfffffc), &value) == MVC2_MEMORY_OK);
    assert(value == UINT32_C(0x10203040));
    size_t offset = 123;
    const uint32_t invalid[] = {0, UINT32_C(0x0bffffff), UINT32_C(0x0e000001),
        UINT32_C(0x2c000000), UINT32_C(0x4c000000), UINT32_C(0x6c000000),
        UINT32_C(0xcc000000), UINT32_C(0xec000000), UINT32_MAX};
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        assert(mvc2_memory_offset(invalid[i], 0, &offset) == MVC2_MEMORY_BAD_ADDRESS);
        assert(offset == 123);
        assert(!mvc2_memory_ram_pointer(memory, invalid[i], 1));
    }
    assert(mvc2_memory_offset(MVC2_NATIVE_RAM_BASE, SIZE_MAX, &offset) == MVC2_MEMORY_OUT_OF_RANGE);
    assert(offset == 123);
    assert(mvc2_memory_offset(MVC2_NATIVE_RAM_BASE, 1, NULL) == MVC2_MEMORY_INVALID_ARGUMENT);
    assert(mvc2_memory_offset(UINT32_C(0x0e000000), 0, &offset) == MVC2_MEMORY_OK);
    assert(offset == MVC2_NATIVE_RAM_SIZE);
    assert(mvc2_memory_ram_pointer(memory, UINT32_C(0x8e000000), 0) == ram + MVC2_NATIVE_RAM_SIZE);
    assert(!mvc2_memory_ram_pointer(memory, UINT32_C(0xae000000), 1));
    assert(mvc2_memory_read(memory, MVC2_NATIVE_RAM_BASE, NULL, 0) == MVC2_MEMORY_OK);
    assert(mvc2_memory_write(memory, MVC2_NATIVE_RAM_BASE, NULL, 0) == MVC2_MEMORY_OK);
    assert(mvc2_memory_read(memory, MVC2_NATIVE_RAM_BASE, NULL, 1) == MVC2_MEMORY_INVALID_ARGUMENT);
    assert(mvc2_memory_write(memory, MVC2_NATIVE_RAM_BASE, NULL, 1) == MVC2_MEMORY_INVALID_ARGUMENT);
    assert(mvc2_memory_read_u32(memory, MVC2_NATIVE_RAM_BASE, NULL) == MVC2_MEMORY_INVALID_ARGUMENT);
    assert(!mvc2_memory_ram_pointer(NULL, MVC2_NATIVE_RAM_BASE, 1));
    /* The API defines aliasing when source/destination are in its own RAM. */
    assert(mvc2_memory_write(memory, MVC2_NATIVE_RAM_BASE, "abcdef", 6) == MVC2_MEMORY_OK);
    assert(mvc2_memory_write(memory, MVC2_NATIVE_RAM_BASE + 1, ram, 5) == MVC2_MEMORY_OK);
    assert(memcmp(ram, "aabcde", 6) == 0);
    assert(mvc2_memory_read(memory, MVC2_NATIVE_RAM_BASE + 1, ram, 5) == MVC2_MEMORY_OK);
    assert(memcmp(ram, "abcde", 5) == 0);
    mvc2_memory_destroy(memory);
    mvc2_memory_destroy(NULL);
}

typedef struct WidenedObject {
    void *pointers[3];
    uint32_t count;
} WidenedObject;

static void test_native_bindings(void) {
    MVC2Memory *memory = mvc2_memory_create();
    WidenedObject *object = calloc(1, sizeof(*object));
    assert(memory && object);
    if ((uintptr_t)object > UINT32_MAX) ++high_address_cases;
    object->pointers[0] = memory;
    object->count = 41;
    assert(mvc2_memory_bind_object(memory, UINT32_C(0x0c000100), 12, object, sizeof(*object)) == MVC2_MEMORY_OK);
    void *resolved = NULL;
    size_t native_bytes = 0;
    assert(mvc2_memory_resolve_object(memory, UINT32_C(0x8c000100), sizeof(*object), &resolved, &native_bytes) == MVC2_MEMORY_OK);
    assert(resolved == object && native_bytes == sizeof(*object));
    assert(((WidenedObject *)resolved)->count == 41 && ((WidenedObject *)resolved)->pointers[0] == memory);
    assert(mvc2_memory_ram_pointer(memory, UINT32_C(0x0c000100), 12) != object);
    /* An offset into serialized12-byte data is not an offset into a wider host record. */
    resolved = (void *)(uintptr_t)1;
    native_bytes = 77;
    assert(mvc2_memory_resolve_object(memory, UINT32_C(0xac000104), 4, &resolved, &native_bytes) == MVC2_MEMORY_NOT_BOUND);
    assert(resolved == (void *)(uintptr_t)1 && native_bytes == 77);
    assert(mvc2_memory_resolve_object(memory, UINT32_C(0x0c000100), sizeof(*object) + 1, &resolved, &native_bytes) == MVC2_MEMORY_OBJECT_TOO_SMALL);
    assert(resolved == (void *)(uintptr_t)1 && native_bytes == 77);
    assert(mvc2_memory_resolve_object(memory, UINT32_C(0x0c000100), 0, &resolved, &native_bytes) == MVC2_MEMORY_INVALID_ARGUMENT);
    assert(resolved == (void *)(uintptr_t)1 && native_bytes == 77);
    assert(mvc2_memory_resolve_object(memory, UINT32_C(0x0c000100), 1, NULL, &native_bytes) == MVC2_MEMORY_INVALID_ARGUMENT);
    assert(native_bytes == 77);
    assert(mvc2_memory_bind_object(memory, UINT32_C(0xac000100), 12, object, sizeof(*object)) == MVC2_MEMORY_BINDING_OVERLAP);
    assert(mvc2_memory_bind_object(memory, UINT32_C(0x0c0000ff), 2, object, sizeof(*object)) == MVC2_MEMORY_BINDING_OVERLAP);
    assert(mvc2_memory_bind_object(memory, UINT32_C(0x0c00010b), 2, object, sizeof(*object)) == MVC2_MEMORY_BINDING_OVERLAP);
    /* Adjacent serialized ranges are independent even when their host objects differ in size. */
    assert(mvc2_memory_bind_object(memory, UINT32_C(0x0c00010c), 4, &object->count, sizeof(object->count)) == MVC2_MEMORY_OK);
    assert(mvc2_memory_unbind_object(memory, UINT32_C(0x8c000100)) == MVC2_MEMORY_OK);
    assert(mvc2_memory_unbind_object(memory, UINT32_C(0xac000100)) == MVC2_MEMORY_NOT_BOUND);
    assert(mvc2_memory_resolve_object(memory, UINT32_C(0x0c00010c), 4, &resolved, NULL) == MVC2_MEMORY_OK);
    assert(resolved == &object->count);
    /* Force registry growth and removals at different positions. */
    for (size_t i = 0; i < 100; ++i)
        assert(mvc2_memory_bind_object(memory, MVC2_NATIVE_RAM_BASE + 0x1000 + (uint32_t)i * 8, 8, object, sizeof(*object)) == MVC2_MEMORY_OK);
    for (size_t i = 0; i < 100; i += 2)
        assert(mvc2_memory_unbind_object(memory, MVC2_NATIVE_RAM_BASE + 0x1000 + (uint32_t)i * 8) == MVC2_MEMORY_OK);
    for (size_t i = 1; i < 100; i += 2)
        assert(mvc2_memory_resolve_object(memory, MVC2_NATIVE_RAM_BASE + 0x1000 + (uint32_t)i * 8, 1, &resolved, NULL) == MVC2_MEMORY_OK);
    assert(mvc2_memory_bind_object(memory, MVC2_NATIVE_RAM_BASE, 0, object, sizeof(*object)) == MVC2_MEMORY_INVALID_ARGUMENT);
    assert(mvc2_memory_bind_object(memory, MVC2_NATIVE_RAM_BASE, 4, NULL, sizeof(*object)) == MVC2_MEMORY_INVALID_ARGUMENT);
    assert(mvc2_memory_bind_object(memory, MVC2_NATIVE_RAM_BASE, 4, object, 0) == MVC2_MEMORY_INVALID_ARGUMENT);
    assert(mvc2_memory_write_u32(memory, MVC2_NATIVE_RAM_BASE, 0xff) == MVC2_MEMORY_OK);
    mvc2_memory_reset(memory);
    uint32_t value = 99;
    assert(mvc2_memory_read_u32(memory, MVC2_NATIVE_RAM_BASE, &value) == MVC2_MEMORY_OK && value == 0);
    assert(mvc2_memory_resolve_object(memory, UINT32_C(0x0c00010c), 1, &resolved, NULL) == MVC2_MEMORY_NOT_BOUND);
    assert(object->count == 41); /* Bound object lifetime stays with the caller. */
    mvc2_memory_destroy(memory);
    free(object);
}

enum { ROM_SIZE = 0x900, TABLE = 0x360 };
typedef struct ROMFixture {
    unsigned char bytes[ROM_SIZE];
    unsigned calls, fail_call;
} ROMFixture;

static void put_u32(unsigned char *bytes, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[i] = (unsigned char)(value >> (i * 8));
}

static void segment(ROMFixture *rom, unsigned slot, uint32_t source, uint32_t destination, uint32_t length) {
    assert(slot < 8);
    unsigned char *p = rom->bytes + TABLE + slot * 12;
    put_u32(p, source);
    put_u32(p + 4, destination);
    put_u32(p + 8, length);
}

static void prepare_rom(ROMFixture *rom) {
    memset(rom, 0, sizeof(*rom));
    memcpy(rom->bytes, "NAOMI", 5);
    for (unsigned i = 0; i < 8; ++i) segment(rom, i, UINT32_MAX, 0, 0);
    for (size_t i = 0x500; i < sizeof(rom->bytes); ++i) rom->bytes[i] = (unsigned char)i;
}

static int fixture_read(void *context, uint32_t offset, void *output, size_t length) {
    ROMFixture *rom = context;
    ++rom->calls;
    assert(offset <= sizeof rom->bytes && length <= sizeof rom->bytes - offset);
    if (rom->calls == rom->fail_call) {
        if (length) memset(output, 0xcc, length / 2);
        return 0;
    }
    memcpy(output, rom->bytes + offset, length);
    return 1;
}

static void test_program_load(void) {
    ROMFixture rom;
    MVC2Memory *memory = mvc2_memory_create();
    assert(memory);
    unsigned char *ram = mvc2_memory_ram_pointer(memory, MVC2_NATIVE_RAM_BASE, MVC2_NATIVE_RAM_SIZE);
    unsigned char *before = malloc(MVC2_NATIVE_RAM_SIZE);
    assert(before);
    memset(ram, 0xa5, MVC2_NATIVE_RAM_SIZE);
    int object = 91;
    assert(mvc2_memory_bind_object(memory, MVC2_NATIVE_RAM_BASE + 0x200, 4, &object, sizeof object) == MVC2_MEMORY_OK);
    prepare_rom(&rom);
    segment(&rom, 0, 0x500, MVC2_NATIVE_RAM_BASE + 0x200, 32);
    segment(&rom, 1, 0x600, UINT32_C(0xac000210), 16); /* Later overlap wins. */
    segment(&rom, 2, 0x700, UINT32_C(0x8dfffff0), 16);
    assert(mvc2_memory_load_program(memory, fixture_read, &rom, sizeof rom.bytes) == MVC2_MEMORY_OK);
    assert(rom.calls == 4);
    assert(memcmp(ram + 0x200, rom.bytes + 0x500, 16) == 0);
    assert(memcmp(ram + 0x210, rom.bytes + 0x600, 16) == 0);
    assert(memcmp(ram + MVC2_NATIVE_RAM_SIZE - 16, rom.bytes + 0x700, 16) == 0);
    assert(ram[0x1ff] == 0xa5 && ram[0x220] == 0xa5);
    void *resolved = NULL;
    assert(mvc2_memory_resolve_object(memory, MVC2_NATIVE_RAM_BASE + 0x200, sizeof object, &resolved, NULL) == MVC2_MEMORY_NOT_BOUND);
    assert(mvc2_memory_bind_object(memory, MVC2_NATIVE_RAM_BASE + 0x200, 4, &object, sizeof object) == MVC2_MEMORY_OK);
    memcpy(before, ram, MVC2_NATIVE_RAM_SIZE);
    /* Header and first/middle/final payload failures must not commit earlier
       writes or discard native bindings. The reader can return partial data. */
    memset(rom.bytes + 0x500, 0x34, 32);
    for (unsigned fail = 1; fail <= 4; ++fail) {
        rom.calls = 0;
        rom.fail_call = fail;
        assert(mvc2_memory_load_program(memory, fixture_read, &rom, sizeof rom.bytes) == MVC2_MEMORY_ROM_READ_FAILED);
        assert(memcmp(before, ram, MVC2_NATIVE_RAM_SIZE) == 0);
        assert(mvc2_memory_resolve_object(memory, MVC2_NATIVE_RAM_BASE + 0x200, sizeof object, &resolved, NULL) == MVC2_MEMORY_OK);
        assert(resolved == &object);
    }
    const struct { uint32_t source, destination, length; } invalid[] = {
        {0x500, MVC2_NATIVE_RAM_BASE, 0},
        {ROM_SIZE - 1, MVC2_NATIVE_RAM_BASE, 2},
        {ROM_SIZE, MVC2_NATIVE_RAM_BASE, 1},
        {UINT32_C(0xfffffff0), MVC2_NATIVE_RAM_BASE, 32},
        {0x500, UINT32_C(0x0dffffff), 2},
        {0x500, UINT32_C(0x2c000000), 4},
        {0x500, UINT32_C(0x0bffffff), 4},
        {0x500, MVC2_NATIVE_RAM_BASE, UINT32_MAX}
    };
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        prepare_rom(&rom);
        segment(&rom, 0, 0x500, MVC2_NATIVE_RAM_BASE, 4);
        segment(&rom, 1, invalid[i].source, invalid[i].destination, invalid[i].length);
        assert(mvc2_memory_load_program(memory, fixture_read, &rom, sizeof rom.bytes) == MVC2_MEMORY_INVALID_PROGRAM);
        assert(rom.calls == 1); /* Validate the entire table before requesting payloads. */
        assert(memcmp(before, ram, MVC2_NATIVE_RAM_SIZE) == 0);
        assert(mvc2_memory_resolve_object(memory, MVC2_NATIVE_RAM_BASE + 0x200, sizeof object, &resolved, NULL) == MVC2_MEMORY_OK);
        assert(resolved == &object);
    }
    prepare_rom(&rom);
    assert(mvc2_memory_load_program(memory, fixture_read, &rom, sizeof rom.bytes) == MVC2_MEMORY_INVALID_PROGRAM);
    assert(memcmp(before, ram, MVC2_NATIVE_RAM_SIZE) == 0);
    assert(mvc2_memory_resolve_object(memory, MVC2_NATIVE_RAM_BASE + 0x200, sizeof object, &resolved, NULL) == MVC2_MEMORY_OK);
    rom.bytes[0] = 'X';
    assert(mvc2_memory_load_program(memory, fixture_read, &rom, sizeof rom.bytes) == MVC2_MEMORY_INVALID_PROGRAM);
    assert(memcmp(before, ram, MVC2_NATIVE_RAM_SIZE) == 0);
    assert(mvc2_memory_resolve_object(memory, MVC2_NATIVE_RAM_BASE + 0x200, sizeof object, &resolved, NULL) == MVC2_MEMORY_OK);
    assert(mvc2_memory_load_program(memory, fixture_read, &rom, 0x4ff) == MVC2_MEMORY_INVALID_PROGRAM);
    assert(mvc2_memory_load_program(NULL, fixture_read, &rom, sizeof rom.bytes) == MVC2_MEMORY_INVALID_ARGUMENT);
    assert(mvc2_memory_load_program(memory, NULL, &rom, sizeof rom.bytes) == MVC2_MEMORY_INVALID_ARGUMENT);
    /* The end marker ends the table even if later unused bytes look invalid. */
    prepare_rom(&rom);
    segment(&rom, 0, 0x500, MVC2_NATIVE_RAM_BASE, 16);
    segment(&rom, 2, UINT32_C(0xffffff00), 0, UINT32_MAX);
    assert(mvc2_memory_load_program(memory, fixture_read, &rom, sizeof rom.bytes) == MVC2_MEMORY_OK);
    assert(rom.calls == 2 && memcmp(ram, rom.bytes + 0x500, 16) == 0);
    /* Eight entries without a sentinel are valid, matching the original bound. */
    prepare_rom(&rom);
    for (unsigned i = 0; i < 8; ++i) segment(&rom, i, 0x500 + i * 16, MVC2_NATIVE_RAM_BASE + i * 16, 16);
    assert(mvc2_memory_load_program(memory, fixture_read, &rom, sizeof rom.bytes) == MVC2_MEMORY_OK);
    assert(rom.calls == 9);
    assert(memcmp(ram, rom.bytes + 0x500, 8 * 16) == 0);
    free(before);
    mvc2_memory_destroy(memory);
}

int main(void) {
    test_addresses_and_words();
    test_native_bindings();
    test_program_load();
    if (sizeof(void *) > 4)
        printf("High-address allocation cases exercised: %u (allocator-dependent).\n", high_address_cases);
    puts("Native64 owned RAM, mirrored addresses, object bindings and transactional program loads passed.");
    return 0;
}
