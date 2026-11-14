/* Lifetime of the native game's shared fixed-buffer storage. Creating a view
 * does not boot the game, load ROMs, or install serialized native bindings. */
#ifndef MVC2_NATIVE64_GAME_MEMORY_H
#define MVC2_NATIVE64_GAME_MEMORY_H
#include "memory.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Call from the owning game thread. Open is idempotent, preserves existing
 * bytes, and returns NULL if the wrapper cannot be allocated. Its handle is
 * owned by this provider: callers must not pass it to mvc2_memory_destroy.
 * Close invalidates the handle and its bindings but preserves RAM and all
 * raw/static buffer pointers. A subsequent open starts a fresh binding registry.
 * Bound native objects remain caller-owned through close and reset. */
MVC2Memory *mvc2_game_memory_open(void);
void mvc2_game_memory_close(void);
/* Clears all shared RAM and, if open, invalidates its object bindings. This
 * also works before open or after close and never allocates a wrapper. */
void mvc2_game_memory_reset(void);

#ifdef __cplusplus
}
#endif
#endif
