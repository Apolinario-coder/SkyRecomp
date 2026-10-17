// Shader preparation tour (first-launch shader compile, user 2026-10-08): LARECOMP_SHADER_TOUR=<frames per spot>
// warps through every warp spot of the game (runtime/ui/warp_data.h), letting each scene render for a moment so
// its shaders get translated and compiled - they land in cache/shaders/ and Metal's disk cache, which the boot
// warm-up (runtime/nvn/renderer.m shader_cache_warm) loads every later start: no first-use pop-in. Nothing derived
// from the game ships: the cache is built on the player's machine from their own copy. Exits when done.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "warp_data.h"

extern "C" {
#include "../hle.h"
int game_in_play(void);
void warp_request(const char *level, const char *locator);
int warp_busy(void);
bool warp_blocked(const char *locator);
}

#define PLAYER_STATUS_PTR (MAIN_BASE + 0x1CC1368)  // PlayerStatus*: current level name at +0

extern "C" void shader_tour_tick(uint64_t frame) {
    static int per = -1, i, waiting;
    static uint64_t since, started, applied_reset;
    if (per < 0) { const char *e = ENV("LARECOMP_SHADER_TOUR"); per = e ? atoi(e) : 0; if (e && per < 120) per = 240; }
    if (!per || !game_in_play()) return;
    static long from = -1;  // LARECOMP_TOUR_FROM=<frame>: not before (a test script still loading a save)
    if (from < 0) { const char *e = ENV("LARECOMP_TOUR_FROM"); from = e ? atol(e) : 0; }
    if ((long)frame < from) return;
    const int n = (int)(sizeof kWarpSpots / sizeof kWarpSpots[0]);
    if (!started) { started = frame; LOG("[tour] %d spots, %d frames each\n", n, per); }
    if (i >= n && ENV("LARECOMP_TOUR_LOOP")) { i = 0; LOG("[tour] loop\n"); }
    if (i >= n) {
        LOG("[tour] done: %d spots in %.0f s\n", n, (frame - started) / 60.0);
        exit(0);
    }
    // LARECOMP_TOUR_LEVEL=<level>: only that level's spots; LARECOMP_TOUR_LOOP=1: start over at the end (soak
    // tests, e.g. the overworld frame-rate dips after a long session)
    static const char *only = ENV("LARECOMP_TOUR_LEVEL");
    static const char *pre = ENV("LARECOMP_TOUR_PREFIX");  // locator prefix (e.g. Field_: Manbo's Warp_* spots
                                                           // throw when warped to from inside the Field)
    if ((only && strcmp(kWarpSpots[i].level, only)) || (pre && strncmp(kWarpSpots[i].locator, pre, strlen(pre))) ||
        warp_blocked(kWarpSpots[i].locator)) {
        i++;
        if (i >= n && ENV("LARECOMP_TOUR_LOOP")) { i = 0; LOG("[tour] loop\n"); }
        return;
    }
    const WarpSpot &w = kWarpSpots[i];
    if (!waiting) {
        warp_request(w.level, w.locator);
        waiting = 1;
        since = frame;
        applied_reset = 1;
        return;
    }
    // the warp applied (warping again while the last one still loads threw in the game: Field -> Field warps look
    // "arrived" at once), back in play, then `per` frames of rendering - or give up after 30 s
    static uint64_t applied;
    if (applied_reset) { applied = 0; applied_reset = 0; }  // (a new spot: its own wait)
    if (warp_busy() || !game_in_play()) { applied = 0; if (frame - since <= 1800) return; }
    if (!applied) applied = frame;
    uint64_t ps = LD64(PLAYER_STATUS_PTR);
    bool there = ps && !strcmp((const char *)(uintptr_t)ps, w.level);
    if ((there && frame - applied >= (uint64_t)per) || frame - since > 1800) {
        if (!there) LOG("[tour] %s / %s: not reached\n", w.level, w.locator);
        i++;
        waiting = 0;
        if (i % 20 == 0) LOG("[tour] %d / %d\n", i, n);
    }
}
