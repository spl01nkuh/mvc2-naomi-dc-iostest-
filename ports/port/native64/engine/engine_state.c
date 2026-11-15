#include "engine_state.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(MVC2_ENGINE_SECTION_SNAPSHOT)
#if defined(__APPLE__)
extern unsigned char engine_data_start[] __asm("section$start$__DATA$__mvc2_data");
extern unsigned char engine_data_end[] __asm("section$end$__DATA$__mvc2_data");
extern unsigned char engine_bss_start[] __asm("section$start$__DATA$__mvc2_bss");
extern unsigned char engine_bss_end[] __asm("section$end$__DATA$__mvc2_bss");
#else
extern unsigned char engine_data_start[] __asm("__start_mvc2_data");
extern unsigned char engine_data_end[] __asm("__stop_mvc2_data");
extern unsigned char engine_bss_start[] __asm("__start_mvc2_bss");
extern unsigned char engine_bss_end[] __asm("__stop_mvc2_bss");
#endif

/* These live in normal host sections, outside the state they restore. */
static unsigned char* checkpoint;
static size_t data_size, bss_size;

int mvc2_engine_checkpoint(void) {
    if (checkpoint) return 1;
    uintptr_t data_begin = (uintptr_t)engine_data_start, data_end = (uintptr_t)engine_data_end;
    uintptr_t bss_begin = (uintptr_t)engine_bss_start, bss_end = (uintptr_t)engine_bss_end;
    if (data_end <= data_begin || bss_end <= bss_begin) return 0;
    size_t new_data_size = data_end - data_begin, new_bss_size = bss_end - bss_begin;
    for (size_t index = 0; index < new_bss_size; ++index)
        if (engine_bss_start[index]) return 0;
    unsigned char* saved = malloc(new_data_size);
    if (!saved) return 0;
    memcpy(saved, engine_data_start, new_data_size);
    data_size = new_data_size;
    bss_size = new_bss_size;
    checkpoint = saved;
    return 1;
}

void mvc2_engine_restore(void) {
    if (!checkpoint) return;
    memset(engine_bss_start, 0, bss_size);
    memcpy(engine_data_start, checkpoint, data_size);
}
#else
/* The original GCC build remains separate. It cannot apply Clang's section
 * pragma, so this diagnostic object build never advertises a valid reset. */
int mvc2_engine_checkpoint(void) { return 0; }
void mvc2_engine_restore(void) {}
#endif
