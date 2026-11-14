/* Native-owned RAM and serialized-address resolution for the ARM64 port.
 * No guest address is converted to a process pointer. This module does not
 * execute ROM code or resolve function pointers from data addresses. */
#ifndef MVC2_NATIVE64_MEMORY_H
#define MVC2_NATIVE64_MEMORY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MVC2_NATIVE_RAM_BASE UINT32_C(0x0c000000)
#define MVC2_NATIVE_RAM_SIZE UINT32_C(0x02000000)

typedef struct MVC2Memory MVC2Memory;

typedef enum MVC2MemoryStatus {
    MVC2_MEMORY_OK = 0,
    MVC2_MEMORY_INVALID_ARGUMENT,
    MVC2_MEMORY_BAD_ADDRESS,
    MVC2_MEMORY_OUT_OF_RANGE,
    MVC2_MEMORY_NO_MEMORY,
    MVC2_MEMORY_BINDING_OVERLAP,
    MVC2_MEMORY_NOT_BOUND,
    MVC2_MEMORY_OBJECT_TOO_SMALL,
    MVC2_MEMORY_INVALID_PROGRAM,
    MVC2_MEMORY_ROM_READ_FAILED
} MVC2MemoryStatus;

/* Allocates 32 MiB at an ordinary native address, initially zero. The caller
 * serializes access. Reset clears RAM and invalidates all object bindings;
 * neither reset nor destroy frees caller-owned bound objects. */
MVC2Memory *mvc2_memory_create(void);
void mvc2_memory_destroy(MVC2Memory *memory);
void mvc2_memory_reset(MVC2Memory *memory);

/* Only the main-RAM ranges at 0x0c..., 0x8c... and 0xac... are recognized.
 * The byte directly after RAM is allowed only for a zero-length operation.
 * On failure, the offset output is unchanged. */
MVC2MemoryStatus mvc2_memory_offset(uint32_t address, size_t length, size_t *offset);
void *mvc2_memory_ram_pointer(MVC2Memory *memory, uint32_t address, size_t length);
const void *mvc2_memory_ram_const_pointer(const MVC2Memory *memory,
                                         uint32_t address, size_t length);
MVC2MemoryStatus mvc2_memory_read(const MVC2Memory *memory, uint32_t address,
                                  void *output, size_t length);
MVC2MemoryStatus mvc2_memory_write(MVC2Memory *memory, uint32_t address,
                                   const void *input, size_t length);
/* Word accesses support unaligned serialized addresses and little-endian
 * bytes regardless of native alignment or byte order. Failed reads do not
 * change the output; failed writes do not change RAM. */
MVC2MemoryStatus mvc2_memory_read_u32(const MVC2Memory *memory, uint32_t address,
                                      uint32_t *output);
MVC2MemoryStatus mvc2_memory_write_u32(MVC2Memory *memory, uint32_t address,
                                       uint32_t value);

/* A binding identifies a separately decoded native object. Its serialized
 * byte span and native allocation size are deliberately independent. Lookup
 * accepts only the exact normalized start address: an interior guest offset
 * is never applied to a widened native record. Distinct serialized spans
 * cannot overlap; a mirror of an existing binding is the same address.
 * Raw RAM access above remains raw bytes and never returns a bound object.
 * Raw writes do not synchronize separately decoded objects. Before changing
 * serialized content with an active binding, its owner must unbind/redecode
 * that object; retained mutable RAM views carry the same requirement.
 * The caller owns each native object and must unbind it before releasing it.
 * This registry stores data pointers only; callbacks require a separately
 * typed resolver, never a cast from a RAM word or from one of these objects. */
MVC2MemoryStatus mvc2_memory_bind_object(MVC2Memory *memory, uint32_t address,
                                         size_t serialized_bytes, void *object,
                                         size_t native_bytes);
MVC2MemoryStatus mvc2_memory_unbind_object(MVC2Memory *memory, uint32_t address);
MVC2MemoryStatus mvc2_memory_resolve_object(const MVC2Memory *memory,
                                            uint32_t address,
                                            size_t required_native_bytes,
                                            void **output, size_t *native_bytes);

/* Reader returns one after writing exactly length bytes, zero on failure.
 * All arguments are validated against rom_size before a segment is read.
 * The 0x500-byte NAOMI header has up to eight {source,destination,length}
 * records at 0x360, terminated by source=0xffffffff when fewer than eight.
 * Segment loads follow table order, including overlaps as in naomi_board.c.
 * A successful load commits all segments and invalidates object bindings.
 * Invalid metadata, allocation failure or a partial/read failure leaves RAM
 * and bindings unchanged. At most one additional 32 MiB RAM image is allocated.
 * Existing 32-bit board_start/load_program is not redirected by this helper. */
typedef int (*MVC2MemoryROMRead)(void *context, uint32_t offset,
                                void *output, size_t length);
MVC2MemoryStatus mvc2_memory_load_program(MVC2Memory *memory,
                                          MVC2MemoryROMRead reader, void *context,
                                          uint32_t rom_size);

const char *mvc2_memory_status_string(MVC2MemoryStatus status);

#ifdef __cplusplus
}
#endif
#endif
