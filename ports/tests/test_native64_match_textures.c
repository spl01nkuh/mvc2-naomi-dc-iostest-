/* The arcade match gauge uses both a ROM texture bank and live portrait RAM.
 * Exercise the original gauge assembler, word-LZ unpacker and texture uploader
 * on serialized generated assets, with actual game work/global definitions. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "player.h"
#include "load.h"
#include "host_memory.h"
#include "model_store.h"
#include "nl_port.h"

extern void tex_load_gauge(void);
static char frame_error[512];

void mvc2_gameplay_fail_frame(const char* message)
{
    snprintf(frame_error, sizeof frame_error, "%s", message);
}

enum { TEXTURES = 22, OBJECT_COUNT = 122, HEADERS = 16 + OBJECT_COUNT * 4,
       OBJECTS = HEADERS + (TEXTURES + 1) * 16, OBJECT_BYTES = 108,
       MODEL_SIZE = OBJECTS + TEXTURES * OBJECT_BYTES, TEX_BYTES = 0x800,
       BANK_SIZE = TEXTURES * TEX_BYTES };

static void word(uint8_t* bytes, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) bytes[i] = (uint8_t)(value >> (i * 8));
}

static uint8_t* gauge_fixture(void)
{
    uint8_t* bytes = host_native64_ram_pointer(DM00_OBJ_ADRS, MODEL_SIZE);
    uint8_t* bank = host_native64_ram_pointer(DM00_TEX_ADRS, BANK_SIZE);
    assert(bytes && bank);
    memset(bytes, 0, MODEL_SIZE);
    memset(bank, 0x42, BANK_SIZE);
    word(bytes, DM00_OBJ_ADRS + 16);
    word(bytes + 4, OBJECT_COUNT);
    word(bytes + 8, DM00_OBJ_ADRS + HEADERS);
    for (unsigned i = 0; i < OBJECT_COUNT; ++i)
        word(bytes + 16 + i * 4, DM00_OBJ_ADRS + OBJECTS + (i % TEXTURES) * OBJECT_BYTES);
    for (unsigned i = 0; i < TEXTURES; ++i) {
        uint8_t* header = bytes + HEADERS + i * 16;
        uint8_t* object = bytes + OBJECTS + i * OBJECT_BYTES;
        word(header, 32u | 32u << 16);
        word(header + 4, 0x901); /* 32x32 RGB565 linear */
        word(header + 8, i == 9 ? 0x0ce60000u : DM00_TEX_ADRS + i * TEX_BYTES);
        word(object + 24, 0x80000000u); /* one empty, textured material */
        word(object + 24 + 32, i);
    }
    assert(mvc2_model_store_written(bytes, MODEL_SIZE));
    assert(mvc2_model_store_written(bank, BANK_SIZE));
    return bytes;
}

static void pictures(void)
{
    static const unsigned characters[6] = {0, 16, 29, 38, 47, 58};
    for (unsigned i = 0; i < 6; ++i) {
        uint32_t address = 0x0c900000u + i * 0x100;
        Sint32* parts = host_native64_ram_pointer(address, 64);
        assert(parts);
        for (unsigned part = 0; part < 4; ++part) parts[part] = 16;
        Uint16* stream = (Uint16*)((uint8_t*)parts + 16);
        /* One literal and a 4095-word overlapping back-reference, followed
           by the real unpacker's zero-copy terminator: 8192 output bytes. */
        stream[0] = 0x6000;
        stream[1] = (Uint16)(0x6000 + i);
        stream[2] = 1;
        stream[3] = 4095;
        stream[4] = stream[5] = 0;
        pl_work[i].pic_adrs = parts;
        pl_work[i].load_type = (Uint8)characters[i];
        pl_work[i].pl_personal = (Uint8)(i % 3);
    }
}

int main(void)
{
    static const unsigned portrait_slots[6] = {0, 3, 1, 4, 2, 5};
    Sys = &sys_work;
    Sys->cpu_level_type = 1;
    uint8_t* bytes = gauge_fixture();
    pictures();
    assert(!mvc2_model_store_get(bytes)); /* scratch texture is outside the ROM bank */
    tex_load_gauge();
    assert(!frame_error[0] && obj_dm00 && obj_dm00->cnt == OBJECT_COUNT);
    assert(obj_dm00->thd[9].address == host_native64_ram_pointer(0x0ce60000, TEX_BYTES));
    for (unsigned player = 0; player < 6; ++player) {
        for (unsigned part = 0; part < 2; ++part) {
            unsigned slot = portrait_slots[player] + 10 + part * 6;
            const Uint16* pixels = obj_dm00->thd[slot].address;
            assert(pixels == host_native64_ram_pointer(DM00_TEX_ADRS + slot * TEX_BYTES, TEX_BYTES));
            for (unsigned pixel = 0; pixel < TEX_BYTES / 2; ++pixel)
                assert(pixels[pixel] == 0x6000 + player);
            assert(nl_tex[0xc90 + slot].loaded);
            assert(!memcmp(nl_tex[0xc90 + slot].tex.texels, pixels, TEX_BYTES));
        }
    }

    MVC2HostObjectFile* previous = obj_dm00;
    word(bytes + HEADERS + 9 * 16 + 8, 0x0dfffffe); /* valid address, insufficient complete texture */
    assert(mvc2_model_store_written(bytes, MODEL_SIZE));
    tex_load_gauge();
    assert(!obj_dm00);
    assert(strstr(frame_error, "match gauge") && strstr(frame_error, "0D082000"));
    assert(strstr(frame_error, "serialized reference"));
    assert(previous->thd[9].address == host_native64_ram_pointer(0x0ce60000, TEX_BYTES));

    /* A format-valid but incomplete model must stop before bg_init indexes
       the gauge objects through 121. */
    word(bytes + HEADERS + 9 * 16 + 8, 0x0ce60000);
    word(bytes + 4, 4);
    assert(mvc2_model_store_written(bytes, MODEL_SIZE));
    frame_error[0] = '\0';
    tex_load_gauge();
    assert(!obj_dm00 && strstr(frame_error, "122 required objects"));
    nl_tex_free_all();
    mvc2_model_store_reset();
    puts("Native match gauge: ROM/live textures, six portraits, real LZ/upload, and controlled invalid-asset failure passed.");
    return 0;
}
