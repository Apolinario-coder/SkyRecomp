#pragma once
#include <math.h>
#include <stdint.h>

static inline void input_stick_deadzone(int32_t *x, int32_t *y) {
    if ((int64_t)*x * *x + (int64_t)*y * *y <= 8000ll * 8000) *x = *y = 0;
}

// A Switch stick reports inside a circle: full tilt is 32767 whatever the direction. Keys (both axes at 32767) and
// square-gated pads reached about 1.41 x full tilt on a diagonal, and Link ran that much faster (#31, #16).
static inline void input_stick_circle(int32_t *x, int32_t *y) {
    int64_t d2 = (int64_t)*x * *x + (int64_t)*y * *y;
    if (d2 <= 32767ll * 32767) return;
    double k = 32767.0 / sqrt((double)d2);
    *x = (int32_t)(*x * k), *y = (int32_t)(*y * k);
}
