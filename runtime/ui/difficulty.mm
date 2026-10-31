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

// Loftwing (tools/patches.py DASH_*_SITE, FLIGHT_SPEED_SITE)
extern "C" uint32_t rt_dash_max(void) { return g_settings.dash_max >= 3 && g_settings.dash_max <= 7 ? g_settings.dash_max : 3; }
extern "C" float rt_flight_speed(void) { return g_settings.flight_speed > 0 ? g_settings.flight_speed : 1.0f; }
extern "C" float rt_walk_speed(void) { return g_settings.walk_speed > 0 ? g_settings.walk_speed : 1.0f; }

// Drops (tools/patches.py DROP_*_SITE: drop table entry kinds; TREASURE_ROLL_SITES: monster materials)
extern "C" int rt_drop_kind(uint32_t kind) {
    if (kind == 1 || kind == 2) return g_settings.drop_hearts;
    if ((kind >= 3 && kind <= 5) || kind == 12 || kind == 13) return g_settings.drop_rupees;
    if (kind >= 7 && kind <= 11) return g_settings.drop_ammo;
    return 1;  // (fairies, keys, Eldin Ore, Farore's Tears, Rupoors: as the game has them)
}

extern "C" int rt_drop_always(uint64_t list) {
    uint64_t table = (list - (MAIN_BASE + 0x164c8f0)) >> 4;  // (not the Heart / Rupee Medals' extra rolls)
    return g_settings.drop_always && table != 0x27 && table != 0x28;
}

extern "C" uint32_t rt_treasure_roll(uint64_t ret, uint32_t won) {
    // tryDropItem's callers that drop an item: Bokoblins (skulls), Chuchus, Keese, Guay, Lizalfos... (+4: return address)
    static const uint32_t item_calls[] = {0x2c782c, 0x2e4de4, 0x2f09f8, 0x3368b4, 0x358ff8, 0x35a350,
                                          0x387e50, 0x38ba34, 0x3a23a8, 0x3b5060, 0x441820};
    for (uint32_t a : item_calls)
        if (ret == MAIN_BASE + a + 4) return !g_settings.drop_treasures ? 0 : g_settings.drop_always ? 1 : won;
    return won;
}

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

extern "C" void loftwing_rows(void) {
    static const char *const dashes[] = {"3 (Default)", "4", "5", "6", "7"};
    int di = rt_dash_max() - 3;
    if (opt_choice("Dash charges", &di, dashes, 5, 0,
                   "How many dashes your Loftwing holds; they recharge one at a time as usual.\n\nApplies from the "
                   "next time you call your Loftwing. The gauge shows the extra wings from the next start of the "
                   "game.")) {
        g_settings.dash_max = di + 3;
        settings_save();
    }
    static const char *const names[] = {"0.5x", "0.75x", "Default", "1.25x", "1.5x", "2x"};
    static const float mult[] = {0.5f, 0.75f, 1, 1.25f, 1.5f, 2};
    int si = 2;
    for (int i = 0; i < 6; i++)
        if (fabsf(mult[i] - g_settings.flight_speed) < fabsf(mult[si] - g_settings.flight_speed)) si = i;
    if (opt_choice("Flight speed", &si, names, 6, 2,
                   "How fast your Loftwing flies, dashes included. Turning stays the same, so turns are wider when "
                   "faster.")) {
        g_settings.flight_speed = mult[si];
        settings_save();
    }
}

extern "C" void drop_rows(void) {
    bool c = false;
    c |= opt_toggle("Hearts", &g_settings.drop_hearts, true, "Defeated enemies, grass and pots can drop recovery hearts.");
    c |= opt_toggle("Rupees", &g_settings.drop_rupees, true, "Defeated enemies, grass and pots can drop rupees.");
    c |= opt_toggle("Ammo", &g_settings.drop_ammo, true,
                    "Defeated enemies, grass and pots can drop arrows, Deku Seeds and bombs (once you have the bag).");
    c |= opt_toggle("Treasures", &g_settings.drop_treasures, true,
                    "Defeated enemies can drop monster materials: Chu Jelly, Monster Claws, Ornamental and Golden "
                    "Skulls, Evil Crystals.");
    if (c) settings_save();
}

static void speed_row(const char *label, float *v, const char *desc) {
    static const char *const names[] = {"0.5x", "0.75x", "Default", "1.25x", "1.5x", "2x"};
    static const float mult[] = {0.5f, 0.75f, 1, 1.25f, 1.5f, 2};
    int si = 2;
    for (int i = 0; i < 6; i++)
        if (fabsf(mult[i] - *v) < fabsf(mult[si] - *v)) si = i;
    if (opt_choice(label, &si, names, 6, 2, desc)) {
        *v = mult[si];
        settings_save();
    }
}

extern "C" void link_rows(void) {
    speed_row("Walking & running speed", &g_settings.walk_speed,
              "How fast Link walks, runs and sprints. His animations keep their pace, so his feet may slide at the "
              "far ends.");
}
