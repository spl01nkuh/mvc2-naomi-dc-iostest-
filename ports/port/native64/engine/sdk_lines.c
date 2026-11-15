/* libline's game-used entry points through the native triangle renderer.
 * The line is transformed in homogeneous space and clipped before division;
 * no SH-4 store queues or serialized addresses are used. */
#include <math.h>
#include <string.h>
#include "player.h"
#include "nl_port.h"

extern float nl_cnv_proj_pers[16];
extern unsigned char nl_objput_param[];

static float line_thickness = 1.0f;

typedef struct LinePoint { float x, y, w; } LinePoint;

static void multiply(float* output, const float* a, const float* b) {
    float temporary[16];
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            temporary[row * 4 + column] =
                a[row * 4] * b[column] + a[row * 4 + 1] * b[4 + column] +
                a[row * 4 + 2] * b[8 + column] + a[row * 4 + 3] * b[12 + column];
    memcpy(output, temporary, sizeof temporary);
}

static LinePoint transform(const float matrix[16], const VEC3* point) {
    return (LinePoint){
        point->x * matrix[1] + point->y * matrix[5] + point->z * matrix[9] + matrix[13],
        point->x * matrix[2] + point->y * matrix[6] + point->z * matrix[10] + matrix[14],
        point->x * matrix[0] + point->y * matrix[4] + point->z * matrix[8] + matrix[12]};
}

/* Clip one homogeneous half space, whose signed distances are da and db. */
static int clip(LinePoint* a, LinePoint* b, float da, float db) {
    if (da < 0.0f && db < 0.0f) return 0;
    if ((da < 0.0f) != (db < 0.0f)) {
        float t = da / (da - db);
        LinePoint cut = { a->x + t * (b->x - a->x),
                          a->y + t * (b->y - a->y),
                          a->w + t * (b->w - a->w) };
        if (da < 0.0f) *a = cut;
        else *b = cut;
    }
    return 1;
}

void nlLineSetThickness(float thickness) {
    if (isfinite(thickness) && thickness >= 0.0f) line_thickness = thickness;
}

void nlSingleLineUseViewMatrix(VEC3* from, VEC3* to, unsigned color) {
    if (!from || !to || rd_discard || line_thickness <= 0.0f) return;
    float projection[16], matrix[16], near_negative;
    multiply(projection, nl_view_matrix(), nl_cnv_proj_pers);
    multiply(matrix, nl_xmtrx(), projection);
    LinePoint a = transform(matrix, from), b = transform(matrix, to);
    if (!isfinite(a.x) || !isfinite(a.y) || !isfinite(a.w) ||
        !isfinite(b.x) || !isfinite(b.y) || !isfinite(b.w)) return;
    memcpy(&near_negative, nl_objput_param + 12, sizeof near_negative);
    float near_w = near_negative < 0.0f ? -near_negative : 0.001f;
    if (!clip(&a, &b, a.w - near_w, b.w - near_w) ||
        !clip(&a, &b, a.x + a.w, b.x + b.w) ||
        !clip(&a, &b, (RD_WIDTH + 1) * a.w - a.x, (RD_WIDTH + 1) * b.w - b.x) ||
        !clip(&a, &b, a.y + a.w, b.y + b.w) ||
        !clip(&a, &b, (RD_HEIGHT + 1) * a.w - a.y, (RD_HEIGHT + 1) * b.w - b.y)) return;
    float ax = a.x / a.w, ay = a.y / a.w, bx = b.x / b.w, by = b.y / b.w;
    float dx = bx - ax, dy = by - ay, length = sqrtf(dx * dx + dy * dy);
    float half = line_thickness * 0.5f;
    float ox = length > 0.0f ? -dy * half / length : half;
    float oy = length > 0.0f ? dx * half / length : 0.0f;
    if (length == 0.0f) { by += half; ay -= half; }
    RD_VTX vertices[4] = {
        { ax + ox, ay + oy, 1.0f / a.w, 0, 0, color, 0 },
        { bx + ox, by + oy, 1.0f / b.w, 0, 0, color, 0 },
        { bx - ox, by - oy, 1.0f / b.w, 0, 0, color, 0 },
        { ax - ox, ay - oy, 1.0f / a.w, 0, 0, color, 0 }};
    RD_STATE state = {0};
    state.list = RD_OPAQUE;
    state.depth = RD_GREATER;
    state.zwrite = 1;
    state.src = RD_ONE;
    state.dst = RD_ZERO;
    state.fog = RD_FOG_NONE;
    rd_quad(&state, vertices);
}
