#ifndef MVC2_NATIVE_VIEWPORT_H
#define MVC2_NATIVE_VIEWPORT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MVC2Viewport {
    double x, y, width, height;
} MVC2Viewport;

typedef enum MVC2Aspect {
    MVC2_ASPECT_FOUR_THREE = 0,
    MVC2_ASPECT_SOURCE = 1,
    MVC2_ASPECT_FILL = 2,
    MVC2_ASPECT_SIXTEEN_NINE = 3
} MVC2Aspect;

/* Returns an unclipped rectangle: deliberate zooming may crop the picture.
 * Offsets are fractions of half the available canvas, independent of zoom.
 * Invalid sizes produce an empty rectangle. Nonfinite options use defaults.
 */
MVC2Viewport mvc2_viewport(double available_width, double available_height,
                         double source_width, double source_height,
                         MVC2Aspect aspect, double scale,
                         double width_scale, double height_scale,
                         double offset_x, double offset_y);

#ifdef __cplusplus
}
#endif
#endif
