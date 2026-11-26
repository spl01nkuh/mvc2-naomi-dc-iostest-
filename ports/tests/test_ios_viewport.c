#include "display/viewport.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static void close_to(double actual, double expected)
{
    assert(fabs(actual - expected) < 0.00001);
}

int main(void)
{
    MVC2Viewport rect = mvc2_viewport(1920, 1080, 640, 480,
                                     MVC2_ASPECT_FOUR_THREE, 1, 1, 1, 0, 0);
    close_to(rect.x, 240); close_to(rect.y, 0);
    close_to(rect.width, 1440); close_to(rect.height, 1080);

    rect = mvc2_viewport(2622, 1206, 640, 480, MVC2_ASPECT_SIXTEEN_NINE, 1, 1, 1, 0, 0);
    close_to(rect.width, 1206.0 * 16.0 / 9.0); close_to(rect.height, 1206);
    close_to(rect.x, (2622.0 - rect.width) / 2); close_to(rect.y, 0);

    rect = mvc2_viewport(1920, 1080, 854, 480,
                         MVC2_ASPECT_SOURCE, 1, 1, 1, 0, 0);
    close_to(rect.width, 1920);
    close_to(rect.height, 1920.0 * 480.0 / 854.0);
    close_to(rect.x, 0);
    close_to(rect.y, (1080.0 - rect.height) / 2);
    close_to(rect.width / 854.0, rect.height / 480.0);

    rect = mvc2_viewport(1080, 1920, 640, 480, MVC2_ASPECT_FOUR_THREE,
                         0.5, 2, 0.5, -0.25, 0.5);
    close_to(rect.width, 1080); close_to(rect.height, 202.5);
    close_to(rect.x, -135); close_to(rect.y, 1338.75);

    rect = mvc2_viewport(1000, 1000, 320, 200, MVC2_ASPECT_SOURCE, 1, 1, 1, 0, 0);
    close_to(rect.width, 1000); close_to(rect.height, 625);
    close_to(rect.y, 187.5);

    rect = mvc2_viewport(1000, 600, 320, 200, MVC2_ASPECT_FILL, 1, 2, 2, 0, 0);
    close_to(rect.width, 2000); close_to(rect.height, 1200);
    close_to(rect.x, -500); close_to(rect.y, -300);

    rect = mvc2_viewport(1000, 600, NAN, 0, MVC2_ASPECT_SOURCE,
                         NAN, INFINITY, -INFINITY, NAN, INFINITY);
    close_to(rect.width, 800); close_to(rect.height, 600);
    close_to(rect.x, 100); close_to(rect.y, 0);

    rect = mvc2_viewport(0, 0, 640, 480, MVC2_ASPECT_FILL, 1, 1, 1, 0, 0);
    close_to(rect.width, 0); close_to(rect.height, 0);
    rect = mvc2_viewport(NAN, 50, 640, 480, MVC2_ASPECT_FILL, 1, 1, 1, 0, 0);
    close_to(rect.width, 0); close_to(rect.height, 0);
    puts("native viewport tests passed");
    return 0;
}
