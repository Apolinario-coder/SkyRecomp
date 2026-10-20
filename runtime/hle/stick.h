#pragma once
#include <stdint.h>

static inline void input_stick_deadzone(int32_t *x, int32_t *y) {
    if ((int64_t)*x * *x + (int64_t)*y * *y <= 8000ll * 8000) *x = *y = 0;
}
