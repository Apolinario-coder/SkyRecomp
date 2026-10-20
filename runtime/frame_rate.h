#pragma once
#include <stdint.h>

int rt_frame_rate(void);
uint64_t rt_frame_period_ns(void);
float rt_frame_scale(void);
float rt_sky_frame_step(float original);
float rt_sky_frame_ms(float original);
