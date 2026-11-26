#include "player.h"

#include <assert.h>
#include <stdarg.h>
#include <string.h>

extern void vs_msg_set(int no);
extern void vs_res_set(int no);
extern void bonus_screen_set(int two_p);
extern void ef12_disp(PLAYER_WORK* work);

static int glyphs[64], glyph_count, knockout_count;

void STrPrint(Uint8 x, Uint8 y, Uint8 color, const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    assert(x == 8 && y == 7 && color == 10 && !strcmp(format, "%s"));
    assert(!strcmp(va_arg(arguments, const char*), "K.O."));
    va_end(arguments);
    ++knockout_count;
}
Uint32 Rnd(void) { return 0; }
Sint32 flash_flag_get(void) { return 0; }
void flash_white_set(void) {}
void na_req(Uint8 no) { (void)no; }

void bonus_add(Uint32* hi, Uint32* lo) { *hi = 1; *lo = 0x23456789; }
void vital_bonus(Uint32* hi, Uint32* lo) { *hi = 0; *lo = 0x25000; }
void time_bonus(Uint32* hi, Uint32* lo) { vital_bonus(hi, lo); }
void no_ko_bonus(Uint32* hi, Uint32* lo) { vital_bonus(hi, lo); }

int nlPushMatrix(MATRIX* matrix) { (void)matrix; return 0; }
int nlPopMatrix(int count) { (void)count; return 0; }
MATRIX* nlGetMatrix(MATRIX* matrix) { return matrix; }
void nlLoadMatrix(MATRIX* matrix) { (void)matrix; }
void nlTranslateV(VEC3* vector) { (void)vector; }
void nlTranslate(Float x, Float y, Float z) { (void)x; (void)y; (void)z; }
void nlScaleV(VEC3* vector) { (void)vector; }
void nlUnitMatrix(void) {}
void scr_2d_matrix_set(void) {}
Float obj_scale_max(VEC3* vector) { return vector->x; }
void nlSetScaleFactor(Float scale) { (void)scale; }
void nlObjPutSetFadeColor(Float r, Float g, Float b) { (void)r; (void)g; (void)b; }
void obj_col_set(int* object, int color) { (void)object; (void)color; }
void nlObjPut(int* object) { assert(glyph_count < 64); glyphs[glyph_count++] = *object; }

int main(void) {
    int objects[64];
    int* slots[64];
    OBJ_FILE model = { .cnt = 64, .obj = slots };
    for (int i = 0; i < 64; ++i) {
        objects[i] = i;
        slots[i] = &objects[i];
    }
    Sys = &sys_work;
    vs_sys_w = &vs_sys_work;
    for (int match = 0; match < 2; ++match) {
        work_init();
        obj_dm00 = &model;
        vs_sys_work.winner_id = 0;
        vs_msg_set(2);
        work_list_move(W_SYS);
        assert(knockout_count == match + 1);

        vs_res_set(0);
        work_list_move(W_FRONT);
        glyph_count = 0;
        ef12_disp(work_top[W_FRONT]);
        assert(glyph_count == 8 && glyphs[0] == 0xd && glyphs[7] == 0xe);

        work_list_kill(W_FRONT);
        bonus_screen_set(0);
        for (int frame = 0; frame < 12; ++frame) work_list_move(W_FRONT);
        PLAYER_WORK* total = work_end[W_FRONT];
        assert(total->disp_flag && !total->em_w && !total->hit_data_adrs);
        glyph_count = 0;
        ef12_disp(total);
        assert(glyph_count == 14 && glyphs[0] == 0x1d);
        for (int digit = 0; digit < 9; ++digit) assert(glyphs[5 + digit] == digit + 1);
        for (int frame = 0; frame < 120; ++frame) work_list_move(W_FRONT);
        assert(!work_top[W_FRONT] && !work_end[W_FRONT]);
    }
}
