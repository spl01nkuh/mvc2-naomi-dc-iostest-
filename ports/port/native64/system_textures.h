#ifndef MVC2_NATIVE_SYSTEM_TEXTURES_H
#define MVC2_NATIVE_SYSTEM_TEXTURES_H
#include <stddef.h>
#include "../include/host_assets.h"

/* These are real native arrays: the original sys_task declarations name
 * arrays, so a pointer variable would have a different link-time ABI. */
extern NLthd sys_font_thd[];
extern NLthd sys_spr_thd[];
/* Owning game thread only. Files 1 and 2 are the system font and sprites.
 * Failure preserves the prior descriptors and their payload ownership. */
int mvc2_system_textures_load(int file_number, const void* bytes, size_t size);
void mvc2_system_textures_reset(void);
#endif
