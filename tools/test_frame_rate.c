#include <assert.h>
#include <math.h>
#include "../runtime/frame_rate.h"
#include "../runtime/settings.h"

Settings g_settings;
bool g_is_las;
bool g_is_skyward = true;
static uint64_t now_ns = 1;
uint64_t plat_now_ns(void) { return now_ns; }

int main(void) {
    for (int fps = 60; fps <= 120; fps += 60) {
        g_settings.frame_rate = fps;
        assert(rt_frame_rate() == fps);
        assert(fabs((double)rt_frame_period_ns() * fps - 1e9) <= 40);
        float ticks = 0, ms = 0, remainder = 0;
        int integer_ticks = 0;
        for (int f = 0; f < fps; f++) {
            float step = 0.5f * rt_frame_scale();
            ticks += step;
            ms += (1000.0f / 60) * rt_frame_scale();
            remainder += step;
            integer_ticks += (int)remainder;
            remainder -= (int)remainder;
        }
        assert(ticks == 30 && integer_ticks == 30);
        assert(fabsf(ms - 1000) < 0.01f);
    }
    g_settings.frame_rate = 0;
    assert(rt_frame_rate() == 60);
    g_settings.frame_rate = 144;
    assert(rt_frame_rate() == 60);
    g_settings.frame_rate = 120;
    rt_sky_frame_step(0.5f);  // (the first-ever call: the flat fallback, no prior beat - discarded)
    for (int actual = 60; actual <= 120; actual += 20) {
        // real time carries across the 60/120 toggle (no clock reset): each loop integrates one second
        float ticks = 0, ms = 0;
        for (int f = 0; f < actual; f++) {
            now_ns += 1000000000ull / actual;
            ticks += rt_sky_frame_step(0.5f);
            ms += rt_sky_frame_ms(1000.0f / 60);
        }
        assert(fabsf(ticks - 30) < 0.001f);
        assert(fabsf(ms - 1000) < 0.01f);
    }
    now_ns += 500000000;
    assert(rt_sky_frame_step(0.5f) == 0.25f);
    now_ns += 50000000;
    assert(rt_sky_frame_step(0.5f) == 1);
    g_is_skyward = false;
    assert(rt_frame_rate() == 60 && rt_frame_scale() == 1);

    // 60 FPS target: the elapsed-time correction applies here too (it used to be 120 only, and the game's own
    // 30 Hz mode ran the whole world at 2x: 1.0 step per frame at 60 presents).
    g_is_skyward = true;
    g_settings.frame_rate = 60;
    rt_sky_frame_step(0.5f);  // (a first beat restarts the clock: discarded)
    float ticks = 0, ms = 0;
    for (int f = 0; f < 60; f++) {  // steady 60 presents, the game's own 60 Hz step: unchanged, real-time speed
        now_ns += 1000000000ull / 60;
        ticks += rt_sky_frame_step(0.5f);
        ms += rt_sky_frame_ms(1000.0f / 60);
    }
    assert(fabsf(ticks - 30) < 0.001f);
    assert(fabsf(ms - 1000) < 0.01f);
    ticks = 0, ms = 0;
    for (int f = 0; f < 60; f++) {  // THE BUG: the game's own 30 Hz mode (1.0 per frame) at 60 presents
        now_ns += 1000000000ull / 60;
        ticks += rt_sky_frame_step(1.0f);
        ms += rt_sky_frame_ms(1000.0f / 30);
    }
    assert(fabsf(ticks - 30) < 0.001f);  // (still 30 ticks/s: the world at real-time speed, not 2x)
    assert(fabsf(ms - 1000) < 0.01f);
    ticks = 0;
    for (int f = 0; f < 40; f++) {  // GPU-bound: 40 real fps at the 60 target - no slow motion
        now_ns += 1000000000ull / 40;
        ticks += rt_sky_frame_step(0.5f);
    }
    assert(fabsf(ticks - 30) < 0.001f);
    now_ns += 500000000;  // a long stall restarts the clock: the game's own step, no giant jump
    assert(rt_sky_frame_step(0.5f) == 0.5f);
}
