// Pets row of the in-game Randomizer tab: call any found pet (runtime/hle/pets.c) or send the companion away (the
// same choice as the Items page's companion slot, runtime/ui/inventory_overlay.mm; the swap happens at once).
// Shown once a pet has been found on a seed with pets (tools/rando/pets.py).
#include "imgui.h"

extern "C" {
int game_in_play(void);
int pets_current(void);
int pets_callable(int out[5]);
const char *pets_name(int c);
void pets_call(int c);
}

extern "C" void pets_ui_draw(void) {
    if (!game_in_play()) return;
    int c[5], n = pets_callable(c), cur = pets_current();
    bool any_pet = false;
    for (int i = 0; i < n; i++) any_pet |= c[i] >= 2;  // (Marin / Ghost / Rooster: BowWow alone isn't a pet)
    if (!any_pet) return;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Companion:");
    for (int i = -1; i < n; i++) {
        int v = i < 0 ? 0 : c[i];
        ImGui::SameLine();
        ImGui::PushID(v);
        if (ImGui::RadioButton(v ? pets_name(v) : "None", cur == v)) pets_call(v);
        ImGui::PopID();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(also on the pause menu's Items page: LEFT from the item grid)");
}
