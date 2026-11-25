/* Run the real attract scene using generated mesh records. The graphics,
 * sound and ROM boundaries are replaced so the scene needs no game data. */
#include <assert.h>
#include <stdio.h>
#include "player.h"

extern void attract_opening_main(void);

static OBJ_FILE *opening_model;
static int model_loads, draws, opening_cues, fallbacks;

void TexLoad(int scene) {
    assert(scene == 12);
    obj_dm08 = opening_model;
    ++model_loads;
}
void host_log(const char *format, ...) { (void)format; ++fallbacks; }
void Palette_Init(void) {}
void Texture_Init(void) {}
void bright_init(void) {}
void fog_init(void) {}
void fog_set(void) {}
void back_color_set2(Uint32 a, Uint32 b) { (void)a; (void)b; }
void ex_sound_load2(void) {}
void total_vol_init(void) {}
void ex_se_req(Uint8 cue) { assert(cue == 0); ++opening_cues; }
void stream_req(Sint8 cue) { assert(cue == 11); }
void stream_start(void) {}
void fade_set(int a, int b, int c) { (void)a; (void)b; (void)c; }
void name_pic_set(int side, Uint32 name) { assert(side >= 0 && side < 5 && name < 59); }
void nlMatrixMode(int mode) { (void)mode; }
void nlUnitMatrix(void) {}
void nlPerspective(Sint32 fovy, Float aspect, Float near, Float far) {
    (void)fovy; (void)aspect; (void)near; (void)far;
}
void nlLookAt(VEC3 *eye, VEC3 *target, Sint32 angle) { (void)eye; (void)target; (void)angle; }
MATRIX *nlGetMatrix(MATRIX *matrix) { return matrix; }
int nlPushMatrix(MATRIX *matrix) { (void)matrix; return 0; }
int nlPopMatrix(int count) { (void)count; return 0; }
void nlMakeModelViewProjectMatrix(Float width, Float height) { (void)width; (void)height; }
void nlLoadMatrix(MATRIX *matrix) { (void)matrix; }
void nlProjectScreen(VEC3 *pos, Float *screen) { (void)pos; *screen = 320.0f; }
void nlObjPutSetFadeColor(Float r, Float g, Float b) { (void)r; (void)g; (void)b; }
void nlTranslateV(VEC3 *position) { (void)position; }
void nlRotateAZ(Sint32 angle) { (void)angle; }
void nlRotateAY(Sint32 angle) { (void)angle; }
void nlScaleV(VEC3 *scale) { (void)scale; }
void nlSetScaleFactor(Float factor) { (void)factor; }
Float obj_scale_max(VEC3 *scale) { return scale->x; }
void nlObjPutUseViewMatrix(int *obj) { assert(obj); ++draws; }

static void start_opening(OBJ_FILE *model, int variant, int monitor) {
    opening_model = model;
    *Sys = (SYSTEM_WORK){0};
    Sys->task_step = 2;
    Sys->cpu_level_type = variant;
    Sys->monitor_31k = monitor;
    model_loads = draws = opening_cues = fallbacks = 0;
    attract_opening_main();
    assert(model_loads == 1);
}

static void expect_title_fallback(OBJ_FILE *model, int variant) {
    start_opening(model, variant, 0);
    assert(Sys->task_step == 3 && Sys->scene_no == 0 && Sys->timer1 == 0);
    assert(!draws && !opening_cues && fallbacks == 1);
}

int main(void) {
    int meshes[60][7] = {{0}};
    int *objects[60];
    for (int i = 0; i < 60; ++i) objects[i] = meshes[i];
    OBJ_FILE model = { .obj = objects, .cnt = 59 };
    Sys = &sys_work;

    expect_title_fallback(NULL, 0);
    expect_title_fallback(&model, 0);
    model.cnt = 60;
    objects[46] = NULL;
    expect_title_fallback(&model, 0);
    objects[46] = meshes[46];
    objects[53] = NULL;
    expect_title_fallback(&model, 0);
    objects[53] = meshes[53];
    objects[44] = NULL;
    expect_title_fallback(&model, 1);

    /* Unused localized meshes may be omitted in the active variant. */
    for (int variant = 0; variant < 2; ++variant) {
        objects[44] = variant ? meshes[44] : NULL;
        objects[20] = variant ? NULL : meshes[20];
        start_opening(&model, variant, variant);
        assert(Sys->task_step == 2 && Sys->scene_no == 1 && Sys->timer1 == 1);
        assert(draws == 2 && opening_cues == 1 && !fallbacks);
        int length = variant ? 1718 : 1700;
        for (int frame = 2; frame <= length + 1; ++frame) {
            attract_opening_main();
            assert(Sys->timer1 == frame);
        }
        assert(Sys->task_step == 3 && Sys->scene_no == 0);
        assert(model_loads == 1 && draws > length && !fallbacks);
    }
    puts("Native opening reaches the title with complete or missing scene models.");
    return 0;
}
