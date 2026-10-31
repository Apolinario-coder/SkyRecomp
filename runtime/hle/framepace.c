// Frame pacing: the game's speed doesn't depend on the frame rate.
// The game advances its world each frame by its frame period (16.67 ms) times the swap interval its frame timer
// (f_700000c910) reads from the system object - normally 1. When a frame takes longer than a vsync (GPU-bound at
// 32:9, a hitch), the next frame starts a vsync later but still advanced only 16.67 ms: slow motion (30 fps = half
// speed). tools/patches.py FRAME_STEPS_SITE routes that read here: we return how many vsyncs really passed since
// the last frame, so the world advances by the time that passed. The game's own waiting is unaffected (it reads
// the interval nowhere else). Shared by the macOS and Windows builds.
// FINDING (2026-10-09): forcing 2 steps did NOT change Link's walking speed - gameplay is a true fixed step per
// frame; this timer feeds something else. Kept for research, off by default.
// FINDING (2026-10-25): when the game takes its own 30 fps mode (swap interval 2, seen here whenever its frame
// timer runs), it does NOT wait two vsyncs per frame - it presents every frame and expects the compositor to hold
// each one for two vsyncs. So the present pacer honors the interval (runtime/nvn/vulkan/exec.c); ignoring it made
// the game advance its world (every animation) twice per presented frame - 2x speed - whenever the game had gone
// to its 30 fps mode under GPU load, while Link's walking stayed normal (see runtime/frame_rate.c).
//   LARECOMP_FRAMEPACE=1     hand the timer the vsyncs elapsed (default: the game's own value)
//   LARECOMP_FRAME_STEPS_MAX maximum steps per frame (default 4: after a long stall the game doesn't jump ahead)
//   LARECOMP_FORCE_STEPS=<n> (test) always n
#include <stdlib.h>

#include "../hle.h"
#include "../frame_rate.h"
#include "../platform.h"

extern volatile uint64_t g_vsync_count;  // runtime/hle/vi.c

// The present pacer, after each presented frame (metal.m backend_present, vulkan/device.c via exec.c vk_pace_frame).
// The game's own swap interval (1 = 60 Hz, 2 = its 30 fps mode): on a console the compositor holds each present that
// many vsyncs - here the pacer does, so the game's 30 fps mode presents at 30 and plays at full speed instead of
// advancing the world twice per presented frame. It also keeps a 120 Hz display at the chosen frame rate.
// LARECOMP_UNLOCK_FPS=1: off.
extern __thread uint64_t g_blocked_ns;  // kernel.c: this thread's time blocked in waits / sleeps
void rt_present_pace(void) {
    static int cap_fps = -1;
    if (cap_fps < 0) { const char *e = getenv("LARECOMP_UNLOCK_FPS"); cap_fps = !e || atoi(e) == 0; }
    if (!cap_fps) return;
    const uint32_t interval = rt_game_interval();
    const uint64_t period_ns = rt_frame_period_ns() * interval;
    static uint64_t next_present_ns;
    static uint32_t interval_seen = 1;
    if (interval_seen != interval) {  // (the game changed its frame mode: re-anchor the present grid)
        interval_seen = interval;
        next_present_ns = 0;
    }
    uint64_t t0 = plat_now_ns();
    if (!next_present_ns) next_present_ns = t0;
    next_present_ns += period_ns;
    if (t0 < next_present_ns) {
        plat_sleep_until_ns(next_present_ns);
        uint64_t t1 = plat_now_ns();
        if (t1 > t0) g_blocked_ns += t1 - t0;
    } else if (t0 > next_present_ns + period_ns * 2) {
        next_present_ns = t0;
    }
}
uint64_t g_frame_steps_late;             // (stats) frames that advanced more than one step

uint32_t rt_frame_steps(uint32_t interval) {
    rt_game_interval_seen(interval);  // the present pacer honors the game's own 30 fps mode (runtime/frame_rate.c)
    static int on = -1, max_steps, forced;
    if (on < 0) {
        const char *e = ENV("LARECOMP_FRAMEPACE");
        on = e ? atoi(e) != 0 : 0;  // off by default: the world (Link's movement) doesn't use this step (tested)
        e = ENV("LARECOMP_FRAME_STEPS_MAX");
        max_steps = e ? atoi(e) : 4;
        if (max_steps < 1) max_steps = 1;
        e = ENV("LARECOMP_FORCE_STEPS");
        forced = e ? atoi(e) : 0;
    }
    static int ncall;
    if (ENV("LARECOMP_LOG_FRAMEPACE") && ncall++ % 120 == 0) LOG("[framepace] call %d interval %u vsync %llu\n", ncall, interval, (unsigned long long)g_vsync_count);
    if (forced > 0) return (uint32_t)forced;
    static uint64_t last;
    uint64_t now = __atomic_load_n(&g_vsync_count, __ATOMIC_RELAXED);
    uint64_t steps = last ? now - last : 1;
    last = now;
    if (!on) return interval;
    if (steps < interval) steps = interval;  // (in its 30 fps mode the game advances two steps per frame whatever it presents)
    if (steps < 1) steps = 1;
    if (steps > (uint64_t)max_steps) steps = (uint64_t)max_steps;
    if (steps > 1) g_frame_steps_late++;
    return (uint32_t)steps;
}
