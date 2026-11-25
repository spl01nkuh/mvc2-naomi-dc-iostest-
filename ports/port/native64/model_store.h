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
/* The arcade select model also references portraits assembled separately in RAM. */
MVC2HostObjectFile* mvc2_model_store_get_select(void* model_bytes, uint32_t texture_address);
/* The arcade card notice references an unpacked texture. Decode the registered
 * packed file and bind its bytes without rewriting a serialized pointer. */
MVC2HostObjectFile* mvc2_model_store_get_packed_texture(void* model_bytes,
                                                       const void* packed_bytes);
void mvc2_model_store_reset(void);
#endif
