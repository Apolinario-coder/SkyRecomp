#pragma once
#include <stdint.h>

int rt_frame_rate(void);
uint64_t rt_frame_period_ns(void);
float rt_frame_scale(void);
float rt_sky_frame_step(float original);
float rt_sky_frame_ms(float original);
// The game's own swap interval, as its frame timer reads it (runtime/hle/framepace.c): 1 = 60 Hz, 2 = the game's
// own 30 fps mode (its frame timer advances two world steps per frame then). On a console the compositor holds
// each present that many vsyncs; here the present pacer (exec.c vk_pace_frame) does, so the world keeps real-time
// speed - without it a 30 fps mode presents at 60 and every animation runs twice as fast.
void rt_game_interval_seen(uint32_t interval);
uint32_t rt_game_interval(void);
