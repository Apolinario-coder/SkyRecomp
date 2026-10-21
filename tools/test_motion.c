// clang -O1 tools/test_motion.c -lm -o build/test_motion && build/test_motion
#include <assert.h>
#include "../runtime/hle/motion.h"

static bool near(float a, float b) { return fabsf(a - b) < 1e-3f; }

int main(void) {
    // a quarter turn about z (90 degrees / s for 1 s, 100 steps)
    float q[4] = {0, 0, 0, 1}, w[3] = {0, 0, 0.25f}, d[9];
    for (int i = 0; i < 100; i++) motion_integrate(q, w, 0.01f);
    assert(near(q[0], 0) && near(q[1], 0) && near(q[2], sinf((float)M_PI / 4)) && near(q[3], cosf((float)M_PI / 4)));
    motion_dir_from_q(q, d);  // the controller's x axis now points along the world's y
    assert(near(d[0], 0) && near(d[1], 1) && near(d[2], 0));

    // direction matrix and back (both signs of q are the same rotation); the last ones have a negative trace
    const float turns[][3] = {{0.1f, 0, 0}, {0, -0.3f, 0.2f}, {0.4f, 0.4f, -0.4f}, {0, 0, 0.5f}, {0.5f, 0, 0}};
    for (unsigned i = 0; i < sizeof turns / sizeof turns[0]; i++) {
        float p[4] = {0, 0, 0, 1}, r[4];
        motion_integrate(p, turns[i], 1.0f);
        motion_dir_from_q(p, d);
        motion_q_from_dir(d, r);
        float sign = p[0] * r[0] + p[1] * r[1] + p[2] * r[2] + p[3] * r[3] < 0 ? -1.0f : 1.0f;
        for (int k = 0; k < 4; k++) assert(near(p[k], sign * r[k]));
    }
    float id[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}, r[4];  // at rest (the no-gyro state): the identity quaternion
    motion_q_from_dir(id, r);
    assert(r[0] == 0 && r[1] == 0 && r[2] == 0 && r[3] == 1);

    // the default map: SDL's up is the Switch's z, toward the player is -y
    MotionMap m;
    assert(motion_parse_map("x,-z,y", &m));
    float up[3] = {0, 9.80665f, 0}, back[3] = {0, 0, 1}, sw[3];
    motion_map(&m, up, sw);
    assert(sw[0] == 0 && sw[1] == 0 && near(sw[2], 9.80665f));
    motion_map(&m, back, sw);
    assert(sw[0] == 0 && sw[1] == -1 && sw[2] == 0);
    assert(motion_parse_map("+y,x,-z", &m) && !motion_parse_map("x,y", &m) && !motion_parse_map("a,b,c", &m) &&
           !motion_parse_map("x,y,z,", &m));
}
