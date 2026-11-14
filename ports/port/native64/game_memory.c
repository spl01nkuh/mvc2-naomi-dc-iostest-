#include "game_memory.h"
#include "../include/host_memory.h"

#include <string.h>

_Alignas(64) unsigned char host_native64_ram[MVC2_NATIVE_RAM_SIZE];
static MVC2Memory *game_memory;

void *host_native64_ram_pointer(uint32_t address, uint32_t size) {
    size_t offset;
    if (mvc2_memory_offset(address, size, &offset) != MVC2_MEMORY_OK) return NULL;
    return host_native64_ram + offset;
}

MVC2Memory *mvc2_game_memory_open(void) {
    if (!game_memory)
        game_memory = mvc2_memory_borrow_ram(host_native64_ram, sizeof host_native64_ram);
    return game_memory;
}

void mvc2_game_memory_close(void) {
    mvc2_memory_destroy(game_memory);
    game_memory = NULL;
}

void mvc2_game_memory_reset(void) {
    if (game_memory) mvc2_memory_reset(game_memory);
    else memset(host_native64_ram, 0, sizeof host_native64_ram);
}
