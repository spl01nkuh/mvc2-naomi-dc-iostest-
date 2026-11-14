#include "model_store.h"
#include "model.h"
#include "../include/host_memory.h"
#include <stdlib.h>
#include <string.h>

enum { MAX_FILES = 256, MAX_MODELS = 64 };
typedef struct FileExtent {
    size_t offset, size;
    uint64_t generation;
} FileExtent;
typedef struct ModelEntry {
    size_t offset;
    uint64_t model_generation, texture_generation;
    MVC2HostObjectFile file;
    MVC2NativeModel* owner;
} ModelEntry;
static FileExtent files[MAX_FILES];
static ModelEntry* models[MAX_MODELS];
static size_t file_count, model_count;
static uint64_t generation;

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
MVC2HostObjectFile* mvc2_model_store_get(void* model_bytes) {
    size_t offset;
    if (!ram_offset(model_bytes, 16, &offset)) return NULL;
    const FileExtent* source = find_file(offset);
    if (!source || source->size < 16) return NULL;
    const uint8_t* bytes = model_bytes;
    uint32_t table = le32(bytes), texture_table = le32(bytes + 8);
    if (table < 16 || texture_table < table - 16) return NULL;
    size_t relative = texture_table - (table - 16);
    if (relative > source->size || source->size - relative < 2) return NULL;
    const FileExtent* textures = NULL;
    if (bytes[relative] || bytes[relative + 1]) {
        if (source->size - relative < 16) return NULL;
        size_t texture_offset;
        if (mvc2_memory_offset(le32(bytes + relative + 8), 1, &texture_offset) != MVC2_MEMORY_OK)
            return NULL;
        textures = find_file(texture_offset);
        if (!textures) return NULL;
    }
    ModelEntry* entry = NULL;
    for (size_t i = 0; i < model_count; ++i)
        if (models[i]->offset == offset) { entry = models[i]; break; }
    uint64_t texture_generation = textures ? textures->generation : 0;
    if (entry && entry->model_generation == source->generation &&
        entry->texture_generation == texture_generation) return &entry->file;
    MVC2NativeModel* owner = NULL;
    if (mvc2_native_model_decode(bytes, source->size,
        textures ? host_native64_ram + textures->offset : NULL,
        textures ? textures->size : 0, &owner) != MVC2_NATIVE_MODEL_OK) return NULL;
    if (!entry) {
        if (model_count == MAX_MODELS) { mvc2_native_model_free(owner); return NULL; }
        entry = calloc(1, sizeof(*entry));
        if (!entry) { mvc2_native_model_free(owner); return NULL; }
        entry->offset = offset;
        models[model_count++] = entry;
    }
    MVC2NativeModel* previous = entry->owner;
    entry->file = *mvc2_native_model_file(owner);
    entry->owner = owner;
    entry->model_generation = source->generation;
    entry->texture_generation = texture_generation;
    mvc2_native_model_free(previous);
    return &entry->file;
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
}
