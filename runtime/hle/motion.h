// The controller's motion for the game's six-axis sensor (runtime/hle/input.c): SDL's gamepad axes to the Switch's,
// the orientation the gyro integrates to, and the sensor state's direction matrix (tools/test_motion.c).
#pragma once
#include <math.h>
#include <stdbool.h>

// Switch axis i = sign[i] * SDL axis idx[i]. SDL: x right, y up, z toward the player; Switch: x right, y away from
// the player, z up - "x,-z,y", as Switch emulators map it. LARECOMP_GYRO_MAP overrides it (a sign or an axis that
// turns out wrong in game, fixed without a rebuild).
typedef struct { int idx[3]; float sign[3]; } MotionMap;

static inline bool motion_parse_map(const char *s, MotionMap *m) {  // "x,-z,y"
    for (int i = 0; i < 3; i++) {
        m->sign[i] = *s == '-' ? -1.0f : 1.0f;
        if (*s == '-' || *s == '+') s++;
        if (*s < 'x' || *s > 'z') return false;
        m->idx[i] = *s++ - 'x';
        if (i < 2 && *s++ != ',') return false;
    }
    return *s == 0;
}

static inline void motion_map(const MotionMap *m, const float in[3], float out[3]) {
    for (int i = 0; i < 3; i++) out[i] = m->sign[i] * in[m->idx[i]];
}

// q (x, y, z, w: the controller in the world) turned by w (rotations / s, about the controller's own axes) for dt s
static inline void motion_integrate(float q[4], const float w[3], float dt) {
    float h[3] = {w[0] * (float)M_PI * dt, w[1] * (float)M_PI * dt, w[2] * (float)M_PI * dt};  // (half the angle)
    float n = sqrtf(h[0] * h[0] + h[1] * h[1] + h[2] * h[2]);
    if (n == 0) return;
    float k = sinf(n) / n, c = cosf(n), ax = h[0] * k, ay = h[1] * k, az = h[2] * k;
    float x = q[0], y = q[1], z = q[2], s = q[3];  // q * (ax, ay, az, c)
    q[0] = s * ax + x * c + y * az - z * ay;
    q[1] = s * ay + y * c + z * ax - x * az;
    q[2] = s * az + z * c + x * ay - y * ax;
    q[3] = s * c - x * ax - y * ay - z * az;
    float len = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);  // (rounding, over thousands of steps)
    for (int i = 0; i < 4; i++) q[i] /= len;
}

// The state's direction: the controller's x, y and z axes in the world (d[0..2], d[3..5], d[6..8]), identity at rest
static inline void motion_dir_from_q(const float q[4], float d[9]) {
    float x = q[0], y = q[1], z = q[2], s = q[3];
    d[0] = 1 - 2 * (y * y + z * z), d[1] = 2 * (x * y + z * s), d[2] = 2 * (x * z - y * s);
    d[3] = 2 * (x * y - z * s), d[4] = 1 - 2 * (x * x + z * z), d[5] = 2 * (y * z + x * s);
    d[6] = 2 * (x * z + y * s), d[7] = 2 * (y * z - x * s), d[8] = 1 - 2 * (x * x + y * y);
}

static inline void motion_q_from_dir(const float d[9], float q[4]) {
#define R(i, j) d[3 * (j) + (i)]
    float t = R(0, 0) + R(1, 1) + R(2, 2), s;
    if (t > 0) {
        s = sqrtf(t + 1) * 2;
        q[0] = (R(2, 1) - R(1, 2)) / s, q[1] = (R(0, 2) - R(2, 0)) / s, q[2] = (R(1, 0) - R(0, 1)) / s, q[3] = s / 4;
    } else if (R(0, 0) > R(1, 1) && R(0, 0) > R(2, 2)) {
        s = sqrtf(1 + R(0, 0) - R(1, 1) - R(2, 2)) * 2;
        q[0] = s / 4, q[1] = (R(0, 1) + R(1, 0)) / s, q[2] = (R(0, 2) + R(2, 0)) / s, q[3] = (R(2, 1) - R(1, 2)) / s;
    } else if (R(1, 1) > R(2, 2)) {
        s = sqrtf(1 + R(1, 1) - R(0, 0) - R(2, 2)) * 2;
        q[0] = (R(0, 1) + R(1, 0)) / s, q[1] = s / 4, q[2] = (R(1, 2) + R(2, 1)) / s, q[3] = (R(0, 2) - R(2, 0)) / s;
    } else {
        s = sqrtf(1 + R(2, 2) - R(0, 0) - R(1, 1)) * 2;
        q[0] = (R(0, 2) + R(2, 0)) / s, q[1] = (R(1, 2) + R(2, 1)) / s, q[2] = s / 4, q[3] = (R(1, 0) - R(0, 1)) / s;
    }
#undef R
}
