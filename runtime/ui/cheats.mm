// Cheats tab: toggles applied to the live save every frame while a save is loaded (layout: notes/save_format.md),
// plus one-shot buttons. Settings persist in larecomp.ini (g_settings.cheat_*).
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "imgui.h"
#include "option_rows.h"

extern "C" {
#include "../hle.h"
#include "../settings.h"
int game_in_play(void);
}

#define LIVE_SAVE 0x7001CBB220ull  // main + 0x1CBB220
template <typename T> static T &F(uint32_t off) { return *(T *)(uintptr_t)(LIVE_SAVE + off); }

enum : uint32_t {
    RUPEES = 0x5540, HP = 0x5544, PIECES = 0x5550, CONTAINERS = 0x5558, UPGRADES = 0x555C, BOMBS = 0x555E,
    ARROWS = 0x555F, POWDER = 0x5560, ITEMS = 0x5568,
};

static int max_hp(void) {  // quarter-hearts, as GetMaxHealth() computes it
    return 12 + 4 * (__builtin_popcount(F<uint16_t>(CONTAINERS) & 0x1FF) + __builtin_popcountll(F<uint64_t>(PIECES)) / 4);
}

static void fill_ammo(uint32_t count_off, int item_bit, int up_bit, int base_cap) {
    if (!((F<uint32_t>(ITEMS) >> item_bit) & 1)) return;  // only bags Link owns
    int cap = ((F<uint8_t>(UPGRADES) >> up_bit) & 1) ? base_cap * 2 : base_cap;
    if (F<uint8_t>(count_off) != cap) F<uint8_t>(count_off) = (uint8_t)cap;
}

// Once per presented frame (render thread), from save_editor_frame.
extern "C" void cheats_frame(void) {
    if (!game_in_play()) return;
    static int env = -1;  // LARECOMP_CHEATS=hearts,ammo,rupees: force on for tests (not saved)
    if (env < 0) {
        const char *e = getenv("LARECOMP_CHEATS");
        env = e != NULL;
        if (e) {
            g_settings.cheat_hearts |= strstr(e, "hearts") != NULL;
            g_settings.cheat_ammo |= strstr(e, "ammo") != NULL;
            g_settings.cheat_rupees |= strstr(e, "rupees") != NULL;
        }
    }
    if (g_settings.cheat_hearts) {
        int m = max_hp();
        if (F<int16_t>(HP) > 0 && F<int16_t>(HP) < m) F<int16_t>(HP) = (int16_t)m;  // (0 = dying/respawning: leave it)
    }
    if (g_settings.cheat_ammo) {
        fill_ammo(BOMBS, 2, 4, 30);
        fill_ammo(ARROWS, 3, 5, 30);
        fill_ammo(POWDER, 8, 3, 20);
    }
    if (g_settings.cheat_rupees && F<uint32_t>(RUPEES) != 999) F<uint32_t>(RUPEES) = 999;
}

extern "C" void cheats_draw(float scale) {
    if (!game_in_play()) {
        ImGui::TextDisabled("Load a save file and get into the game to use cheats.");
        return;
    }
    opt_begin("##cheats", scale);
    opt_section("Always on");
    if (opt_toggle("Infinite hearts", &g_settings.cheat_hearts, false, "Health refills to full whenever Link is hurt."))
        settings_save();
    if (opt_toggle("Infinite ammo", &g_settings.cheat_ammo, false,
                   "Bombs, arrows and Magic Powder stay full.\n\nOnly for the bags Link already owns."))
        settings_save();
    if (opt_toggle("Infinite rupees", &g_settings.cheat_rupees, false, "Rupees stay at 999.")) settings_save();

    opt_section("One time");
    if (opt_button("Max hearts", "Gives all 9 Heart Containers and 32 Pieces of Heart (20 hearts), then refills them.\n\n"
                                 "The hearts on the HUD update after the next screen change.")) {
        F<uint16_t>(CONTAINERS) = 0x1FF;
        F<uint64_t>(PIECES) = 0xFFFFFFFFull;
        F<int16_t>(HP) = (int16_t)max_hp();
    }
    if (opt_button("Refill hearts", "Refills health to full.")) F<int16_t>(HP) = (int16_t)max_hp();
    if (opt_button("Max ammo capacity", "Upgrades the bomb bag and quiver to 60 and Magic Powder to 40, then fills "
                                        "them (for the bags Link owns).")) {
        F<uint8_t>(UPGRADES) |= 0x38;  // bits 3-5: powder 40, bombs 60, arrows 60
        fill_ammo(BOMBS, 2, 4, 30);
        fill_ammo(ARROWS, 3, 5, 30);
        fill_ammo(POWDER, 8, 3, 20);
    }

    opt_section("Companion");
    {  // save+0x555D: the follower (verified: setting it spawns that companion at the next screen / room load)
        static const char *const names[] = {"None", "BowWow", "Marin", "Ghost", "Flying Rooster"};
        int cur = F<uint8_t>(0x555D);
        if (cur < 0 || cur > 4) cur = 0;
        if (opt_choice("Companion", &cur, names, 5, 0,
                       "Who follows Link. They join at the next screen change.\n\n"
                       "Story events may expect a specific companion."))
            F<uint8_t>(0x555D) = (uint8_t)cur;
    }
    opt_end();
}
