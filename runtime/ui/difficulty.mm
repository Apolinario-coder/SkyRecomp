// Gameplay tab (was Difficulty): damage taken multiplier / one-hit KO (applied where the game computes Link's damage, see
// tools/patches.py DAMAGE_*_SITES); rt_hero_mode() reads the game's own Hero Mode flag (PlayerStatus+0x120 = save
// 0x55E0). Settings persist in larecomp.ini (g_settings.diff_*).
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "imgui.h"
#include "option_rows.h"

extern "C" {
#include "../hle.h"
#include "../settings.h"
int game_in_play(void);
int rt_hookshot_seed_forced(void);  // hle/hookshot.c
int rt_no_buff_seed_forced(void);   // hle/qol.c
}

#define LIVE_SAVE 0x7001CBB220ull
#define HERO_FLAG (LIVE_SAVE + 0x55E0)  // PlayerStatus (save+0x54C0) + 0x120

extern "C" int rt_hero_mode(void) { return *(volatile uint8_t *)(uintptr_t)HERO_FLAG == 1; }

// damage in quarter hearts before the Hero Mode shift; returns the damage to subtract
extern "C" int32_t rt_damage(int32_t dmg, int32_t shift) {
    static int envd = -1;  // LARECOMP_DAMAGE=<mult> / ohko: test override
    if (envd < 0) {
        const char *e = getenv("LARECOMP_DAMAGE");
        envd = e != NULL;
        if (e) {
            if (!strcmp(e, "ohko")) g_settings.diff_ohko = true;
            else g_settings.diff_damage = (float)atof(e);
        }
    }
    int32_t base = dmg << (shift & 31);
    if (base <= 0) return base;  // healing / zero: untouched
    if (g_settings.diff_ohko) return 999;
    float m = g_settings.diff_damage > 0 ? g_settings.diff_damage : 1.0f;
    int32_t r = (int32_t)lroundf((float)base * m);
    if (ENV("LARECOMP_LOG_DAMAGE")) LOG("[difficulty] damage %d (shift %d) -> %d\n", dmg, shift, r < 1 ? 1 : r);
    return r < 1 ? 1 : r;  // at least a quarter heart
}

// Once per presented frame (from save_editor_frame). (The "Hero Mode on every file" lever, g_settings.diff_hero, left
// the Gameplay tab - user, 2026-10-11 - and is no longer applied, so nobody is stuck with it; a file started in Hero
// Mode keeps the game's own flag.)
extern "C" void difficulty_frame(void) {}

// Plain levers, each at the game's own value by default (Default, off), as option rows (option_rows.h).
extern "C" void difficulty_draw(float scale) {
    opt_begin("##gameplay", scale);
    opt_section("General");
    if (rt_hookshot_seed_forced()) {  // (the seed's logic counts on it: always on, the toggle greyed out)
        bool on = true;
        opt_disable_next();
        opt_toggle("Hookshot anything", &on, false,
                   "On: this randomizer seed was made with Hookshot Anything, and its logic counts on it.");
    } else if (opt_toggle("Hookshot anything", &g_settings.cheat_hookshot, false,
                          "The hookshot latches onto any wall or object, not only its targets. Randomizer seeds made "
                          "with Hookshot Anything always have it on."))
        settings_save();
    if (rt_no_buff_seed_forced()) {
        bool on = true;
        opt_disable_next();
        opt_toggle("No Guardian Acorns / Pieces of Power", &on, false,
                   "On: this randomizer seed was made with it.");
    } else if (opt_toggle("No Guardian Acorns / Pieces of Power", &g_settings.no_buff_drops, false,
                          "Defeating enemies in a row never drops a Guardian Acorn or a Piece of Power. Randomizer seeds "
                          "made with this option always have it on."))
        settings_save();
    if (opt_toggle("Mirror mode", &g_settings.mirror_mode, false,
                   "Mirrors the game world."))
        settings_save();
    opt_section("Difficulty");
    static const char *const names[] = {"Half", "Default", "2x", "3x", "4x", "5x", "6x", "7x", "8x"};
    static const float mult[] = {0.5f, 1, 2, 3, 4, 5, 6, 7, 8};
    int di = 1;  // (the closest step to the setting: older files may hold any multiplier)
    for (int i = 0; i < 9; i++)
        if (fabsf(mult[i] - g_settings.diff_damage) < fabsf(mult[di] - g_settings.diff_damage)) di = i;
    if (opt_choice("Damage taken", &di, names, 9, 1,
                   "How much damage Link takes from every hit: half, the game's own, or up to eight times as much.")) {
        g_settings.diff_damage = mult[di];
        settings_save();
    }
    if (opt_toggle("One-hit KO", &g_settings.diff_ohko, false, "Any hit defeats Link, whatever his hearts."))
        settings_save();
    opt_end();
}
