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
    for (int actual = 60; actual <= 120; actual += 20) {
        g_settings.frame_rate = 60;
        assert(rt_sky_frame_step(0.5f) == 0.5f);
        assert(rt_sky_frame_ms(1000.0f / 60) == 1000.0f / 60);
        g_settings.frame_rate = 120;
        assert(rt_sky_frame_step(0.5f) == 0.25f);
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
}
