#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "imgui.h"

extern "C" {
#include "../mods.h"
#include "../settings.h"
#include "../rt.h"
}

static bool s_has_changes = false;

void tab_mods(float scale) {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10 * scale, 10 * scale));

    // Top action bar
    float btn_h = 42 * scale;
    float btn_w1 = 200 * scale;
    float btn_w2 = 140 * scale;

    if (ImGui::Button("Open Mods Folder", ImVec2(btn_w1, btn_h))) {
        mods_open_folder();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Open the 'mods/' directory in File Explorer.\nDrop your mod folders containing 'romfs/' and/or 'romfs_metadata.bin' here.");
    }

    ImGui::SameLine();
    if (ImGui::Button("Rescan", ImVec2(btn_w2, btn_h))) {
        mods_rescan();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Re-scan the 'mods/' directory for newly added or removed mods.");
    }

    if (s_has_changes) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.55f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.65f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.95f, 0.75f, 0.25f, 1.0f));
        if (ImGui::Button("Apply & Restart Game", ImVec2(240 * scale, btn_h))) {
            settings_save();
            rt_request_reboot(RT_REBOOT_GAME);
        }
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Reboots the game instantly to reload virtual RomFS with the updated mod selection and priority order.");
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    int count = mods_count();
    if (count == 0) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.75f, 0.70f, 1.0f));
        ImGui::TextUnformatted("No mods detected in the 'mods/' folder.");
        ImGui::Spacing();
        ImGui::TextUnformatted("How to install mods:");
        ImGui::BulletText("Click 'Open Mods Folder' above.");
        ImGui::BulletText("Paste your mod folder inside (e.g. 'mods/MyMod/').");
        ImGui::BulletText("The mod folder should contain a 'romfs/' directory and/or 'romfs_metadata.bin'.");
        ImGui::BulletText("Click 'Rescan' to detect the mod, then restart the game to load it.");
        ImGui::PopStyleColor();
    } else {
        ImGui::TextColored(ImVec4(0.93f, 0.76f, 0.33f, 1.0f), "Installed Mods (%d detected):", count);
        ImGui::TextDisabled("Mods at the top of the list take highest priority when overriding files.");
        ImGui::Spacing();

        ImGuiTableFlags table_flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                      ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX;

        if (ImGui::BeginTable("##modstable", 4, table_flags)) {
            ImGui::TableSetupColumn("Priority", ImGuiTableColumnFlags_WidthFixed, 100 * scale);
            ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed, 90 * scale);
            ImGui::TableSetupColumn("Mod Name & Details", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Order", ImGuiTableColumnFlags_WidthFixed, 120 * scale);
            ImGui::TableHeadersRow();

            for (int i = 0; i < count; i++) {
                ModEntry *m = mods_get(i);
                if (!m) continue;

                ImGui::PushID(i);
                ImGui::TableNextRow(0, 48 * scale);

                // Col 0: Priority number
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ImVec4(0.85f, 0.75f, 0.4f, 1.0f), "#%d", i + 1);

                // Col 1: Enabled checkbox
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                bool enabled = m->enabled;
                if (ImGui::Checkbox("##enabled", &enabled)) {
                    mods_set_enabled(i, enabled);
                    settings_save();
                    s_has_changes = true;
                }

                // Col 2: Name and info badges
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                if (m->enabled) {
                    ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.95f, 1.0f), "%s", m->name);
                } else {
                    ImGui::TextDisabled("%s (Disabled)", m->name);
                }

                ImGui::SameLine();
                if (m->enabled) {
                    ImGui::TextColored(ImVec4(0.2f, 0.85f, 0.4f, 1.0f), "[Active]");
                } else {
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "[Inactive]");
                }

                if (m->has_metadata) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.35f, 0.8f, 0.95f, 1.0f), "[LayeredFS / romfs_metadata.bin]");
                }

                ImGui::SameLine();
                ImGui::TextDisabled("(%d files)", m->file_count);

                // Col 3: Up / Down order buttons
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::BeginDisabled(i == 0);
                if (ImGui::Button(" ^ ", ImVec2(40 * scale, 34 * scale))) {
                    mods_move_up(i);
                    settings_save();
                    s_has_changes = true;
                }
                ImGui::EndDisabled();

                ImGui::SameLine();
                ImGui::BeginDisabled(i == count - 1);
                if (ImGui::Button(" v ", ImVec2(40 * scale, 34 * scale))) {
                    mods_move_down(i);
                    settings_save();
                    s_has_changes = true;
                }
                ImGui::EndDisabled();

                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    ImGui::PopStyleVar();
}
