#include "texture_file.h"
#include "texture_layout.h"
#include "nl_port.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FILE_BYTES ((size_t)64 * 1024 * 1024)
enum { SERIALIZED_HEADER_BYTES = 16 };

struct MVC2NativeTextureFile {
    uint8_t* bytes;
    NLthd* headers;
    size_t* source_sizes;
    size_t count;
};

static uint16_t le16(const uint8_t* bytes) {
    return (uint16_t)((uint16_t)bytes[0] | (uint16_t)bytes[1] << 8);
}
static uint32_t le32(const uint8_t* bytes) {
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
        (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

void mvc2_native_texture_file_free(MVC2NativeTextureFile* file) {
    if (!file) return;
    free(file->bytes);
    free(file->headers);
    free(file->source_sizes);
    free(file);
}

MVC2NativeTextureFileStatus mvc2_native_texture_file_decode(
    const void* bytes, size_t size, MVC2NativeTextureFile** output) {
    if (!bytes || !output) return MVC2_NATIVE_TEXTURE_FILE_ARGUMENT;
    if (size > MAX_FILE_BYTES) return MVC2_NATIVE_TEXTURE_FILE_LIMIT;
    const uint8_t* source = bytes;
    size_t count = 0, table_end = 0;
    for (;;) {
        if (table_end > size || SERIALIZED_HEADER_BYTES > size - table_end)
            return MVC2_NATIVE_TEXTURE_FILE_HEADER;
        uint32_t address = le32(source + table_end + 8);
        table_end += SERIALIZED_HEADER_BYTES;
        if (!address || address & UINT32_C(0x80000000)) break;
        if (count == NL_MAX_TEX) return MVC2_NATIVE_TEXTURE_FILE_LIMIT;
        ++count;
    }
    /* Validate the complete file before allocating or publishing descriptors. */
    for (size_t i = 0; i < count; ++i) {
        const uint8_t* entry = source + i * SERIALIZED_HEADER_BYTES;
        int16_t width = (int16_t)le16(entry), height = (int16_t)le16(entry + 2);
        size_t offset = le32(entry + 8), extent, alignment;
        MVC2TextureLayoutStatus status = mvc2_texture_source_extent(
            (int32_t)le32(entry + 4), (unsigned)width, (unsigned)height, &extent, &alignment);
        if (status == MVC2_TEXTURE_LAYOUT_INVALID) return MVC2_NATIVE_TEXTURE_FILE_TEXTURE;
        if (status != MVC2_TEXTURE_LAYOUT_OK) return MVC2_NATIVE_TEXTURE_FILE_UNSUPPORTED;
        if (offset < table_end || offset > size || extent > size - offset)
            return MVC2_NATIVE_TEXTURE_FILE_REFERENCE;
        if (offset % alignment) return MVC2_NATIVE_TEXTURE_FILE_TEXTURE;
    }
    MVC2NativeTextureFile* file = calloc(1, sizeof(*file));
    if (!file) return MVC2_NATIVE_TEXTURE_FILE_ALLOCATION;
    file->bytes = malloc(size);
    file->headers = calloc(count + 1, sizeof(*file->headers));
    file->source_sizes = count ? calloc(count, sizeof(*file->source_sizes)) : NULL;
    if (!file->bytes || !file->headers || (count && !file->source_sizes)) {
        mvc2_native_texture_file_free(file);
        return MVC2_NATIVE_TEXTURE_FILE_ALLOCATION;
    }
    memcpy(file->bytes, bytes, size);
    file->count = count;
    for (size_t i = 0; i < count; ++i) {
        const uint8_t* entry = file->bytes + i * SERIALIZED_HEADER_BYTES;
        NLthd* header = file->headers + i;
        header->width = (int16_t)le16(entry);
        header->height = (int16_t)le16(entry + 2);
        header->type = (int32_t)le32(entry + 4);
        header->address = file->bytes + le32(entry + 8);
        header->globl_index = (int32_t)le32(entry + 12);
        (void)mvc2_texture_source_extent(header->type, (unsigned)header->width,
            (unsigned)header->height, file->source_sizes + i, NULL);
    }
    file->headers[count] = (NLthd){-1, -1, -1, NULL, -1};
    *output = file;
    return MVC2_NATIVE_TEXTURE_FILE_OK;
}

size_t mvc2_native_texture_file_count(const MVC2NativeTextureFile* file) {
    return file ? file->count : 0;
}
const NLthd* mvc2_native_texture_file_headers(const MVC2NativeTextureFile* file) {
    return file ? file->headers : NULL;
}
const NLthd* mvc2_native_texture_file_header(const MVC2NativeTextureFile* file, size_t index) {
    return file && index < file->count ? file->headers + index : NULL;
}
size_t mvc2_native_texture_file_source_bytes(const MVC2NativeTextureFile* file, size_t index) {
    return file && index < file->count ? file->source_sizes[index] : 0;
}
const char* mvc2_native_texture_file_status_string(MVC2NativeTextureFileStatus status) {
    switch (status) {
    case MVC2_NATIVE_TEXTURE_FILE_OK: return "ok";
    case MVC2_NATIVE_TEXTURE_FILE_ARGUMENT: return "missing standalone texture input or output";
    case MVC2_NATIVE_TEXTURE_FILE_HEADER: return "truncated or missing standalone texture table terminator";
    case MVC2_NATIVE_TEXTURE_FILE_REFERENCE: return "texture payload overlaps the table or exceeds the file";
    case MVC2_NATIVE_TEXTURE_FILE_TEXTURE: return "invalid standalone texture dimensions or payload alignment";
    case MVC2_NATIVE_TEXTURE_FILE_UNSUPPORTED: return "unsupported standalone texture storage layout or format";
    case MVC2_NATIVE_TEXTURE_FILE_LIMIT: return "standalone texture file exceeds the allocation limit";
    case MVC2_NATIVE_TEXTURE_FILE_ALLOCATION: return "could not allocate standalone texture descriptors";
    }
    return "unknown standalone texture status";
}
