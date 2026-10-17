// Hookshot anything: the hookshot latches onto any solid surface or object it hits, not only hookshot targets.
//
// The hookshot actor (ctor main+0xd7e780; states Attach / Shot / Return / Adhere / WaitToReturn) casts a ray each
// frame of its Shot state (update main+0xd7ecc0). On a hit it reads the hit surface's attribute word (w23); bit
// 0x4000 = hookshot target: the hook goes to Adhere ("HookshotPierce" effect) and Link's own state pulls him to it.
// Without the bit it goes to Return ("HookshotHit" clink). tools/patches.py redirects that load here.
//
// On when the active seed was made with "hookshot-anything" (its logic counts on it), or with the Gameplay toggle.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../hle.h"
#include "../rando.h"
#include "../settings.h"

#define HOOK_TARGET 0x4000u

// a seed folder's settings.json says "hookshot-anything": true
static int dir_has_it(const char *dir) {
    char p[512];
    snprintf(p, sizeof p, "%s/settings.json", dir);
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    static char buf[1 << 16];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    buf[n] = 0;
    fclose(f);
    return strstr(buf, "\"hookshot-anything\": true") != NULL;
}

static int g_seed_on = -1;  // (per seed: read again after an in-place reboot)
static int seed_on(void) {
    if (g_seed_on < 0) {
        const char *dir = rando_seed_dir();
        g_seed_on = dir ? dir_has_it(dir) : 0;
        LOG("[hookshot] hookshot anything %s by the seed\n", g_seed_on ? "ON" : "off");
    }
    return g_seed_on;
}

// The seed forces it on (the Gameplay toggle is greyed out then). On the start screen: the seed PLAY would start
// (g_settings.rando_seed, kept up to date there), read from its folder - rando_seed_dir() would apply its patches
// before the game is loaded.
extern int g_start_screen;  // warp.c
int rt_hookshot_seed_forced(void) {
    if (!g_start_screen) return seed_on();
    static char last[128] = "\x01";
    static int on;
    if (strcmp(last, g_settings.rando_seed)) {
        snprintf(last, sizeof last, "%s", g_settings.rando_seed);
        char dir[256];
        snprintf(dir, sizeof dir, "rando/%s", g_settings.rando_seed);
        on = g_settings.rando_seed[0] && dir_has_it(dir);
    }
    return on;
}

int rt_hookshot_anything(void) {
    const char *e = ENV("LARECOMP_HOOKSHOT_ANY");  // test: 1 / 0 forces it
    if (e) return atoi(e) != 0;
    return g_settings.cheat_hookshot || seed_on();
}

// x0..: the Shot update's frame; attr = the hit surface's attribute word (sp+0x38)
uint32_t rt_hookshot_attr(Cpu *c, uint32_t attr) {
    if (ENV("LARECOMP_HOOKSHOT_LOG")) {
        uint64_t res = LD64(c->sp + 0xa8);  // hit record
        LOG("[hookshot] hit attr %#x rec %#llx\n", attr, (unsigned long long)res);
    }
    if (rt_hookshot_anything()) attr |= HOOK_TARGET;
    return attr;
}

// ---- test hook: LARECOMP_HOOKSHOT_TOUR=<step>;<step>...  step = <Level>:<Locator>:<x>:<z>:<script>
// Warps to Level/Locator, waits for the level, puts Link at x, z, then runs <script> (relative frames):
// "<t>:<L|R|U|D|A|B|X|Y>:<frames>" input, "<t>:S:0" window shot (build/tour_<SHOT_ID>_<step>_<t>.png),
// "<t>:E:0" end of the step. Link's position is logged every 10 frames ([tour]). (The shots come out upside down.)
char g_window_shot_request[256];

void hookshot_tour_tick(uint64_t frame) {
    static char *steps[64];
    static int nsteps = -1, cur, phase;
    static uint64_t t0;
    if (nsteps < 0) {
        nsteps = 0;
        const char *e = getenv("LARECOMP_HOOKSHOT_TOUR");
        if (!e) return;
        char *s = strdup(e);
        for (char *p = strtok(s, ";"); p && nsteps < 64; p = strtok(NULL, ";")) steps[nsteps++] = p;
        LOG("[tour] %d steps\n", nsteps);
    }
    if (cur >= nsteps) return;
    extern int game_in_play(void);
    extern void warp_request(const char *, const char *);
    extern int warp_busy(void);
    extern uint64_t doorjump_player(uint64_t);
    if (!game_in_play() || frame < 2700) return;
    char lv[64], loc[64], script[512] = "";
    float x, z;
    if (sscanf(steps[cur], "%63[^:]:%63[^:]:%f:%f:%511s", lv, loc, &x, &z, script) < 4) { cur++; return; }
    if (phase == 0) { warp_request(lv, loc); phase = 1; return; }
    if (phase == 1) { if (!warp_busy()) { phase = 2; t0 = frame; } return; }
    if (phase == 2) {
        if (frame < t0 + 150) return;
        uint64_t pc = doorjump_player(frame);
        if (!pc) return;
        float *pos = (float *)(uintptr_t)(pc + 0x80), *pos2 = (float *)(uintptr_t)(pc + 0xb0);
        pos[0] = pos2[0] = x;
        pos[2] = pos2[2] = z;
        LOG("[tour] f%llu step %d: %s %s Link -> (%.2f, %.2f)\n", (unsigned long long)frame, cur, lv, loc, x, z);
        phase = 3;
        t0 = frame;
        return;
    }
    long t = (long)(frame - t0);
    uint64_t pc = doorjump_player(frame);
    static int tries;
    if (pc && t == 20) {  // the level's arrival (walk-in / fade) can put Link back at the start: place again
        const float *p = (const float *)(uintptr_t)(pc + 0x80);
        if (fabsf(p[0] - x) + fabsf(p[2] - z) > 3 && tries++ < 5) { phase = 2; t0 = frame - 120; return; }
        tries = 0;
    }
    if (pc && t % 10 == 0) {
        const float *p = (const float *)(uintptr_t)(pc + 0x80);
        LOG("[tour] step %d t%ld (%.2f, %.2f, %.2f)\n", cur, t, p[0], p[1], p[2]);
    }
    extern int32_t g_test_lx, g_test_ly;
    extern uint64_t g_test_stick_until, g_test_buttons, g_test_buttons_until;
    char buf[512];
    strlcpy(buf, script, sizeof buf);
    for (char *st = strtok(buf, "/"); st; st = strtok(NULL, "/")) {
        long d, n;
        char k;
        if (sscanf(st, "%ld:%c:%ld", &d, &k, &n) != 3 || t != d) continue;
        if (k == 'E') { cur++; phase = 0; LOG("[tour] step %d done\n", cur - 1); return; }
        if (k == 'S') {
            const char *id = getenv("SHOT_ID") ? getenv("SHOT_ID") : "x";
            snprintf(g_window_shot_request, sizeof g_window_shot_request, "build/tour_%s_%d_%ld.png", id, cur, t);
        } else if (strchr("LRUD", k)) {
            g_test_lx = k == 'L' ? -32767 : k == 'R' ? 32767 : 0;
            g_test_ly = k == 'U' ? 32767 : k == 'D' ? -32767 : 0;
            g_test_stick_until = frame + n;
        } else {
            g_test_buttons = k == 'A' ? 1 : k == 'X' ? 4 : k == 'Y' ? 8 : 2;
            g_test_buttons_until = frame + n;
        }
    }
}

RT_ON_RESET(hookshot_reset) { g_seed_on = -1; }  // (seed switch)
