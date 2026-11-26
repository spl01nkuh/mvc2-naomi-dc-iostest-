#include "model_store.h"
#include "model.h"
#include "../include/host_memory.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_FILES = 256, MAX_MODELS = 64, MAX_PACKED_TEXTURE_BYTES = 4 * 1024 * 1024 };
typedef struct FileExtent {
    size_t offset, size;
    uint64_t generation;
} FileExtent;
typedef struct ModelEntry {
    size_t offset;
    uint64_t model_generation, texture_generation;
    int packed_texture;
    uint32_t mapped_texture_address;
    MVC2HostObjectFile file;
    MVC2NativeModel* owner;
} ModelEntry;
static FileExtent files[MAX_FILES];
static ModelEntry* models[MAX_MODELS];
static size_t file_count, model_count;
static uint64_t generation;
static char last_error[256];

static MVC2HostObjectFile* fail(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(last_error, sizeof last_error, format, args);
    va_end(args);
    return NULL;
}
const char* mvc2_model_store_last_error(void) {
    return last_error;
}

static uint16_t le16(const uint8_t* bytes) {
    return (uint16_t)(bytes[0] | (uint16_t)bytes[1] << 8);
}
static uint32_t le32(const uint8_t* bytes) {
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
        (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}
static int ram_offset(const void* pointer, size_t size, size_t* offset) {
    uintptr_t value = (uintptr_t)pointer, base = (uintptr_t)host_native64_ram;
    if (value < base || value - base > MVC2_NATIVE_RAM_SIZE) return 0;
    *offset = (size_t)(value - base);
    return size <= MVC2_NATIVE_RAM_SIZE - *offset;
}
int mvc2_model_store_written(void* destination, size_t size) {
    size_t offset;
    if (!size || !ram_offset(destination, size, &offset) || generation == UINT64_MAX) return 0;
    size_t slot = file_count;
    for (size_t i = 0; i < file_count; ++i)
        if (files[i].offset == offset) { slot = i; break; }
    if (slot == file_count && file_count == MAX_FILES) {
        for (size_t i = 0; i < file_count; ++i)
            if (!files[i].size) { slot = i; break; }
        if (slot == file_count) return 0;
    }
    for (size_t i = 0; i < file_count; ++i) {
        if (offset < files[i].offset + files[i].size && files[i].offset < offset + size)
            files[i].size = 0;
    }
    files[slot] = (FileExtent){offset, size, ++generation};
    if (slot == file_count) ++file_count;
    return 1;
}
static const FileExtent* find_file(size_t offset) {
    for (size_t i = 0; i < file_count; ++i)
        if (files[i].offset == offset && files[i].size) return &files[i];
    return NULL;
}
/* The card notice uses the game's word-LZ stream, with an explicit zero-copy
 * terminator. With output NULL this validates the stream and measures it. */
static int unpack_lz16(const uint8_t* input, size_t input_size, uint8_t* output,
                       size_t capacity, size_t* output_size) {
    size_t pos = 0, words = 0;
    uint16_t flags = 0, bit = 0;
    while (pos + 2 <= input_size) {
        if (!bit) {
            flags = le16(input + pos);
            pos += 2;
            bit = 0x8000;
        }
        if (pos + 2 > input_size) return 0;
        uint16_t token = le16(input + pos);
        pos += 2;
        if (flags & bit) {
            size_t back = token & 0x7ff, count = token >> 11;
            if (!count) {
                if (pos + 2 > input_size) return 0;
                count = le16(input + pos);
                pos += 2;
            }
            if (!count) {
                if (back) return 0;
                *output_size = words * 2;
                return 1;
            }
            if (back > words || count > capacity / 2 - words) return 0;
            if (output) {
                for (size_t i = 0; i < count; ++i) {
                    uint8_t* dst = output + (words + i) * 2;
                    if (back) {
                        const uint8_t* src = output + (words + i - back) * 2;
                        dst[0] = src[0];
                        dst[1] = src[1];
                    } else {
                        dst[0] = dst[1] = 0;
                    }
                }
            }
            words += count;
        } else {
            if (words == capacity / 2) return 0;
            if (output) {
                output[words * 2] = (uint8_t)token;
                output[words * 2 + 1] = (uint8_t)(token >> 8);
            }
            ++words;
        }
        bit >>= 1;
    }
    return 0;
}
int mvc2_model_store_unpack_picture(const MVC2HostObjectFile* model, size_t texture,
                                    const void* picture, size_t part) {
    size_t offset;
    if (!ram_offset(picture, 4, &offset)) return 0;
    const FileExtent* file = find_file(offset);
    if (!file || part >= file->size / 4) return 0;
    const uint8_t* bytes = picture;
    uint32_t begin = le32(bytes + part * 4);
    if (begin < (part + 1) * 4 || begin >= file->size || (begin & 1u)) return 0;
    for (size_t i = 0; i < model_count; ++i) {
        ModelEntry* entry = models[i];
        if (model != &entry->file || !entry->mapped_texture_address) continue;
        const NLthd* target = mvc2_native_model_texture(entry->owner, texture);
        if (!target) return 0;
        size_t capacity = mvc2_native_model_texture_source_bytes(entry->owner, texture), decoded;
        return unpack_lz16(bytes + begin, file->size - begin, target->address, capacity, &decoded)
            && decoded != 0;
    }
    return 0;
}

static MVC2HostObjectFile* get_model(void* model_bytes, const void* packed_bytes,
                                     uint32_t mapped_texture_address) {
    size_t offset;
    last_error[0] = '\0';
    if (!ram_offset(model_bytes, 16, &offset))
        return fail("Model source is outside the native game RAM backing.");
    uint32_t model_address = MVC2_NATIVE_RAM_BASE + (uint32_t)offset;
    const FileExtent* source = find_file(offset);
    if (!source || source->size < 16)
        return fail("Model %08X has no complete registered source file.", model_address);
    const uint8_t* bytes = model_bytes;
    uint32_t table = le32(bytes), texture_table = le32(bytes + 8);
    if (table < 16 || texture_table < table - 16)
        return fail("Model %08X has invalid object/texture table addresses %08X/%08X.",
                    model_address, table, texture_table);
    size_t relative = texture_table - (table - 16);
    if (relative > source->size || source->size - relative < 2)
        return fail("Model %08X texture table %08X is outside its %zu-byte source file.",
                    model_address, texture_table, source->size);
    const FileExtent* textures = NULL;
    if (packed_bytes || mapped_texture_address || le16(bytes + relative)) {
        size_t texture_offset;
        if (packed_bytes) {
            if (!ram_offset(packed_bytes, 1, &texture_offset))
                return fail("Model %08X packed texture source is outside game RAM.", model_address);
        } else if (mapped_texture_address) {
            if (mvc2_memory_offset(mapped_texture_address, 1, &texture_offset) != MVC2_MEMORY_OK)
                return fail("Model %08X texture bank %08X is outside game RAM.",
                            model_address, mapped_texture_address);
        } else if (source->size - relative < 16 ||
                   mvc2_memory_offset(le32(bytes + relative + 8), 1, &texture_offset) != MVC2_MEMORY_OK) {
            return fail("Model %08X first texture address cannot resolve in game RAM.", model_address);
        }
        textures = find_file(texture_offset);
        if (!textures)
            return fail("Model %08X texture bank %08X has no complete registered file.",
                        model_address, MVC2_NATIVE_RAM_BASE + (uint32_t)texture_offset);
    }
    ModelEntry* entry = NULL;
    for (size_t i = 0; i < model_count; ++i)
        if (models[i]->offset == offset) { entry = models[i]; break; }
    uint64_t texture_generation = textures ? textures->generation : 0;
    if (entry && entry->model_generation == source->generation &&
        entry->texture_generation == texture_generation &&
        entry->mapped_texture_address == mapped_texture_address &&
        entry->packed_texture == (packed_bytes != NULL)) return &entry->file;

    const uint8_t* texture_bytes = textures ? host_native64_ram + textures->offset : NULL;
    size_t texture_size = textures ? textures->size : 0;
    uint8_t* unpacked = NULL;
    if (packed_bytes) {
        size_t decoded_size;
        if (!unpack_lz16(texture_bytes, texture_size, NULL, MAX_PACKED_TEXTURE_BYTES,
                         &decoded_size) || !decoded_size)
            return fail("Model %08X packed texture stream is invalid or too large.", model_address);
        unpacked = malloc(decoded_size);
        if (!unpacked) return fail("Model %08X could not allocate its unpacked texture.", model_address);
        if (!unpack_lz16(texture_bytes, texture_size, unpacked, decoded_size, &decoded_size)) {
            free(unpacked);
            return fail("Model %08X packed texture stream changed while decoding.", model_address);
        }
        texture_bytes = unpacked;
        texture_size = decoded_size;
    }
    MVC2NativeModel* owner = NULL;
    MVC2NativeModelStatus status;
    if (mapped_texture_address) {
        /* The board's model links textures throughout shared RAM, including
         * the selection atlases, name pictures and packed-picture scratch
         * buffers (sel_sub.c). Physical, cached and uncached SH-4 aliases
         * resolve to the same bounded native backing, never process casts.
         * Each complete texture read is still checked by the decoder. */
        const MVC2ModelTextureSpan spans[] = {
            { MVC2_NATIVE_RAM_BASE, host_native64_ram, MVC2_NATIVE_RAM_SIZE },
            { MVC2_NATIVE_RAM_BASE | UINT32_C(0x80000000), host_native64_ram, MVC2_NATIVE_RAM_SIZE },
            { MVC2_NATIVE_RAM_BASE | UINT32_C(0xa0000000), host_native64_ram, MVC2_NATIVE_RAM_SIZE },
        };
        status = mvc2_native_model_decode_mapped(bytes, source->size, spans,
                                                  sizeof spans / sizeof spans[0], &owner);
    } else {
        status = mvc2_native_model_decode(bytes, source->size, texture_bytes, texture_size, &owner);
    }
    free(unpacked);
    if (status != MVC2_NATIVE_MODEL_OK)
        return fail("Model %08X (%zu bytes): %s.", model_address, source->size,
                    mvc2_native_model_status_string(status));
    if (!entry) {
        if (model_count == MAX_MODELS) {
            mvc2_native_model_free(owner);
            return fail("Model %08X exceeds the native model-cache capacity.", model_address);
        }
        entry = calloc(1, sizeof(*entry));
        if (!entry) {
            mvc2_native_model_free(owner);
            return fail("Model %08X could not allocate its native cache entry.", model_address);
        }
        entry->offset = offset;
        models[model_count++] = entry;
    }
    MVC2NativeModel* previous = entry->owner;
    entry->file = *mvc2_native_model_file(owner);
    entry->owner = owner;
    entry->model_generation = source->generation;
    entry->texture_generation = texture_generation;
    entry->mapped_texture_address = mapped_texture_address;
    entry->packed_texture = packed_bytes != NULL;
    mvc2_native_model_free(previous);
    return &entry->file;
}
MVC2HostObjectFile* mvc2_model_store_get(void* model_bytes) {
    return get_model(model_bytes, NULL, 0);
}
MVC2HostObjectFile* mvc2_model_store_get_mapped(void* model_bytes, uint32_t texture_address) {
    return texture_address ? get_model(model_bytes, NULL, texture_address) :
        fail("Mapped texture bank address is zero.");
}
MVC2HostObjectFile* mvc2_model_store_get_packed_texture(void* model_bytes,
                                                       const void* packed_bytes) {
    return packed_bytes ? get_model(model_bytes, packed_bytes, 0) :
        fail("Packed texture source is missing.");
}
void mvc2_model_store_reset(void) {
    for (size_t i = 0; i < model_count; ++i) {
        mvc2_native_model_free(models[i]->owner);
        free(models[i]);
    }
    memset(models, 0, sizeof models);
    memset(files, 0, sizeof files);
    model_count = file_count = 0;
    generation = 0;
    last_error[0] = '\0';
}
