#include <assert.h>
#include <stdio.h>
#include "../runtime/hle/input.c"

int main(void) {
    g_is_skyward = true;
    g_settings.free_cam = true;
    g_settings.free_cam_sens = .5f;
    free_camera_reset();
    int32_t x = 24000, y = 0;
    uint64_t b = free_camera_input(1ull << 22, &x, &y, 100, false);
    assert(x == 12000 && (b & (1ull << BTN_L)) && !(b & (1ull << 22)));
    assert(input_free_cam_active());
    b = free_camera_input(1ull << BTN_L, &x, &y, 200, false);
    assert(!(b & (1ull << BTN_L)) && !input_free_cam_active());
    b = free_camera_input(0, &x, &y, 600, false);
    assert((b & (1ull << BTN_L)) && input_free_cam_active());
    free_camera_input(1ull << BTN_L, &x, &y, 700, false);
    b = free_camera_input(0, &x, &y, 800, false);
    assert(!(b & (1ull << BTN_L)) && !input_free_cam_active());
    free_camera_reset();
    for (int button = BTN_R; button <= BTN_ZR; button++) {
        if (button != BTN_R && button != BTN_ZL && button != BTN_ZR) continue;
        x = 24000;
        b = free_camera_input(1ull << button, &x, &y, 900, false);
        assert(x == 24000 && !(b & (1ull << BTN_L)) && !input_free_cam_active());
    }
    free_camera_input(1ull << BTN_L, &x, &y, 1000, true);
    b = free_camera_input(0, &x, &y, 1100, false);
    assert((b & (1ull << BTN_L)) && input_free_cam_active());
    g_settings.free_cam_sens = 2;
    x = 30000; y = -30000;
    b = free_camera_input(0, &x, &y, 1200, false);
    assert(x == 32767 && y == -32767 && (b & (1ull << 22)) && (b & (1ull << 23)));
    g_settings.free_cam = false;
    x = 24000;
    b = free_camera_input(0, &x, &y, 1300, false);
    assert(x == 24000 && !(b & (1ull << BTN_L)) && !input_free_cam_active());
    g_is_skyward = false;
    g_settings.free_cam = true;
    assert(free_camera_input(123, &x, &y, 1400, false) == 123 && x == 24000);
    puts("free camera: tap, hold, sensitivity, stick directions, menu and action controls passed");
}
