// Cheats tab (Skyward Sword HD 1.0.0), from the community's Atmosphere codes for this build (switch-cheats-db, build
// id D824D4B1BBD45D03). Two kinds:
// - code: tools/patches.py SKY_PATCHES ask rt_cheat_*() at the instruction the code patches (stamina drain, Beetle
//   flight timer, Loftwing dash); off, the game's own instruction runs.
// - values topped up every frame: the live save file sits at a fixed spot of the game's static heap arena in main's
//   .bss (the codes' addresses). Each write first checks that the health fields look like a save file, so a layout
//   that differs (the title screen, another build) leaves memory alone.
// Settings persist in larecomp.ini (g_settings.cheat_*).
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "imgui.h"
#include "option_rows.h"

extern "C" {
#include "../hle.h"
#include "../settings.h"
}

#define SAVE_HEALTH 0x7005AF0056ull  // u16 heart capacity, u16 (unused), u16 current health: quarter hearts
#define SAVE_RUPEES 0x7005AEB7B2ull  // u16
static volatile uint16_t &U16(uint64_t a) { return *(volatile uint16_t *)(uintptr_t)a; }
#define PLAYER 0x7001826AD0ull         // dAcPy_c * (Link), null without him
#define PLAYER_VTABLE 0x7001637D28ull  // its vtable (main+0xab3c2c sets it)

extern "C" int rt_cheat_stamina(void) { return g_settings.cheat_stamina; }
extern "C" int rt_cheat_beetle(void) { return g_settings.cheat_beetle; }
extern "C" int rt_cheat_dash(void) { return g_settings.cheat_dash; }

// Once per presented frame (render thread), from save_editor_frame.
extern "C" void cheats_frame(void) {
    if (!g_is_skyward) return;
    if (g_settings.cheat_skyward) {  // the community code's "Lightning Skyward Sword", on Link himself
        uint64_t p = *(volatile uint64_t *)(uintptr_t)PLAYER;
        if (p && *(volatile uint64_t *)(uintptr_t)p == PLAYER_VTABLE) {
            *(volatile uint8_t *)(uintptr_t)(p + 0x446) |= 0x50;  // +0x444 bits 20 / 22: Skyward Strike charged
            *(volatile uint16_t *)(uintptr_t)(p + 0x641E) = 0x120;  // its timer
            *(volatile uint8_t *)(uintptr_t)(p + 0x63B7) = 0x37;
        }
    }
    static int env = -1;  // LARECOMP_CHEATS=hearts,stamina,rupees,beetle: force on for tests (not saved)
    if (env < 0) {
        const char *e = getenv("LARECOMP_CHEATS");
        env = e != NULL;
        if (e) {
            g_settings.cheat_hearts |= strstr(e, "hearts") != NULL;
            g_settings.cheat_stamina |= strstr(e, "stamina") != NULL;
            g_settings.cheat_rupees |= strstr(e, "rupees") != NULL;
            g_settings.cheat_beetle |= strstr(e, "beetle") != NULL;
        }
    }
    uint16_t cap = U16(SAVE_HEALTH), hp = U16(SAVE_HEALTH + 4);
    bool save = cap >= 12 && cap <= 80 && cap % 4 == 0 && hp <= cap;
    static int was = -1;
    if (save != was) LOG("[cheats] save file %s (capacity %u, health %u)\n", save ? "found" : "gone", cap, hp), was = save;
    if (!save) return;
    if (g_settings.cheat_hearts && hp > 0 && hp < cap) U16(SAVE_HEALTH + 4) = cap;  // (0 = defeated: left alone)
    if (g_settings.cheat_rupees && U16(SAVE_RUPEES) < 9900) U16(SAVE_RUPEES) = 9900;
}

extern "C" void cheats_draw(float scale) {
    opt_begin("##cheats", scale);
    opt_section("Always on");
    if (opt_toggle("Infinite health", &g_settings.cheat_hearts, false,
                   "Health refills to full whenever Link is hurt.\n\nA single hit bigger than all his hearts still "
                   "defeats him."))
        settings_save();
    if (opt_toggle("Infinite stamina", &g_settings.cheat_stamina, false,
                   "The stamina gauge never drains: sprint, climb and spin as long as you like."))
        settings_save();
    if (opt_toggle("Infinite rupees", &g_settings.cheat_rupees, false, "Rupees stay at 9,900.")) settings_save();
    if (opt_toggle("Infinite Beetle flight", &g_settings.cheat_beetle, false,
                   "The Beetle keeps flying until you call it back."))
        settings_save();
    if (opt_toggle("Skyward Strike always charged", &g_settings.cheat_skyward, false,
                   "Link's sword is always charged: every vertical swing sends a Skyward Strike, with no need to "
                   "hold it up."))
        settings_save();
    if (opt_toggle("Infinite air dash", &g_settings.cheat_dash, false,
                   "Your Loftwing's dashes are never used up: dash as often as you like."))
        settings_save();
    if (opt_toggle("Enemies always drop items", &g_settings.drop_always, false,
                   "Defeated enemies, grass and pots always drop something from what they can drop, and enemies "
                   "always drop their monster materials. Drops turned off in the Gameplay tab stay off."))
        settings_save();
    opt_end();
}
