#include <assert.h>
#include <math.h>
#include <string.h>
#include "player.h"
#include "rd.h"

static const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
float nl_cnv_proj_pers[16];
unsigned char nl_objput_param[44];
int rd_discard;
static unsigned draws;
static RD_STATE observed_state;
static RD_VTX observed[4];
static float horizontal_margin;
float rd_widescreen_offset(void) { return horizontal_margin; }

const float* nl_xmtrx(void) { return identity; }
const float* nl_view_matrix(void) { return identity; }
void rd_quad(const RD_STATE* state, const RD_VTX* vertices) {
    ++draws;
    observed_state = *state;
    memcpy(observed, vertices, sizeof observed);
}

static void close_to(float value, float expected) {
    assert(fabsf(value - expected) < 0.0001f);
}

int main(void) {
    /* Perspective with w=z, centre (320,240), and a one-unit near plane. */
    nl_cnv_proj_pers[8] = 1;
    nl_cnv_proj_pers[1] = 320;
    nl_cnv_proj_pers[9] = 320;
    nl_cnv_proj_pers[6] = -240;
    nl_cnv_proj_pers[10] = 240;
    float near_negative = -1;
    memcpy(nl_objput_param + 12, &near_negative, sizeof near_negative);
    VEC3 from = {-0.5f,0,2}, to = {0.5f,0,2};
    nlLineSetThickness(2);
    nlSingleLineUseViewMatrix(&from, &to, 0xff123456);
    assert(draws == 1);
    assert(observed_state.list == RD_OPAQUE && observed_state.zwrite);
    close_to(observed[0].x, 240);
    close_to(observed[0].y, 241);
    close_to(observed[2].x, 400);
    close_to(observed[2].y, 239);
    close_to(observed[0].z, 0.5f);
    assert(observed[0].base == 0xff123456);

    from = (VEC3){0,0,-1}; to = (VEC3){0,0,3};
    nlSingleLineUseViewMatrix(&from, &to, 0xffffffff);
    assert(draws == 2);
    close_to(observed[0].z, 1);
    close_to(observed[1].z, 1.0f / 3.0f);
    for (unsigned index = 0; index < 4; ++index)
        assert(isfinite(observed[index].x) && isfinite(observed[index].y));

    from = (VEC3){100,0,2}; to = (VEC3){101,1,2};
    nlSingleLineUseViewMatrix(&from, &to, 0xffffffff);
    assert(draws == 2);
    from = (VEC3){0,0,-2}; to = (VEC3){0,0,0};
    nlSingleLineUseViewMatrix(&from, &to, 0xffffffff);
    assert(draws == 2);
    nlLineSetThickness(0);
    from = (VEC3){0,0,2}; to = (VEC3){1,0,2};
    nlSingleLineUseViewMatrix(&from, &to, 0xffffffff);
    assert(draws == 2);
    /* Geometry in the new left margin must survive line clipping. These
     * projected positions remain in logical coordinates until rd_quad adds
     * the common viewport translation in the production renderer. */
    nlLineSetThickness(2);
    horizontal_margin = 107;
    from = (VEC3){-2.3125f,0,2}; to = (VEC3){-2.0f,0,2};
    nlSingleLineUseViewMatrix(&from, &to, 0xffffffff);
    assert(draws == 3);
    close_to(observed[0].x, -50);
    close_to(observed[1].x, 0);
    return 0;
}
