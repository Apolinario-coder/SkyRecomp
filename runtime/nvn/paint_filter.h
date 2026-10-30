#pragma once
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "../settings.h"

#define SKY_PAINT_PARAMS_MAX 0x800

// SSHD's active Paint_Constant_NewKernel variant takes the brightest of four neighboring samples.
// Center those samples when the filter is disabled. Keep its draw, alpha, UVs and subsequent composites intact.
// Only patch the known parameter layout; never change the guest's uniform buffer.
static inline bool sky_paint_filter_parameters(void *out, const void *source, size_t size, uint64_t shader_hash,
                                              int mode, uint32_t texture_width, uint32_t texture_height,
                                              uint32_t rendered_width, uint32_t rendered_height) {
    if ((mode != WATERCOLOR_RESOLUTION && mode != WATERCOLOR_OFF) ||
        !out || !source || size < 16 || size > SKY_PAINT_PARAMS_MAX ||
        shader_hash != 0x6fb6d06f10bd0297ull || !texture_width || !texture_height) return false;
    float dimensions[2];
    memcpy(dimensions, source, sizeof dimensions);
    if (!isfinite(dimensions[0]) || !isfinite(dimensions[1]) ||
        fabsf(dimensions[0] - texture_width) >= 1 || fabsf(dimensions[1] - texture_height) >= 1) return false;
    if (mode == WATERCOLOR_RESOLUTION && (!rendered_width || !rendered_height)) return false;
    memcpy(out, source, size);
    // Finite reciprocal dimensions make all offsets round to zero at the original UV, without dividing by zero.
    const float dimensions_out[2] = {mode == WATERCOLOR_OFF ? 1e30f : (float)rendered_width,
                                     mode == WATERCOLOR_OFF ? 1e30f : (float)rendered_height};
    memcpy(out, dimensions_out, sizeof dimensions_out);
    return true;
}
