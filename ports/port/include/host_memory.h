/* Native backing for the game's fixed byte buffers. This does not translate
 * serialized pointers into widened native objects or executable callbacks. */
#ifndef MVC2_HOST_MEMORY_H
#define MVC2_HOST_MEMORY_H

#include "../native64/memory.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Process-lifetime storage at an ordinary native address, aligned to 64 bytes.
 * The owning game thread serializes all access, including reads through static
 * buffer tables. Only an explicit game-memory reset clears these bytes. */
extern unsigned char host_native64_ram[MVC2_NATIVE_RAM_SIZE];

/* Runtime addresses accept exactly the mirrors and bounds documented by
 * mvc2_memory_offset. Failure returns NULL. This helper does not open a game
 * session or allocate; its successful pointers remain valid across close/open. */
void *host_native64_ram_pointer(uint32_t address, uint32_t size);

/* For generated static pointer tables only: address must be a physical RAM
 * integer constant in [0x0c000000,0x0e000000). Mirrors and runtime inputs must
 * use the checked function above. The bit-field requires an integer constant
 * expression, and its negative width rejects invalid constants at compile time.
 * No serialized address is cast to a process pointer. */
#ifndef __cplusplus
#define HOST_NATIVE64_RAM_AT(address) \
    (host_native64_ram + ((address) - MVC2_NATIVE_RAM_BASE) + \
     0 * sizeof(struct { unsigned valid : \
         ((address) >= MVC2_NATIVE_RAM_BASE && \
          (address) < MVC2_NATIVE_RAM_BASE + MVC2_NATIVE_RAM_SIZE) ? 1 : -1; }))
#else
/* A template preserves the same constant-expression restrictions in C++. */
}
template<uint32_t Address> struct MVC2HostRAMConstant {
    static_assert(Address >= MVC2_NATIVE_RAM_BASE &&
                  Address < MVC2_NATIVE_RAM_BASE + MVC2_NATIVE_RAM_SIZE,
                  "native RAM table address must be a physical RAM constant");
    static constexpr size_t offset = Address - MVC2_NATIVE_RAM_BASE;
};
#define HOST_NATIVE64_RAM_AT(address) \
    (host_native64_ram + MVC2HostRAMConstant<(address)>::offset)
extern "C" {
#endif

#ifdef __cplusplus
}
#endif
#endif
