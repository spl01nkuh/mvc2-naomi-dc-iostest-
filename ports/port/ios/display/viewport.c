#include "viewport.h"

#include <math.h>

static double option(double value, double fallback, double minimum, double maximum)
{
    if (!isfinite(value)) return fallback;
    return fmin(fmax(value, minimum), maximum);
}

MVC2Viewport mvc2_viewport(double available_width, double available_height,
                         double source_width, double source_height,
                         MVC2Aspect aspect, double scale,
                         double width_scale, double height_scale,
                         double offset_x, double offset_y)
{
    MVC2Viewport result = {0, 0, 0, 0};
    if (!isfinite(available_width) || !isfinite(available_height) ||
        available_width <= 0 || available_height <= 0)
        return result;

    double ratio = 4.0 / 3.0;
    if (aspect == MVC2_ASPECT_FILL) {
        ratio = available_width / available_height;
    } else if (aspect == MVC2_ASPECT_SOURCE &&
               isfinite(source_width) && isfinite(source_height) &&
               source_width > 0 && source_height > 0) {
        double source_ratio = source_width / source_height;
        if (isfinite(source_ratio) && source_ratio > 0) ratio = source_ratio;
    }
    double width = available_width;
    double height = width / ratio;
    if (height > available_height) {
        height = available_height;
        width = height * ratio;
    }
    double common = option(scale, 1, 0.25, 1);
    result.width = width * common * option(width_scale, 1, 0.25, 2);
    result.height = height * common * option(height_scale, 1, 0.25, 2);
    result.x = (available_width - result.width) * 0.5 +
               option(offset_x, 0, -1, 1) * available_width * 0.5;
    result.y = (available_height - result.height) * 0.5 +
               option(offset_y, 0, -1, 1) * available_height * 0.5;
    if (!isfinite(result.x) || !isfinite(result.y) ||
        !isfinite(result.width) || !isfinite(result.height)) {
        MVC2Viewport empty = {0, 0, 0, 0};
        return empty;
    }
    return result;
}
