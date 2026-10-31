// Performance numbers for the overlay (runtime/perfmon.c, F10 / Settings > Video): the game's frame rate and frame
// times, and the machine's CPU, memory and GPU load and temperatures. A value the platform doesn't give is negative.
// Adapted from Savage Wind Recomp (MIT, Copyright (c) 2026 Revivedx and the Savage Wind Recomp contributors).
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PERF_HISTORY 120  // frame times kept for the graph

typedef struct {
    // the game's frames (presents), over the last second
    float fps, frame_ms_avg, frame_ms_max, frame_ms_low1;  // low1: the 99th percentile frame time (the "1% low")
    float frame_ms[PERF_HISTORY];                           // oldest first
    // CPU: the whole machine and this process, percent of all logical cores
    char cpu_name[64];
    float cpu_total_pct, cpu_game_pct, cpu_temp_c;
    const char *cpu_temp_source;  // where the temperature comes from ("k10temp", "ACPI thermal zone", ...)
    // memory, MB
    float ram_used_mb, ram_total_mb, ram_game_mb;
    // GPU: NVIDIA through NVML, AMD through sysfs on Linux, any GPU's load through PDH on Windows, IOKit on macOS
    char gpu_name[96];
    float gpu_pct, gpu_temp_c, gpu_power_w, gpu_clock_mhz, vram_used_mb, vram_total_mb;
    float vram_game_mb;  // the game's own GPU allocations (the renderer's count: metal.m / vulkan/device.c)
} PerfStats;

void perfmon_frame(void);                                // a game frame was presented (the presenting thread)
void perfmon_snapshot(PerfStats *out);                   // the latest numbers (machine numbers refresh twice a second)
void perfmon_set_gpu_hint(const char *n);                // the renderer's device name: which GPU to report
void perfmon_set_gpu_mem(float game_mb, float total_mb); // the renderer's own allocations, and its device's memory

#ifdef __cplusplus
}
#endif
