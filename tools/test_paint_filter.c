#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../runtime/nvn/paint_filter.h"

int main(void) {
    const uint64_t paint = 0x6fb6d06f10bd0297ull;
    const float source[] = {1920, 1080, 0.65f, 42, 9, 8, 7, 6};
    float result[8] = {0};
    assert(sky_paint_filter_parameters(result, source, sizeof source, paint, WATERCOLOR_OFF, 1920, 1080, 2316, 2572));
    // Every paint tap now lands at the original center, preserving fine dark features rather than dilating bright ones.
    const float uv = 0.5f;
    for (int i = -3; i <= 3; i++) {
        assert(uv + (0.5f * i) / result[0] == uv);
        assert(uv + (0.5f * i) / result[1] == uv);
    }
    assert(isfinite(result[0]) && isfinite(result[1]));
    assert(memcmp(result + 2, source + 2, sizeof source - 2 * sizeof(float)) == 0);
    assert(source[0] == 1920 && source[1] == 1080); // guest data stays untouched

    assert(sky_paint_filter_parameters(result, source, sizeof source, paint,
                                      WATERCOLOR_RESOLUTION, 1920, 1080, 2316, 2572));
    assert(result[0] == 2316 && result[1] == 2572);
    assert(memcmp(result + 2, source + 2, sizeof source - 2 * sizeof(float)) == 0);
    float sentinel[8];
    memset(sentinel, 0x5a, sizeof sentinel);
    memcpy(result, sentinel, sizeof result);
    assert(!sky_paint_filter_parameters(result, source, sizeof source, paint ^ 1, WATERCOLOR_OFF, 1920, 1080, 2316, 2572));
    assert(!sky_paint_filter_parameters(result, source, sizeof source, paint, WATERCOLOR_OFF, 1280, 720, 2316, 2572));
    assert(!sky_paint_filter_parameters(result, source, 8, paint, WATERCOLOR_OFF, 1920, 1080, 2316, 2572));
    assert(!sky_paint_filter_parameters(result, source, SKY_PAINT_PARAMS_MAX + 1, paint, WATERCOLOR_OFF, 1920, 1080, 2316, 2572));
    assert(!sky_paint_filter_parameters(result, source, sizeof source, paint, WATERCOLOR_OFF, 0, 1080, 2316, 2572));
    float invalid[4] = {NAN, 1080, 1, 0};
    assert(!sky_paint_filter_parameters(result, invalid, sizeof invalid, paint, WATERCOLOR_OFF, 1920, 1080, 2316, 2572));
    assert(!sky_paint_filter_parameters(result, source, sizeof source, paint,
                                       WATERCOLOR_ORIGINAL, 1920, 1080, 2316, 2572));
    assert(!sky_paint_filter_parameters(result, source, sizeof source, paint,
                                       WATERCOLOR_RESOLUTION, 1920, 1080, 0, 2572));
    assert(memcmp(result, sentinel, sizeof result) == 0);
    puts("Paint filter: centered taps, preserved alpha/data, and layout guards passed");
    return 0;
}
