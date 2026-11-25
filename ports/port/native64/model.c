#include "model.h"
#include "texture_layout.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_OBJECTS = 16384, MAX_VERTEX_STRIP = 4096 };
#define MAX_FILE_BYTES ((size_t)64 * 1024 * 1024)
_Static_assert(sizeof(int) == 4, "the existing mesh backend consumes 32-bit int words");

struct MVC2NativeModel {
    MVC2HostObjectFile file;
    uint8_t* model_bytes;
    uint8_t* texture_bytes;
    int** objects;
    size_t* object_sizes;
    NLthd* textures;
    size_t* texture_sizes;
    size_t object_count, texture_count;
};

typedef struct ObjectOffset {
    size_t offset, index;
} ObjectOffset;

static uint16_t le16(const uint8_t* p) {
    return (uint16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8);
}
static uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
        (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static int span(size_t offset, size_t count, size_t size) {
    return offset <= size && count <= size - offset;
}
static int offset_for(uint32_t address, uint32_t base, size_t size, size_t* offset) {
    if (address < base || (uint64_t)address - base > size) return 0;
    *offset = (size_t)(address - base);
    return 1;
}

static uint8_t* mapped_texture(const MVC2ModelTextureSpan* spans, size_t count,
                               uint32_t address, size_t bytes) {
    for (size_t i = 0; i < count; ++i) {
        size_t offset;
        if (offset_for(address, spans[i].address, spans[i].size, &offset) &&
            span(offset, bytes, spans[i].size))
            return spans[i].bytes + offset;
    }
    return NULL;
}

/* Exact source reads in nl_tex.c, including its padding/mipmap prefixes.
 * These are source byte extents, not the untwiddled allocation sizes. */
static MVC2NativeModelStatus texture_extent(int32_t type, unsigned w, unsigned h, size_t* bytes) {
    MVC2TextureLayoutStatus status = mvc2_texture_source_extent(type, w, h, bytes, NULL);
    return status == MVC2_TEXTURE_LAYOUT_OK ? MVC2_NATIVE_MODEL_OK :
        status == MVC2_TEXTURE_LAYOUT_INVALID ? MVC2_NATIVE_MODEL_TEXTURE :
        MVC2_NATIVE_MODEL_UNSUPPORTED_TEXTURE;
}

static MVC2NativeModelStatus vertex_records(const uint8_t* model, size_t size,
                                             size_t* cursor, size_t end, size_t vertices) {
    if (vertices > (end - *cursor) / 8) return MVC2_NATIVE_MODEL_PRIMITIVE;
    for (size_t i = 0; i < vertices; ++i) {
        if (!span(*cursor, 8, end)) return MVC2_NATIVE_MODEL_PRIMITIVE;
        if (le32(model + *cursor) & 1) {
            if (!span(*cursor, 32, end)) return MVC2_NATIVE_MODEL_PRIMITIVE;
            *cursor += 32;
        } else {
            int64_t destination = (int64_t)*cursor + 8 + (int32_t)le32(model + *cursor + 4);
            if (destination < 0 || (destination & 3) ||
                !span((size_t)destination, 32, size))
                return MVC2_NATIVE_MODEL_REFERENCE;
            *cursor += 8;
        }
    }
    return MVC2_NATIVE_MODEL_OK;
}

static MVC2NativeModelStatus primitives(const uint8_t* model, size_t size,
                                         size_t begin, size_t end) {
    size_t cursor = begin;
    while (cursor < end) {
        if (!span(cursor, 4, end)) return MVC2_NATIVE_MODEL_PRIMITIVE;
        uint32_t head = le32(model + cursor);
        if (!head || head & UINT32_C(0x80000000)) return MVC2_NATIVE_MODEL_OK;
        if (!span(cursor, 8, end)) return MVC2_NATIVE_MODEL_PRIMITIVE;
        uint32_t count = le32(model + cursor + 4);
        cursor += 8;
        if (count & UINT32_C(0x80000000)) return MVC2_NATIVE_MODEL_PRIMITIVE;
        size_t vertices = count;
        if (head & 8u) {
            if (vertices > SIZE_MAX / 3) return MVC2_NATIVE_MODEL_PRIMITIVE;
            vertices *= 3;
        }
        else if (count > MAX_VERTEX_STRIP) return MVC2_NATIVE_MODEL_PRIMITIVE;
        MVC2NativeModelStatus status = vertex_records(model, size, &cursor, end, vertices);
        if (status != MVC2_NATIVE_MODEL_OK) return status;
    }
    return MVC2_NATIVE_MODEL_OK;
}

/* Negative object kinds are not submitted to nlObjPutLoadTexture. Some are
 * UV sources used by obj_uv_next: after the 24-byte object header they have
 * primitive records directly, without an 80-byte material header. Its count
 * rule is bit 0x10 for a strip, otherwise triangles; this differs from the
 * drawing backend's bit-0x08 rule. Preserve these raw records and their file-
 * relative short references in the owned copy just like normal groups. */
static MVC2NativeModelStatus uv_template_extent(const uint8_t* bytes, size_t size,
                                                 size_t start, size_t* extent) {
    size_t cursor = start + 24;
    for (;;) {
        if (!span(cursor, 4, size)) return MVC2_NATIVE_MODEL_PRIMITIVE;
        uint32_t head = le32(bytes + cursor);
        if (!head) {
            *extent = cursor + 4 - start;
            return MVC2_NATIVE_MODEL_OK;
        }
        if (head & UINT32_C(0x80000000) || !span(cursor, 8, size))
            return MVC2_NATIVE_MODEL_PRIMITIVE;
        uint32_t count = le32(bytes + cursor + 4);
        cursor += 8;
        /* A zero count still makes the original UV helper read one record. */
        if (!count || count & UINT32_C(0x80000000)) return MVC2_NATIVE_MODEL_PRIMITIVE;
        size_t vertices = count;
        if (!(head & 0x10u)) {
            if (vertices > SIZE_MAX / 3) return MVC2_NATIVE_MODEL_PRIMITIVE;
            vertices *= 3;
        } else if (count > MAX_VERTEX_STRIP) return MVC2_NATIVE_MODEL_PRIMITIVE;
        MVC2NativeModelStatus status = vertex_records(bytes, size, &cursor, size, vertices);
        if (status != MVC2_NATIVE_MODEL_OK) return status;
    }
}

static MVC2NativeModelStatus object_extent(const uint8_t* bytes, size_t size,
                                            size_t start, size_t textures,
                                            size_t* extent) {
    if (!span(start, 24, size) || start % 4) return MVC2_NATIVE_MODEL_REFERENCE;
    size_t cursor = start + 24;
    if ((le32(bytes + start) & UINT32_C(0x80000000)) && span(cursor, 4, size)) {
        uint32_t head = le32(bytes + cursor);
        if (head && !(head & UINT32_C(0x80000000)))
            return uv_template_extent(bytes, size, start, extent);
    }
    for (;;) {
        if (!span(cursor, 4, size)) return MVC2_NATIVE_MODEL_HEADER;
        if (!le32(bytes + cursor)) {
            *extent = cursor + 4 - start;
            return MVC2_NATIVE_MODEL_OK;
        }
        if (!span(cursor, 80, size)) return MVC2_NATIVE_MODEL_HEADER;
        int32_t texture = (int32_t)le32(bytes + cursor + 32);
        if (texture >= 0 && (size_t)texture >= textures) return MVC2_NATIVE_MODEL_TEXTURE;
        uint32_t length = le32(bytes + cursor + 76);
        /* Subsequent mesh headers and all native helper word views must stay
         * aligned. An early primitive terminator does not justify odd padding. */
        if (length & 3u) return MVC2_NATIVE_MODEL_PRIMITIVE;
        size_t begin = cursor + 80;
        if (!span(begin, length, size)) return MVC2_NATIVE_MODEL_HEADER;
        MVC2NativeModelStatus result = primitives(bytes, size, begin, begin + length);
        if (result != MVC2_NATIVE_MODEL_OK) return result;
        cursor = begin + length;
    }
}

static int compare_offsets(const void* left, const void* right) {
    const ObjectOffset* a = left;
    const ObjectOffset* b = right;
    return (a->offset > b->offset) - (a->offset < b->offset);
}

void mvc2_native_model_free(MVC2NativeModel* model) {
    if (!model) return;
    free(model->model_bytes);
    free(model->texture_bytes);
    free(model->objects);
    free(model->object_sizes);
    free(model->textures);
    free(model->texture_sizes);
    free(model);
}

static MVC2NativeModelStatus decode(
    const void* model_bytes, size_t model_size,
    const void* texture_bytes, size_t texture_size,
    const MVC2ModelTextureSpan* spans, size_t span_count,
    MVC2NativeModel** output) {
    if (!model_bytes || !output || (!texture_bytes && texture_size)) return MVC2_NATIVE_MODEL_ARGUMENT;
    if (model_size > MAX_FILE_BYTES || texture_size > MAX_FILE_BYTES) return MVC2_NATIVE_MODEL_LIMIT;
    if (model_size < 16) return MVC2_NATIVE_MODEL_HEADER;
    const uint8_t* source = model_bytes;
    uint32_t table_address = le32(source), count = le32(source + 4);
    if (table_address < 16) return MVC2_NATIVE_MODEL_HEADER;
    uint32_t base = table_address - 16;
    if ((uint64_t)base + model_size > UINT64_C(0x100000000)) return MVC2_NATIVE_MODEL_REFERENCE;
    if (count > MAX_OBJECTS) return MVC2_NATIVE_MODEL_LIMIT;
    if (!span(16, (size_t)count * 4, model_size)) return MVC2_NATIVE_MODEL_HEADER;
    size_t texture_offset;
    if (!offset_for(le32(source + 8), base, model_size, &texture_offset))
        return MVC2_NATIVE_MODEL_REFERENCE;

    size_t texture_count = 0, cursor = texture_offset;
    uint32_t texture_base = 0;
    for (;;) {
        if (!span(cursor, 2, model_size)) return MVC2_NATIVE_MODEL_TEXTURE;
        int16_t w = (int16_t)le16(source + cursor);
        if (!w) break;
        if (texture_count == NL_MAX_TEX) return MVC2_NATIVE_MODEL_LIMIT;
        if (!span(cursor, 16, model_size)) return MVC2_NATIVE_MODEL_TEXTURE;
        int16_t h = (int16_t)le16(source + cursor + 2);
        if (w < 0 || h <= 0 || w > 1024 || h > 1024) return MVC2_NATIVE_MODEL_TEXTURE;
        uint32_t address = le32(source + cursor + 8);
        if (!spans && !texture_count) {
            texture_base = address;
            if ((uint64_t)address + texture_size > UINT64_C(0x100000000))
                return MVC2_NATIVE_MODEL_REFERENCE;
        }
        size_t offset = 0, needed;
        if (!spans && !offset_for(address, texture_base, texture_size, &offset))
            return MVC2_NATIVE_MODEL_REFERENCE;
        MVC2NativeModelStatus result = texture_extent((int32_t)le32(source + cursor + 4),
                                                      (unsigned)w, (unsigned)h, &needed);
        if (result != MVC2_NATIVE_MODEL_OK) return result;
        unsigned kind = le32(source + cursor + 4) >> 8 & 255u;
        int word_aligned = kind < 5 || kind > 8;
        if (spans) {
            uint8_t* mapped = mapped_texture(spans, span_count, address, needed);
            if (!mapped) return MVC2_NATIVE_MODEL_REFERENCE;
            if (word_aligned && ((uintptr_t)mapped & 1u)) return MVC2_NATIVE_MODEL_TEXTURE;
        } else {
            /* The owned payload has a malloc-aligned base. */
            if (word_aligned && (offset & 1u)) return MVC2_NATIVE_MODEL_TEXTURE;
            if (!span(offset, needed, texture_size) || !texture_bytes) return MVC2_NATIVE_MODEL_TEXTURE;
        }
        ++texture_count;
        cursor += 16;
    }

    MVC2NativeModel* model = calloc(1, sizeof(*model));
    ObjectOffset* offsets = NULL;
    MVC2NativeModelStatus result = MVC2_NATIVE_MODEL_ALLOCATION;
    if (!model) return result;
    model->object_count = count;
    model->texture_count = texture_count;
    model->model_bytes = malloc(model_size);
    model->texture_bytes = texture_size ? malloc(texture_size) : NULL;
    model->textures = calloc(texture_count + 1, sizeof(*model->textures));
    model->texture_sizes = texture_count ? calloc(texture_count, sizeof(*model->texture_sizes)) : NULL;
    model->objects = count ? calloc(count, sizeof(*model->objects)) : NULL;
    model->object_sizes = count ? calloc(count, sizeof(*model->object_sizes)) : NULL;
    offsets = count ? calloc(count, sizeof(*offsets)) : NULL;
    if (!model->model_bytes || (texture_size && !model->texture_bytes) || !model->textures ||
        (texture_count && !model->texture_sizes) || (count && (!model->objects || !model->object_sizes || !offsets)))
        goto failure;
    memcpy(model->model_bytes, source, model_size);
    if (texture_size) memcpy(model->texture_bytes, texture_bytes, texture_size);
    for (size_t i = 0; i < texture_count; ++i) {
        const uint8_t* entry = source + texture_offset + i * 16;
        NLthd* target = &model->textures[i];
        target->width = (int16_t)le16(entry);
        target->height = (int16_t)le16(entry + 2);
        target->type = (int32_t)le32(entry + 4);
        target->globl_index = (int32_t)le32(entry + 12);
        (void)texture_extent(target->type, (unsigned)target->width,
                             (unsigned)target->height, &model->texture_sizes[i]);
        uint32_t address = le32(entry + 8);
        target->address = spans
            ? mapped_texture(spans, span_count, address, model->texture_sizes[i])
            : model->texture_bytes + (address - texture_base);
    }
    for (size_t i = 0; i < count; ++i) {
        uint32_t address = le32(source + 16 + i * 4);
        offsets[i].index = i;
        if (!address) offsets[i].offset = SIZE_MAX;
        else if (!offset_for(address, base, model_size, &offsets[i].offset)) {
            result = MVC2_NATIVE_MODEL_REFERENCE;
            goto failure;
        }
    }
    if (count) qsort(offsets, count, sizeof(*offsets), compare_offsets);
    for (size_t i = 0; i < count; ++i) {
        size_t offset = offsets[i].offset, index = offsets[i].index;
        if (offset == SIZE_MAX) continue;
        if (i && offsets[i - 1].offset == offset) {
            size_t previous = offsets[i - 1].index;
            model->objects[index] = model->objects[previous];
            model->object_sizes[index] = model->object_sizes[previous];
            continue;
        }
        result = object_extent(source, model_size, offset, texture_count,
                                &model->object_sizes[index]);
        if (result != MVC2_NATIVE_MODEL_OK) goto failure;
        model->objects[index] = (int*)(void*)(model->model_bytes + offset);
    }
    free(offsets);
    model->file.obj = model->objects;
    model->file.cnt = (int32_t)model->object_count;
    model->file.thd = model->textures;
    *output = model;
    return MVC2_NATIVE_MODEL_OK;
failure:
    free(offsets);
    mvc2_native_model_free(model);
    return result;
}

MVC2NativeModelStatus mvc2_native_model_decode(
    const void* model_bytes, size_t model_size,
    const void* texture_bytes, size_t texture_size, MVC2NativeModel** output) {
    return decode(model_bytes, model_size, texture_bytes, texture_size, NULL, 0, output);
}

MVC2NativeModelStatus mvc2_native_model_decode_mapped(
    const void* model_bytes, size_t model_size, const MVC2ModelTextureSpan* spans,
    size_t span_count, MVC2NativeModel** output) {
    if (!spans || !span_count) return MVC2_NATIVE_MODEL_ARGUMENT;
    for (size_t i = 0; i < span_count; ++i)
        if (!spans[i].bytes || !spans[i].size ||
            spans[i].size > UINT64_C(0x100000000) - spans[i].address)
            return MVC2_NATIVE_MODEL_ARGUMENT;
    return decode(model_bytes, model_size, NULL, 0, spans, span_count, output);
}

MVC2HostObjectFile* mvc2_native_model_file(MVC2NativeModel* model) {
    return model ? &model->file : NULL;
}

size_t mvc2_native_model_object_count(const MVC2NativeModel* model) {
    return model ? model->object_count : 0;
}
size_t mvc2_native_model_texture_count(const MVC2NativeModel* model) {
    return model ? model->texture_count : 0;
}
int* mvc2_native_model_object(MVC2NativeModel* model, size_t index, size_t* bytes) {
    if (bytes) *bytes = 0;
    if (!model || index >= model->object_count) return NULL;
    if (bytes) *bytes = model->object_sizes[index];
    return model->objects[index];
}
int* const* mvc2_native_model_objects(const MVC2NativeModel* model) {
    return model ? model->objects : NULL;
}
const NLthd* mvc2_native_model_textures(const MVC2NativeModel* model) {
    return model ? model->textures : NULL;
}
const NLthd* mvc2_native_model_texture(const MVC2NativeModel* model, size_t index) {
    return model && index < model->texture_count ? &model->textures[index] : NULL;
}
size_t mvc2_native_model_texture_source_bytes(const MVC2NativeModel* model, size_t index) {
    return model && index < model->texture_count ? model->texture_sizes[index] : 0;
}
int mvc2_native_model_load_texture(const MVC2NativeModel* model, size_t index, NL_TEX* target) {
    const NLthd* texture = mvc2_native_model_texture(model, index);
    return texture && target ? nl_tex_load(target, texture) : 0;
}
const char* mvc2_native_model_status_string(MVC2NativeModelStatus status) {
    switch (status) {
    case MVC2_NATIVE_MODEL_OK: return "ok";
    case MVC2_NATIVE_MODEL_ARGUMENT: return "missing native model input or output";
    case MVC2_NATIVE_MODEL_HEADER: return "truncated or invalid serialized model header";
    case MVC2_NATIVE_MODEL_REFERENCE: return "serialized reference is outside the owned asset view";
    case MVC2_NATIVE_MODEL_TEXTURE: return "invalid texture header, extent, or mesh texture index";
    case MVC2_NATIVE_MODEL_UNSUPPORTED_TEXTURE: return "unsupported texture storage layout or format";
    case MVC2_NATIVE_MODEL_PRIMITIVE: return "truncated, oversized, or invalid primitive record";
    case MVC2_NATIVE_MODEL_LIMIT: return "native model exceeds the documented allocation limit";
    case MVC2_NATIVE_MODEL_ALLOCATION: return "could not allocate native model storage";
    }
    return "unknown native model status";
}
