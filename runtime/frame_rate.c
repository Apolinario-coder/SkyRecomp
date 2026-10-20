#include "frame_rate.h"
#include "rt.h"
#include "settings.h"
#include "hle.h"

int rt_frame_rate(void) { return g_is_skyward && g_settings.frame_rate == 120 ? 120 : 60; }
uint64_t rt_frame_period_ns(void) { return (1000000000ull + rt_frame_rate() / 2) / rt_frame_rate(); }
float rt_frame_scale(void) { return 60.0f / rt_frame_rate(); }

static uint64_t last_step_ns;
static float sky_step;

float rt_sky_frame_step(float original) {
    if (rt_frame_rate() != 120) { last_step_ns = 0; return original; }
    uint64_t now = plat_now_ns(), elapsed = now - last_step_ns;
    sky_step = original * 0.5f;
    // Keep real-time speed below the target too; loading stalls must not become giant physics steps.
    if (last_step_ns && elapsed < 100000000ull)
        sky_step = elapsed >= 33333333ull ? 1.0f : (float)((double)elapsed * 30.0 / 1e9);
    last_step_ns = now;
    static unsigned calls;
    if (ENV("LARECOMP_LOG_FRAMEPACE") && ++calls % 120 == 0)
        LOG("[frame-rate] target 120 step %.6f ms %.3f\n", sky_step, sky_step * (1000.0f / 30));
    return sky_step;
}

float rt_sky_frame_ms(float original) {
    return rt_frame_rate() == 120 ? sky_step * (1000.0f / 30) : original;
}
