// Quality-of-life options re-implemented from community GameBanana mods (credits in README.md; none of their files
// are used): 360-degree movement and blur removal ("360 Controls (And More!)"). Button prompts are
// runtime/nvn/texpatch.c, the Hero Mode icon is hidden there too.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../hle.h"
#include "../rando.h"
#include "../settings.h"

#define MAIN_BASE 0x7000000000ull

// 360-degree movement: main+0xded9f0 (tools/patches.py SNAP_DIR_SITE) turns the stick vector (s1 = x, s2 = y) into
// one of 8 directions, returned in w0 as an angle (full turn = 2^32, 0 = down, 2^30 = right). With the setting on
// it returns the stick's own angle (returns 1: the original function is skipped).
int rt_snap_dir(Cpu *c) {
    static int env = -1, logn;  // LARECOMP_MOVE_360=0/1: test override; LARECOMP_LOG_SNAP: log some calls
    if (env < 0) { const char *e = getenv("LARECOMP_MOVE_360"); env = e ? atoi(e) != 0 : 2; logn = ENV("LARECOMP_LOG_SNAP") ? 0 : -1; }
    float x, y;
    memcpy(&x, &c->v[1], 4);
    memcpy(&y, &c->v[2], 4);
    bool on = env == 2 ? g_settings.move_360 : env;
    static bool inner;
    if (inner) return 0;
    double turns = atan2(y, x) / (2 * M_PI) + 0.25;  // (the mod: floor(degrees) + 90)
    uint32_t a = (uint32_t)(int64_t)llround(turns * 4294967296.0);
    if (logn >= 0 && logn++ % 30 == 0) {  // (with the game's own 8-way answer for comparison)
        static Cpu tmp;
        tmp = *c;
        inner = true;
        dispatch_lookup(0x7000ded9f0)(&tmp);
        inner = false;
        LOG("[qol] snap (%.3f, %.3f): 8-way %#x, 360 %#x (%s)\n", x, y, (uint32_t)tmp.x[0], a, on ? "360" : "8-way");
    }
    if (!on) return 0;
    c->x[0] = a;
    return 1;
}

// No Guardian Acorns / Pieces of Power (randomizer option no-buff-drops). Enemy kills count up two bytes of PlayerStatus
// (+0xa4, +0xa5, saturating at 255; shared death code main+0x7c6950). The drop roll main+0x89b960 takes the dropper's
// ItemDrop row; a row with mAllowBuffReplace (+0x38: NormalLow / NormalMid / RupeeMid...) first checks the
// kill counters in PlayerStatus (+0xa5 >= 14: "DefenceUp" = Guardian Acorn; +0xa4 past a threshold: "PowerUp" =
// Piece of Power; the counter is then wrapped) and drops that item symbol directly - the ItemDrop rows GuardianAcorn /
// PowerPiece (emptied by LAS-Randomizer item_drops.py) aren't involved, which is why the option alone didn't work.
// tools/patches.py BUFF_REPLACE_SITE reads the flag as false here: the counters are never consumed (they stay at 255) and
// the row's own lot table is rolled as usual. Anything else that would make one (ItemPowerPiece 0xb2 / ItemGuardianAcorn 0xb3) is
// held back at the actor factory (souls.c).
// a seed folder's settings.json says "no-buff-drops": true
static int dir_no_buff(const char *dir) {
    char p[512];
    snprintf(p, sizeof p, "%s/settings.json", dir);
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    static char buf[1 << 16];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    buf[n] = 0;
    fclose(f);
    return strstr(buf, "\"no-buff-drops\": true") != NULL;
}

static int g_no_buff = -1;  // the running seed has it (per seed: read again after an in-place reboot)
static bool seed_no_buff(void) {
    if (g_no_buff < 0) {
        const char *dir = rando_seed_dir();
        g_no_buff = dir ? dir_no_buff(dir) : 0;
        LOG("[qol] no Guardian Acorns / Pieces of Power by the seed: %s\n", g_no_buff ? "ON" : "off");
    }
    return g_no_buff;
}
// on: the Gameplay tab's lever, or the seed was made with it
static bool no_buff(void) {
    const char *e = getenv("LARECOMP_NO_BUFF");  // test: 1 / 0 forces it
    if (e) return atoi(e) != 0;
    return g_settings.no_buff_drops || seed_no_buff();
}
// The seed forces it (the Gameplay toggle is greyed out). On the start screen: the seed PLAY would start, from its
// folder (rando_seed_dir() would apply its patches before the game is loaded) - as rt_hookshot_seed_forced.
extern int g_start_screen;  // warp.c
int rt_no_buff_seed_forced(void) {
    if (!g_start_screen) return seed_no_buff();
    static char last[128] = "\x01";
    static int on;
    if (strcmp(last, g_settings.rando_seed)) {
        snprintf(last, sizeof last, "%s", g_settings.rando_seed);
        char dir[256];
        snprintf(dir, sizeof dir, "rando/%s", g_settings.rando_seed);
        on = g_settings.rando_seed[0] && dir_no_buff(dir);
    }
    return on;
}
RT_ON_RESET(no_buff_reset) { g_no_buff = -1; }  // (seed switch)

// Treat Tunics as Upgrades (seed option "tunic-upgrades"): the game's add-item code sets the new tunic's bit in the
// tunics byte (save +0x555C: 0x40 red, 0x80 blue) and clears the other one (tools/patches.py TUNIC_CLEAR_SITES); with
// the option the other one stays. The game applies each tunic's effect from its own bit (attack: main+0xd1f580, red
// or a Piece of Power; damage halved after main+0xd4c738 etc: blue or a Guardian Acorn), so with both Link has both.
static int g_tunic_up = -1;  // (per seed)
int rt_tunic_upgrades(void) {
    if (g_tunic_up < 0) {
        g_tunic_up = 0;
        const char *dir = rando_seed_dir();
        char p[512];
        FILE *f = dir ? (snprintf(p, sizeof p, "%s/settings.json", dir), fopen(p, "rb")) : NULL;
        if (f) {
            static char buf[1 << 16];
            size_t n = fread(buf, 1, sizeof buf - 1, f);
            buf[n] = 0;
            fclose(f);
            g_tunic_up = strstr(buf, "\"tunic-upgrades\": true") != NULL;
        }
        LOG("[qol] tunics as upgrades: %s\n", g_tunic_up ? "ON" : "off");
    }
    return g_tunic_up;
}
RT_ON_RESET(tunic_up_reset) { g_tunic_up = -1; }

// Underwater checks (tools/rando/underwater.py, settings.json "underwater-items"): the sunken heart-piece actors hold
// randomized items and hide by their own flag, so their "do you own this heart piece already?" check is skipped
// (tools/patches.py UNDERWATER_OWNED_SITE). Item actors keep their name at +0x398 ("ItemHeartPiece-<id>").
static int g_underwater = -1;  // (per seed)
int rt_underwater_items(void) {
    if (g_underwater < 0) {
        g_underwater = 0;
        const char *dir = rando_seed_dir();
        char p[512];
        FILE *f = dir ? (snprintf(p, sizeof p, "%s/settings.json", dir), fopen(p, "rb")) : NULL;
        if (f) {
            static char buf[1 << 16];
            size_t n = fread(buf, 1, sizeof buf - 1, f);
            buf[n] = 0;
            fclose(f);
            g_underwater = strstr(buf, "\"underwater-items\": true") != NULL;
        }
        LOG("[qol] underwater items: %s\n", g_underwater ? "ON" : "off");
    }
    return g_underwater;
}

int rt_underwater_skip_owned(uint64_t actor) {
    return rt_underwater_items() && actor && !memcmp((const void *)(uintptr_t)(actor + 0x398), "ItemHeartPiece-", 15);
}
RT_ON_RESET(underwater_reset) { g_underwater = -1; }

int rt_buff_replace(int allow) {
    // LARECOMP_BUFF_PRIME=1 (test): every roll finds the Guardian Acorn counter full; =2: and every row allows buffs
    // (grass then drops Acorns unless the option is on)
    static int test = -1;
    if (test < 0) test = ENV("LARECOMP_BUFF_PRIME") ? atoi(ENV("LARECOMP_BUFF_PRIME")) : 0;
    if (test) {
        uint64_t ps = LD64(MAIN_BASE + 0x1CC1368);  // PlayerStatus*
        if (ps) ST8(ps + 0xa5, 14);
        if (test == 2) allow = 1;
        { extern uint64_t renderer_present_count(void); LOG("[qol] f%llu drop roll: buff replace %d -> %d\n", (unsigned long long)renderer_present_count(), allow, allow && !no_buff()); }
    }
    return allow && !no_buff();
}

bool rt_no_buff_actor(uint32_t type) {
    if ((type != 0xb2 && type != 0xb3) || !no_buff()) return false;
    LOG("[qol] %s held back (no-buff-drops)\n", type == 0xb2 ? "Piece of Power" : "Guardian Acorn");
    return true;
}

// Blur removal: the post-process's tilt-shift object (post-process manager +0x620, set up once at boot by
// main+0x1071220) copies the name at main+0x16cbd73 ("PFXTiltShiftParam") into a std::string (+0x58) and makes its
// uniform buffer (+0x50) with a pointer to that text (buffer +0x60). The buffer is bound to postprocess.bfsha's block
// of that name each frame; under any other name the shader gets no parameters and draws no blur (LAS-Randomizer
// 'blur-removal', patch by theboy181, renames the string in the module - which only took effect at the next start,
// the copy being made once). tools/patches.py TILT_INIT_SITE hands us the object; its copy's first letter is
// switched P <-> X as the setting changes, live.
static uint64_t g_tilt_obj;
static int g_tilt_applied = -1;  // blur removal as last applied (-1: not yet)
RT_ON_RESET(qol_reset) { g_tilt_obj = 0; g_tilt_applied = -1; }  // (reboot: a new post-process)

void rt_tilt_obj(uint64_t obj) { g_tilt_obj = obj; g_tilt_applied = -1; }

static bool apply_tilt(int removed) {  // false: not set up yet (or not the name we know)
    uint64_t buf = g_tilt_obj ? LD64(g_tilt_obj + 0x50) : 0;
    char *name = buf ? (char *)(uintptr_t)LD64(buf + 0x60) : NULL;
    if (!name || memcmp(name + 1, "FXTiltShiftParam", 17) || (name[0] != 'P' && name[0] != 'X')) return false;
    name[0] = removed ? 'X' : 'P';
    return true;
}

// LARECOMP_SETTING_AT="frame:key=value,..." (test): flip an in-game menu toggle at that presented frame, as the menu
// would (move_360, blur_removal, dpad_move).
static void setting_at_tick(void) {
    static const char *spec;
    static int init;
    static uint64_t last;
    if (!init) { init = 1; spec = getenv("LARECOMP_SETTING_AT"); }
    if (!spec) return;
    extern uint64_t renderer_present_count(void);
    uint64_t now = renderer_present_count();
    if (now == last) return;
    char buf[512];
    snprintf(buf, sizeof buf, "%s", spec);
    for (char *save = NULL, *tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        unsigned long f;
        char key[32];
        int v;
        if (sscanf(tok, "%lu:%31[a-z_0-9]=%d", &f, key, &v) != 3 || f <= last || f > now) continue;
        bool *b = !strcmp(key, "move_360") ? &g_settings.move_360 : !strcmp(key, "blur_removal") ? &g_settings.blur_removal :
                  !strcmp(key, "dpad_move") ? &g_settings.dpad_move : NULL;
        if (b) { *b = v != 0; LOG("[qol] test: f%llu %s = %d\n", (unsigned long long)now, key, v); }
    }
    last = now;
}

// Game thread, once per frame (runtime/hle/input.c).
void qol_tick(void) {
    setting_at_tick();
    static int env = -1;  // LARECOMP_BLUR_REMOVAL=0/1: test override
    if (env < 0) { const char *e = getenv("LARECOMP_BLUR_REMOVAL"); env = e ? atoi(e) != 0 : 2; }
    int want = env == 2 ? g_settings.blur_removal : env;
    if (want != g_tilt_applied && apply_tilt(want)) {
        LOG("[qol] blur removal %s\n", want ? "on" : "off");
        g_tilt_applied = want;
    }
}
