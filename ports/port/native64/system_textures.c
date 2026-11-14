#include "system_textures.h"
#include "texture_file.h"
#include "nl_port.h"
#include <string.h>

#define EMPTY_HEADER {-1, -1, -1, NULL, -1}
NLthd sys_font_thd[NL_MAX_TEX + 1] = {EMPTY_HEADER};
NLthd sys_spr_thd[NL_MAX_TEX + 1] = {EMPTY_HEADER};
static MVC2NativeTextureFile* owners[2];

int mvc2_system_textures_load(int file_number, const void* bytes, size_t size) {
    if (file_number != 1 && file_number != 2) return 0;
    MVC2NativeTextureFile* next = NULL;
    if (mvc2_native_texture_file_decode(bytes, size, &next) != MVC2_NATIVE_TEXTURE_FILE_OK) return 0;
    size_t count = mvc2_native_texture_file_count(next);
    NLthd* table = file_number == 1 ? sys_font_thd : sys_spr_thd;
    memset(table, 0, (NL_MAX_TEX + 1) * sizeof(*table));
    memcpy(table, mvc2_native_texture_file_headers(next), (count + 1) * sizeof(*table));
    MVC2NativeTextureFile* previous = owners[file_number - 1];
    owners[file_number - 1] = next;
    mvc2_native_texture_file_free(previous);
    return 1;
}
void mvc2_system_textures_reset(void) {
    const NLthd empty = EMPTY_HEADER;
    memset(sys_font_thd, 0, sizeof sys_font_thd);
    memset(sys_spr_thd, 0, sizeof sys_spr_thd);
    sys_font_thd[0] = sys_spr_thd[0] = empty;
    for (unsigned i = 0; i < 2; ++i) {
        mvc2_native_texture_file_free(owners[i]);
        owners[i] = NULL;
    }
}
