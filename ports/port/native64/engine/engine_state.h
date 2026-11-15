#ifndef MVC2_NATIVE_ENGINE_STATE_H
#define MVC2_NATIVE_ENGINE_STATE_H

/* Owning game thread only. Capture once before any engine initializer. The
 * engine is C: writable initialized data is preserved and its initial BSS
 * must be zero. Repeated calls retain the same original checkpoint.
 * Returns zero for allocation failure, late capture, or a compiler without
 * writable-section support. The production Apple build uses Clang. */
int mvc2_engine_checkpoint(void);

/* Only after sound/board shutdown, renderer workers joined, and external
 * provider handles closed. Restores game and backend globals, including
 * local statics. Host session state and coroutines are outside this span. */
void mvc2_engine_restore(void);

#endif
