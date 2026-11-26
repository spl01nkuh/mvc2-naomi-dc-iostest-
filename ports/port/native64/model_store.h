/* Native model ownership keyed by the game's raw asset-buffer addresses. */
#ifndef MVC2_NATIVE_MODEL_STORE_H
#define MVC2_NATIVE_MODEL_STORE_H
#include <stddef.h>
#include <stdint.h>
#include "../include/host_assets.h"

/* Call after a complete write of one decoded file to shared native RAM.
 * Writes invalidate overlapping file extents. No serialized pointers are
 * installed in RAM. The owning game thread serializes all operations. */
int mvc2_model_store_written(void* destination, size_t size);
/* Decode on first use or after either source file changes. The model's first
 * linked texture address identifies its separately loaded texture file.
 * Returns NULL for missing/invalid files. A failed refresh preserves the old
 * owner and descriptors, but never reports that refresh as successful.
 * Successful refreshes retain the returned OBJ_FILE descriptor's address;
 * its mesh/table pointers may change. Reset releases all stored owners. */
MVC2HostObjectFile* mvc2_model_store_get(void* model_bytes);
/* Resolve texture references across the complete bounded game RAM arena and
 * its documented aliases. The named bank must be a registered loaded file
 * and participates in cache invalidation alongside the model source. */
MVC2HostObjectFile* mvc2_model_store_get_mapped(void* model_bytes, uint32_t texture_address);
/* Diagnostic for the latest get attempt, valid until the next attempt or
 * reset. Empty after success. All store calls use the owning game thread. */
const char* mvc2_model_store_last_error(void);
/* The arcade card notice references an unpacked texture. Decode the registered
 * packed file and bind its bytes without rewriting a serialized pointer. */
MVC2HostObjectFile* mvc2_model_store_get_packed_texture(void* model_bytes,
                                                       const void* packed_bytes);
void mvc2_model_store_reset(void);
size_t mvc2_model_store_texture_bytes(const MVC2HostObjectFile* model, size_t texture);
/* Decode a raw word-LZ stream from a registered file and return its byte count. */
size_t mvc2_model_store_unpack_file(const void* file, size_t start, void* destination, size_t capacity);
int mvc2_model_store_unpack_texture(const MVC2HostObjectFile* model, size_t texture,
                                    const void* file);
/* Decode one picture part into a mapped model texture using both registered bounds. */
int mvc2_model_store_unpack_picture(const MVC2HostObjectFile* model, size_t texture,
                                    const void* picture, size_t part);
#endif
