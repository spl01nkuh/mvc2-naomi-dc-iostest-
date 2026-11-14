/* Native descriptors for thd_file_load's standalone serialized texture files. */
#ifndef MVC2_NATIVE64_TEXTURE_FILE_H
#define MVC2_NATIVE64_TEXTURE_FILE_H
#include <stddef.h>
#include "../include/host_assets.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MVC2NativeTextureFile MVC2NativeTextureFile;
typedef enum MVC2NativeTextureFileStatus {
    MVC2_NATIVE_TEXTURE_FILE_OK = 0,
    MVC2_NATIVE_TEXTURE_FILE_ARGUMENT,
    MVC2_NATIVE_TEXTURE_FILE_HEADER,
    MVC2_NATIVE_TEXTURE_FILE_REFERENCE,
    MVC2_NATIVE_TEXTURE_FILE_TEXTURE,
    MVC2_NATIVE_TEXTURE_FILE_UNSUPPORTED,
    MVC2_NATIVE_TEXTURE_FILE_LIMIT,
    MVC2_NATIVE_TEXTURE_FILE_ALLOCATION
} MVC2NativeTextureFileStatus;

/* Reads little-endian 16-byte NLthd records from the start of a file. Active
 * addresses are positive signed 32-bit file-relative offsets. The first full
 * record whose serialized address is <=0 ends the table. Every payload must
 * lie after the complete header table and fit its actual supported texture
 * layout, including mipmap prefixes and VQ codebooks. Direct/VQ data requires
 * two-byte file-offset alignment; byte palettes may use an odd offset.
 *
 * Success owns an aligned immutable file copy and a separate native NLthd
 * table, usable by nlSpriteLoadTexture / nlDispStripLoadTexture. Its terminal
 * record is canonical {-1,-1,-1,NULL,-1}: negative type terminates the sprite
 * backend even for files with zero-address/zero-filled serialized sentinels.
 * Serialized sentinel pointers are never materialized as native addresses.
 * Input is unchanged and may be released immediately. Keep the result alive
 * while any descriptor or its source pointer is retained; structure/payload
 * mutation through NLthd's legacy writable fields violates the checked view.
 * Backend texture loads copy texels and can outlive this file.
 *
 * Files are limited to 64 MiB and at most NL_MAX_TEX active textures. Empty
 * files require one complete sentinel record. Failure leaves *output intact.
 */
MVC2NativeTextureFileStatus mvc2_native_texture_file_decode(
    const void* bytes, size_t size, MVC2NativeTextureFile** output);
void mvc2_native_texture_file_free(MVC2NativeTextureFile* file);
size_t mvc2_native_texture_file_count(const MVC2NativeTextureFile* file);
const NLthd* mvc2_native_texture_file_headers(const MVC2NativeTextureFile* file);
const NLthd* mvc2_native_texture_file_header(const MVC2NativeTextureFile* file, size_t index);
size_t mvc2_native_texture_file_source_bytes(const MVC2NativeTextureFile* file, size_t index);
const char* mvc2_native_texture_file_status_string(MVC2NativeTextureFileStatus status);

#ifdef __cplusplus
}
#endif
#endif
