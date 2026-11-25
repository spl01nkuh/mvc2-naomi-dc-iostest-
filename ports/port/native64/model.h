/* Native descriptors for the serialized model/texture boundary. This is an
 * asset decoder, not a widened overlay of OBJ_FILE or NLthd on ROM bytes. */
#ifndef MVC2_NATIVE64_MODEL_H
#define MVC2_NATIVE64_MODEL_H

#include <stddef.h>
#include "nl_port.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MVC2NativeModel MVC2NativeModel;

typedef enum MVC2NativeModelStatus {
    MVC2_NATIVE_MODEL_OK = 0,
    MVC2_NATIVE_MODEL_ARGUMENT,
    MVC2_NATIVE_MODEL_HEADER,
    MVC2_NATIVE_MODEL_REFERENCE,
    MVC2_NATIVE_MODEL_TEXTURE,
    MVC2_NATIVE_MODEL_UNSUPPORTED_TEXTURE,
    MVC2_NATIVE_MODEL_PRIMITIVE,
    MVC2_NATIVE_MODEL_LIMIT,
    MVC2_NATIVE_MODEL_ALLOCATION
} MVC2NativeModelStatus;

/* Inputs are immutable serialized files, not process-address ranges. The
 * model's linked base is inferred from its object-table address minus 16, as
 * obj_file_reloc does. Texture addresses are relative to the first texture's
 * linked address and resolve in the separately loaded texture payload.
 *
 * On success the result owns aligned copies of both payloads and separately
 * allocated native pointer/texture tables. The original 32-bit address words
 * are never replaced with host pointers. Relative vertex references retain
 * their original positions in the copied model blob. The native NLthd table
 * has a zero-width terminator and can be passed directly to the existing
 * nl_tex_load/nlObjPutLoadTexture backend.
 *
 * Bounds, mesh lengths, primitive records/references, texture indices, and
 * the supported texture formats' required source extents are validated. This
 * includes four-byte mesh/reference alignment and two-byte payload alignment
 * for direct/VQ textures; paletted byte data may start at an odd offset. This
 * does not harden every renderer operation or resolve SDK/global render state.
 * Model and texture inputs are each limited to 64 MiB, with at most 16384
 * object entries and NL_MAX_TEX texture entries. Failure leaves *output intact.
 */
MVC2NativeModelStatus mvc2_native_model_decode(
    const void* model_bytes, size_t model_size,
    const void* texture_bytes, size_t texture_size,
    MVC2NativeModel** output);

/* Some arcade models mix a loaded texture bank with textures generated in
 * game RAM. Resolve their original addresses against the specified spans.
 * Texture bytes remain backed by the caller until the model is released. */
typedef struct {
    uint32_t address;
    uint8_t* bytes;
    size_t size;
} MVC2ModelTextureSpan;
MVC2NativeModelStatus mvc2_native_model_decode_mapped(
    const void* model_bytes, size_t model_size, const MVC2ModelTextureSpan* spans,
    size_t span_count, MVC2NativeModel** output);
void mvc2_native_model_free(MVC2NativeModel* model);
/* Same native type as HOST_NATIVE64's OBJ_FILE, usable by game code without
 * aliasing serialized bytes. Owned by model; structural fields must stay
 * unchanged and the descriptor must not outlive its owning model. */
MVC2HostObjectFile* mvc2_native_model_file(MVC2NativeModel* model);

size_t mvc2_native_model_object_count(const MVC2NativeModel* model);
size_t mvc2_native_model_texture_count(const MVC2NativeModel* model);
/* Object bytes remain the original 32-bit mesh/vertex format. Material/UV
 * changes on this owned copy are allowed; modifying structural lengths or
 * relative references invalidates the decoder's checked bounds. */
int* mvc2_native_model_object(MVC2NativeModel* model, size_t index,
                              size_t* object_bytes);
int* const* mvc2_native_model_objects(const MVC2NativeModel* model);
const NLthd* mvc2_native_model_textures(const MVC2NativeModel* model);
const NLthd* mvc2_native_model_texture(const MVC2NativeModel* model, size_t index);
size_t mvc2_native_model_texture_source_bytes(const MVC2NativeModel* model, size_t index);
/* Explicitly opts a decoded descriptor into the existing texture backend.
 * Returns the backend's success result; the target retains its normal ownership
 * contract. This is not a submission to the GPU or an ARM64 game boot path. */
int mvc2_native_model_load_texture(const MVC2NativeModel* model, size_t index,
                                   NL_TEX* target);
const char* mvc2_native_model_status_string(MVC2NativeModelStatus status);

#ifdef __cplusplus
}
#endif
#endif
