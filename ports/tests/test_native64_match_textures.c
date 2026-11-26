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
extern void tex_set_st_demo(void);
extern void tex_load_ranking(void);
extern void tex_set_ranking(void);
static char frame_error[512];
static int fight_loads, intro_effects, draw_requests;
static int rank_effects, loaded_portraits;
static int broken_ranking_portrait;
static int score_fallbacks;

/* Run the real transition with generated portrait data. The remaining
 * callbacks count scene activity without starting the board or mixer. */
static void load_fixture_textures(int scene);
#define TexLoad load_fixture_textures
#include "../../src/start_demo.c"
#include "../../src/ranking.c"
#undef TexLoad

static void load_fixture_textures(int scene) {
    if (scene == 6) tex_set_st_demo();
    else {
        assert(scene == 10);
        tex_load_ranking();
        tex_set_ranking();
    }
}
void fight_load_start(int only) { assert(!only); ++fight_loads; }
void st_demo_ef0_set(void) { ++intro_effects; }
void st_demo_ef1_set(int no) { assert(no >= 0 && no < 6); ++intro_effects; }
void st_demo_ef2_set(int no, int side) { assert(no >= 0 && no < 6 && (side == 0 || side == 1)); ++intro_effects; }
void disp_front_stage2(void) {}
void SysTransSet(void (*draw)(void)) { assert(draw == disp_front_stage || draw == disp_front_stage2); ++draw_requests; }
void Sound_Heart(void) {}
void disp_front_stage(void) {}
void work_init(void) {}
void work_list_move(Uint8 list) { (void)list; }
void light_init(void) {}
void light_dummy(void) {}
void bright_init(void) {}
void Palette_Init(void) {}
void Texture_Init(void) {}
void song_req(Uint8 no) { (void)no; }
void na_req(Uint8 no) { (void)no; }
void scr_work_init(void) {}
void fade_set(int color, int time, int direction) { (void)color; (void)time; (void)direction; }
void fog_init(void) {}
void fog_set(void) {}
void back_color_set(Uint32 color) { (void)color; }
void se_vol_down(Uint16 pack, Uint8 speed) { (void)pack; (void)speed; }
void sound_all_off(void) {}
void se_all_off(void) {}
void se_vol_up(Uint16 pack, Uint8 speed) { (void)pack; (void)speed; }
void stream_stop(void) {}
void fade_out_set(Uint16 pack, Uint8 speed) { (void)pack; (void)speed; }
void light_dir_set(VEC3* direction) { (void)direction; }
void light_amb_set(Float ambient) { (void)ambient; }
void scr_move(void) {}
void rank_bg_set(int no) { (void)no; ++rank_effects; }
void rank_frame_set(int no) { (void)no; ++rank_effects; }
void rank_page_set(int no, int page) { (void)no; (void)page; ++rank_effects; }
void rank_mark_set(int no, int line) { (void)no; (void)line; ++rank_effects; }
void STrPrint(Uint8 x, Uint8 y, Uint8 color, const char* text, ...) {
    assert(x == 50 && y == 56 && color == 11 && !strcmp(text, "SCORE %x%08x"));
    ++score_fallbacks;
}

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
        assert(mvc2_model_store_written(parts, 64));
        pl_work[i].pic_adrs = parts;
        pl_work[i].load_type = (Uint8)characters[i];
        pl_work[i].pl_personal = (Uint8)(i % 3);
    }
}

static void team_introduction(const uint8_t* gauge)
{
    static const unsigned slots[] = {0, 3, 1, 4, 2, 5};
    uint8_t* scene = host_native64_ram_pointer(DM02_OBJ_ADRS, MODEL_SIZE);
    uint8_t* bank = host_native64_ram_pointer(DM02_TEX_ADRS, TEXTURES * 8192);
    assert(scene && bank);
    memcpy(scene, gauge, MODEL_SIZE);
    word(scene, DM02_OBJ_ADRS + 16);
    word(scene + 4, OBJECT_COUNT);
    word(scene + 8, DM02_OBJ_ADRS + HEADERS);
    for (unsigned i = 0; i < OBJECT_COUNT; ++i)
        word(scene + 16 + i * 4, DM02_OBJ_ADRS + OBJECTS + (i % TEXTURES) * OBJECT_BYTES);
    for (unsigned i = 0; i < TEXTURES; ++i) {
        uint8_t* header = scene + HEADERS + i * 16;
        word(header, 64u | 64u << 16);
        word(header + 8, i == 9 ? 0x0ce60000u : DM02_TEX_ADRS + i * 8192);
    }
    assert(mvc2_model_store_written(scene, MODEL_SIZE));
    assert(mvc2_model_store_written(bank, TEXTURES * 8192));
    assert(!mvc2_model_store_get(scene));
    Sys->routine_0 = 2;
    Sys->routine_1 = 0;
    StartDemoMain();
    assert(obj_dm02 && Sys->routine_0 == 2 && Sys->routine_1 == 1);
    assert(fight_loads == 1 && draw_requests == 1 && intro_effects == 19);
    for (unsigned i = 0; i < 6; ++i) {
        const Uint16* pixels = obj_dm02->thd[slots[i] + 2].address;
        assert(pixels[0] == 0x6000 + i && pixels[4095] == pixels[0]);
    }
    pl_work[0].pic_adrs[2] = 64; /* part points beyond the registered picture */
    Sys->routine_1 = 0;
    StartDemoMain();
    assert(!obj_dm02 && Sys->routine_0 == 3 && !Sys->routine_1);
    assert(fight_loads == 2 && draw_requests == 1 && intro_effects == 19);
}

int file_load(int no, void* address, int purge)
{
    assert(no == FILE_TEX(FILE_DM06POL) && !purge);
    assert(address == host_native64_ram_pointer(DM06_TEX_ADRS, 1));
    loaded_portraits = 0;
    return 0x37000;
}

Uint32 file_load2(Sint32 no, void* address, Sint32 purge)
{
    assert(no == vs_file_tbl[vs_sys_work.team_w[Sys->rank_side][loaded_portraits]->load_type]);
    assert(purge && address == Sys->tex_buff + 0x40000);
    const Uint16 data[] = {0x6000, 0x3110 + loaded_portraits, 1, 4095, 0, 0};
    size_t bytes = broken_ranking_portrait && loaded_portraits == 2 ? 4 : sizeof data;
    memcpy(address, data, bytes);
    assert(mvc2_model_store_written(address, bytes));
    ++loaded_portraits;
    return (Uint32)bytes;
}

/* The ranking scene points to mutable score pages outside the loaded bank. */
static void post_match_ranking(const uint8_t* gauge)
{
    uint8_t* scene = host_native64_ram_pointer(DM06_OBJ_ADRS, MODEL_SIZE);
    uint8_t* bank = host_native64_ram_pointer(DM06_TEX_ADRS, 0x37000);
    assert(scene && bank);
    memcpy(scene, gauge, MODEL_SIZE);
    word(scene, DM06_OBJ_ADRS + 16);
    word(scene + 4, OBJECT_COUNT);
    word(scene + 8, DM06_OBJ_ADRS + HEADERS);
    for (unsigned i = 0; i < OBJECT_COUNT; ++i)
        word(scene + 16 + i * 4, DM06_OBJ_ADRS + OBJECTS + (i % TEXTURES) * OBJECT_BYTES);
    for (unsigned i = 0; i < TEXTURES; ++i) {
        uint8_t* header = scene + HEADERS + i * 16;
        word(header, 64u | 64u << 16);
        word(header + 8, i == 8 ? TEX_BUFF0 + 0x20000 : DM06_TEX_ADRS + i * 8192);
    }
    const Uint16 atlas[] = {0xc000, 0, 0x8000, 0, 0};
    memcpy(bank + 0x36000, atlas, sizeof atlas);
    assert(mvc2_model_store_written(scene, MODEL_SIZE));
    assert(mvc2_model_store_written(bank, 0x37000));
    assert(!mvc2_model_store_get(scene));

    for (unsigned i = 0; i < 3; ++i) vs_sys_work.team_w[0][i] = &pl_work[i * 2];
    Sys->scene_no = 2;
    Sys->routine_0 = 4;
    Sys->routine_1 = 0;
    Sys->rank_side = 0;
    Sys->play_side = 1;
    RankingMain();
    assert(obj_dm06 && Sys->routine_0 == 4 && Sys->routine_1 == 1);
    assert(rank_effects >= 10 && loaded_portraits == 3);
    for (unsigned i = 0; i < 3; ++i) {
        const Uint16* pixels = obj_dm06->thd[i + 17].address;
        assert(pixels[0] == 0x3110 + i && pixels[4095] == pixels[0]);
        assert(nl_tex[0x810 + i + 17].loaded);
    }

    broken_ranking_portrait = 1;
    Sys->routine_1 = 0;
    RankingMain();
    assert(obj_dm06 && Sys->routine_0 == 4 && Sys->routine_1 == 1);
    assert(((const Uint16*)obj_dm06->thd[19].address)[0] == 0);

    word(scene + 16, 0); /* A required rank mesh is absent. */
    assert(mvc2_model_store_written(scene, MODEL_SIZE));
    Sys->routine_1 = 0;
    RankingMain();
    assert(!obj_dm06 && Sys->routine_1 == 1);
    Sys->timer0 = 1;
    RankingMain();
    assert(score_fallbacks == 1 && Sys->routine_1 == 2);
    RankingMain();
    assert(!obj_dm06 && Sys->scene_no == 2 && !Sys->routine_0 && !Sys->routine_1);
    Sys->routine_0 = 4;
    Sys->play_side = 0;
    RankingMain();
    Sys->timer0 = 1;
    RankingMain();
    RankingMain();
    assert(!obj_dm06 && Sys->scene_no == 3 && !Sys->routine_0 && !Sys->routine_1);
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
    team_introduction(bytes);
    post_match_ranking(bytes);
    nl_tex_free_all();
    mvc2_model_store_reset();
    puts("Native match textures: team introduction, ranking, match fallback and gauge portraits passed.");
    return 0;
}
