// Difficulty (Gameplay tab): damage taken multiplier / one-hit KO, applied where the game
// computes Link's damage (tools/patches.py DAMAGE_SHIFT_SITE; Link's Awakening: DAMAGE_*_SITES, with rt_hero_mode()
// reading its Hero Mode flag, PlayerStatus+0x120 = save 0x55E0). Settings persist in larecomp.ini (g_settings.diff_*).
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "imgui.h"
#include "option_rows.h"

extern "C" {
#include "../hle.h"
#include "../settings.h"
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

// Once per presented frame (from save_editor_frame).
extern "C" void difficulty_frame(void) {}

// Gameplay tab rows (menu.mm tab_gameplay), each at the game's own value by default.
extern "C" void difficulty_rows(void) {
    static const char *const names[] = {"Half", "Default", "2x", "3x", "4x", "5x", "6x", "7x", "8x"};
    static const float mult[] = {0.5f, 1, 2, 3, 4, 5, 6, 7, 8};
    int di = 1;  // (the closest step to the setting: older files may hold any multiplier)
    for (int i = 0; i < 9; i++)
        if (fabsf(mult[i] - g_settings.diff_damage) < fabsf(mult[di] - g_settings.diff_damage)) di = i;
    if (opt_choice("Damage taken", &di, names, 9, 1,
                   "How much damage enemies deal to Link: half, the game's own, or up to eight times as much. On top "
                   "of Hero Mode's double damage.")) {
        g_settings.diff_damage = mult[di];
        settings_save();
    }
    if (opt_toggle("One-hit KO", &g_settings.diff_ohko, false, "Any hit defeats Link, whatever his hearts."))
        settings_save();
}
