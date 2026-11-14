/* Generated player/animation blocks through the real pl_data_set, lz_unpack16
 * and col_set routines. No dummy loaders, game callbacks or ROM assets. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "player.h"
#include "game_memory.h"
#include "host_memory.h"

extern void pl_data_set(PLAYER_WORK* player, int slot);

static void pack_literals(uint16_t* destination, const uint16_t* words, size_t count) {
    assert(count % 16 == 0);
    while (count) {
        *destination++ = 0; /* sixteen literal words */
        memcpy(destination, words, 16 * sizeof(*words));
        destination += 16; words += 16; count -= 16;
    }
    *destination++ = 0x8000; /* copy of zero words ends the stream */
    *destination++ = 0;
    *destination = 0;
}
int main(void) {
    MVC2Memory* memory = mvc2_game_memory_open();
    assert(memory);
    mvc2_game_memory_reset();
    _Alignas(4) uint16_t animation[512];
    for (size_t i = 0; i < 512; ++i) animation[i] = (uint16_t)(0x1200 + i);
    const int32_t parts[9] = {0x40, 0x80, 0xc0, 0x100, 0x140, 0x180, 0x1c0, 0x200, 0x240};
    memcpy(animation, parts, sizeof parts);
    int32_t* packed_table = host_native64_ram_pointer(0x0dd00000, 0x100);
    packed_table[0] = packed_table[52] = 0x100;
    pack_literals(host_native64_ram_pointer(0x0dd00100, 0x1000), animation, 512);
    for (int slot = 0; slot < 8; ++slot) {
        uint8_t* data = pl_data_adrs_tbl[slot];
        assert(data >= host_native64_ram && data + 0x150000 <= host_native64_ram + MVC2_NATIVE_RAM_SIZE);
        assert(spr_set_buff_tbl[slot] >= host_native64_ram && pic_adrs_tbl[slot] != NULL);
        const int32_t offsets[5] = {0x20, 0x40, 0x100, 0x900, 0xa00};
        memcpy(data, offsets, sizeof offsets);
        uint16_t palette[128];
        for (unsigned i = 0; i < 128; ++i) palette[i] = (uint16_t)(0xa000 + slot * 128 + i);
        memcpy(data + offsets[2], palette, sizeof palette);
        PLAYER_WORK player = {0};
        player.load_type = slot == 7 ? 52 : 0;
        pl_data_set(&player, slot);
        assert(player.pl_data_adrs == (int32_t*)(void*)data);
        assert(player.cell_adrs == data + offsets[0] && player.spr_adrs == data + offsets[1]);
        assert((void*)player.catch_pos_tbl == data + offsets[3]);
        assert(player.ef_pos_tbl == data + offsets[4] && player.spr_set_buff == spr_set_buff_tbl[slot]);
        assert(player.col_no == col_no_tbl[slot] && player.col_cnt == col_cnt_tbl[player.load_type]);
        assert(player.char_ofs == (slot == 7 ? 0x200 : 0x100));
        uint8_t* unpacked = data + 0x130000;
        assert(memcmp(unpacked, animation, sizeof animation) == 0);
        assert(player.char_adrs == unpacked + parts[0]);
        assert((void*)player.frame_box_tbl == unpacked + parts[1]);
        assert((void*)player.hit_box_tbl == unpacked + parts[2]);
        assert((void*)player.hit_data_tbl == unpacked + parts[3]);
        assert((void*)player.cpu_adrs[0] == unpacked + parts[4]);
        assert((void*)player.cpu_adrs[1] == unpacked + parts[5]);
        assert((void*)player.cpu_adrs[2] == unpacked + parts[6]);
        assert((void*)player.cpu_answer_adrs == unpacked + parts[7]);
        assert((void*)player.cpu_ex_adrs == unpacked + parts[8]);
        for (int line = 0; line < 8; ++line) {
            COL_WORK* colors = &col_work[col_no_tbl[slot] + line];
            assert(colors->trans_flag == 1);
            assert(memcmp(colors->col, palette + line * 16, 16 * sizeof(uint16_t)) == 0);
        }
    }
    mvc2_game_memory_close();
    puts("Native player loading: all eight buffers, real animation decompression, fight/CPU references and palettes passed.");
    return 0;
}
