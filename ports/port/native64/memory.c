#include "memory.h"

#include <stdlib.h>
#include <string.h>

typedef struct NativeBinding {
    size_t offset, serialized_bytes, native_bytes;
    void *object;
} NativeBinding;

struct MVC2Memory {
    unsigned char *ram;
    NativeBinding *bindings;
    size_t count, capacity;
    int owns_ram;
};

static uint32_t le32(const unsigned char *bytes) {
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
           (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

MVC2Memory *mvc2_memory_create(void) {
    MVC2Memory *memory = calloc(1, sizeof(*memory));
    if (!memory) return NULL;
    memory->ram = calloc(1, MVC2_NATIVE_RAM_SIZE);
    if (!memory->ram) {
        free(memory);
        return NULL;
    }
    memory->owns_ram = 1;
    return memory;
}

MVC2Memory *mvc2_memory_borrow_ram(void *bytes, size_t capacity) {
    if (!bytes || capacity < MVC2_NATIVE_RAM_SIZE) return NULL;
    MVC2Memory *memory = calloc(1, sizeof(*memory));
    if (memory) memory->ram = bytes;
    return memory;
}

void mvc2_memory_destroy(MVC2Memory *memory) {
    if (!memory) return;
    free(memory->bindings);
    if (memory->owns_ram) free(memory->ram);
    free(memory);
}

void mvc2_memory_reset(MVC2Memory *memory) {
    if (!memory) return;
    memset(memory->ram, 0, MVC2_NATIVE_RAM_SIZE);
    memory->count = 0;
}

MVC2MemoryStatus mvc2_memory_offset(uint32_t address, size_t length, size_t *offset) {
    uint32_t mirror = address & UINT32_C(0xe0000000);
    uint32_t physical = address & UINT32_C(0x1fffffff);
    if (!offset) return MVC2_MEMORY_INVALID_ARGUMENT;
    if ((mirror != 0 && mirror != UINT32_C(0x80000000) &&
         mirror != UINT32_C(0xa0000000)) || physical < MVC2_NATIVE_RAM_BASE ||
        physical > MVC2_NATIVE_RAM_BASE + MVC2_NATIVE_RAM_SIZE)
        return MVC2_MEMORY_BAD_ADDRESS;
    size_t result = physical - MVC2_NATIVE_RAM_BASE;
    if (length > MVC2_NATIVE_RAM_SIZE - result) return MVC2_MEMORY_OUT_OF_RANGE;
    *offset = result;
    return MVC2_MEMORY_OK;
}

const void *mvc2_memory_ram_const_pointer(const MVC2Memory *memory,
                                         uint32_t address, size_t length) {
    size_t offset;
    if (!memory || mvc2_memory_offset(address, length, &offset) != MVC2_MEMORY_OK)
        return NULL;
    return memory->ram + offset;
}

void *mvc2_memory_ram_pointer(MVC2Memory *memory, uint32_t address, size_t length) {
    size_t offset;
    if (!memory || mvc2_memory_offset(address, length, &offset) != MVC2_MEMORY_OK)
        return NULL;
    return memory->ram + offset;
}

MVC2MemoryStatus mvc2_memory_read(const MVC2Memory *memory, uint32_t address,
                                  void *output, size_t length) {
    size_t offset;
    if (!memory || (!output && length)) return MVC2_MEMORY_INVALID_ARGUMENT;
    MVC2MemoryStatus status = mvc2_memory_offset(address, length, &offset);
    if (status != MVC2_MEMORY_OK) return status;
    if (length) memmove(output, memory->ram + offset, length);
    return MVC2_MEMORY_OK;
}

MVC2MemoryStatus mvc2_memory_write(MVC2Memory *memory, uint32_t address,
                                   const void *input, size_t length) {
    size_t offset;
    if (!memory || (!input && length)) return MVC2_MEMORY_INVALID_ARGUMENT;
    MVC2MemoryStatus status = mvc2_memory_offset(address, length, &offset);
    if (status != MVC2_MEMORY_OK) return status;
    if (length) memmove(memory->ram + offset, input, length);
    return MVC2_MEMORY_OK;
}

MVC2MemoryStatus mvc2_memory_read_u32(const MVC2Memory *memory, uint32_t address,
                                      uint32_t *output) {
    unsigned char bytes[4];
    if (!output) return MVC2_MEMORY_INVALID_ARGUMENT;
    MVC2MemoryStatus status = mvc2_memory_read(memory, address, bytes, sizeof bytes);
    if (status == MVC2_MEMORY_OK) *output = le32(bytes);
    return status;
}

MVC2MemoryStatus mvc2_memory_write_u32(MVC2Memory *memory, uint32_t address,
                                       uint32_t value) {
    unsigned char bytes[4] = {(unsigned char)value, (unsigned char)(value >> 8),
                              (unsigned char)(value >> 16), (unsigned char)(value >> 24)};
    return mvc2_memory_write(memory, address, bytes, sizeof bytes);
}

MVC2MemoryStatus mvc2_memory_bind_object(MVC2Memory *memory, uint32_t address,
                                         size_t serialized_bytes, void *object,
                                         size_t native_bytes) {
    size_t offset;
    if (!memory || !object || !serialized_bytes || !native_bytes)
        return MVC2_MEMORY_INVALID_ARGUMENT;
    MVC2MemoryStatus status = mvc2_memory_offset(address, serialized_bytes, &offset);
    if (status != MVC2_MEMORY_OK) return status;
    for (size_t i = 0; i < memory->count; ++i) {
        const NativeBinding *binding = &memory->bindings[i];
        if (offset < binding->offset + binding->serialized_bytes &&
            binding->offset < offset + serialized_bytes)
            return MVC2_MEMORY_BINDING_OVERLAP;
    }
    if (memory->count == memory->capacity) {
        size_t capacity = memory->capacity ? memory->capacity * 2 : 16;
        if (capacity < memory->capacity || capacity > SIZE_MAX / sizeof(NativeBinding))
            return MVC2_MEMORY_NO_MEMORY;
        NativeBinding *bindings = realloc(memory->bindings, capacity * sizeof(*bindings));
        if (!bindings) return MVC2_MEMORY_NO_MEMORY;
        memory->bindings = bindings;
        memory->capacity = capacity;
    }
    NativeBinding binding = {offset, serialized_bytes, native_bytes, object};
    memory->bindings[memory->count++] = binding;
    return MVC2_MEMORY_OK;
}

MVC2MemoryStatus mvc2_memory_unbind_object(MVC2Memory *memory, uint32_t address) {
    size_t offset;
    if (!memory) return MVC2_MEMORY_INVALID_ARGUMENT;
    MVC2MemoryStatus status = mvc2_memory_offset(address, 0, &offset);
    if (status != MVC2_MEMORY_OK) return status;
    for (size_t i = 0; i < memory->count; ++i) {
        if (memory->bindings[i].offset == offset) {
            memory->bindings[i] = memory->bindings[--memory->count];
            return MVC2_MEMORY_OK;
        }
    }
    return MVC2_MEMORY_NOT_BOUND;
}

MVC2MemoryStatus mvc2_memory_resolve_object(const MVC2Memory *memory,
                                            uint32_t address,
                                            size_t required_native_bytes,
                                            void **output, size_t *native_bytes) {
    size_t offset;
    if (!memory || !output || !required_native_bytes) return MVC2_MEMORY_INVALID_ARGUMENT;
    MVC2MemoryStatus status = mvc2_memory_offset(address, 0, &offset);
    if (status != MVC2_MEMORY_OK) return status;
    for (size_t i = 0; i < memory->count; ++i) {
        const NativeBinding *binding = &memory->bindings[i];
        if (binding->offset != offset) continue;
        if (required_native_bytes > binding->native_bytes) return MVC2_MEMORY_OBJECT_TOO_SMALL;
        *output = binding->object;
        if (native_bytes) *native_bytes = binding->native_bytes;
        return MVC2_MEMORY_OK;
    }
    return MVC2_MEMORY_NOT_BOUND;
}

MVC2MemoryStatus mvc2_memory_load_program(MVC2Memory *memory,
                                          MVC2MemoryROMRead reader, void *context,
                                          uint32_t rom_size) {
    enum { HEADER_SIZE = 0x500, TABLE_OFFSET = 0x360, SEGMENTS = 8 };
    struct Segment { uint32_t source, length; size_t offset; } segments[SEGMENTS];
    unsigned char header[HEADER_SIZE];
    size_t count = 0;
    if (!memory || !reader) return MVC2_MEMORY_INVALID_ARGUMENT;
    if (rom_size < sizeof header) return MVC2_MEMORY_INVALID_PROGRAM;
    if (!reader(context, 0, header, sizeof header)) return MVC2_MEMORY_ROM_READ_FAILED;
    if (memcmp(header, "NAOMI", 5)) return MVC2_MEMORY_INVALID_PROGRAM;
    for (size_t i = 0; i < SEGMENTS; ++i) {
        const unsigned char *record = header + TABLE_OFFSET + i * 12;
        uint32_t source = le32(record), destination = le32(record + 4), length = le32(record + 8);
        if (source == UINT32_MAX) break;
        size_t offset;
        if (!length || source > rom_size || length > rom_size - source ||
            mvc2_memory_offset(destination, length, &offset) != MVC2_MEMORY_OK)
            return MVC2_MEMORY_INVALID_PROGRAM;
        struct Segment segment = {source, length, offset};
        segments[count++] = segment;
    }
    if (!count) return MVC2_MEMORY_INVALID_PROGRAM;
    unsigned char *staging = malloc(MVC2_NATIVE_RAM_SIZE);
    if (!staging) return MVC2_MEMORY_NO_MEMORY;
    memcpy(staging, memory->ram, MVC2_NATIVE_RAM_SIZE);
    for (size_t i = 0; i < count; ++i) {
        const struct Segment *segment = &segments[i];
        if (!reader(context, segment->source, staging + segment->offset, segment->length)) {
            free(staging);
            return MVC2_MEMORY_ROM_READ_FAILED;
        }
    }
    memcpy(memory->ram, staging, MVC2_NATIVE_RAM_SIZE);
    free(staging);
    memory->count = 0;
    return MVC2_MEMORY_OK;
}

const char *mvc2_memory_status_string(MVC2MemoryStatus status) {
    switch (status) {
    case MVC2_MEMORY_OK: return "ok";
    case MVC2_MEMORY_INVALID_ARGUMENT: return "invalid native memory argument";
    case MVC2_MEMORY_BAD_ADDRESS: return "address is not a supported main-RAM mirror";
    case MVC2_MEMORY_OUT_OF_RANGE: return "read or write exceeds main RAM";
    case MVC2_MEMORY_NO_MEMORY: return "not enough memory for native RAM operation";
    case MVC2_MEMORY_BINDING_OVERLAP: return "serialized object bindings overlap";
    case MVC2_MEMORY_NOT_BOUND: return "no native object begins at this serialized address";
    case MVC2_MEMORY_OBJECT_TOO_SMALL: return "native object allocation is too small";
    case MVC2_MEMORY_INVALID_PROGRAM: return "invalid NAOMI program load table";
    case MVC2_MEMORY_ROM_READ_FAILED: return "could not read complete program segment";
    }
    return "unknown native memory status";
}
