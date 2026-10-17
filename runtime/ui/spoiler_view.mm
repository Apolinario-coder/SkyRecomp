// In-game spoiler page (Randomizer tab -> a seed's "Spoiler" button): shows rando/<seed>/spoiler_view.txt, written by
// tools/rando/spoiler_view.py (walkthrough by playthrough step, items by area, dungeon entrances, entrances).
// Markup: '# ' section, '## ' sub-heading, '- ' bullet, two-space continuation, plain text otherwise.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include "imgui.h"
#include "osk.h"

namespace {  // (tracker_overlay.mm has its own Node: keep both local or vector<Node> code gets merged)
struct Node {
    std::string title;
    std::vector<std::string> lines;                     // text directly under the heading
    std::vector<std::pair<std::string, std::vector<std::string>>> subs;  // "## " blocks
};

}  // namespace

static std::string g_loaded;  // seed whose file is loaded
static std::vector<Node> g_doc;
static bool g_missing;

static void load(const std::string &seed) {
    g_loaded = seed;
    g_doc.clear();
    g_missing = false;
    std::string p = "rando/" + seed + "/spoiler_view.txt";
    FILE *f = fopen(p.c_str(), "r");
    if (!f) { g_missing = true; return; }
    char buf[1024];
    while (fgets(buf, sizeof buf, f)) {
        std::string l(buf);
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        if (l.rfind("# ", 0) == 0) { g_doc.push_back({l.substr(2), {}, {}}); continue; }
        if (g_doc.empty()) g_doc.push_back({"", {}, {}});
        Node &n = g_doc.back();
        if (l.rfind("## ", 0) == 0) { n.subs.push_back({l.substr(3), {}}); continue; }
        if (!n.subs.empty()) n.subs.back().second.push_back(l);
        else n.lines.push_back(l);
    }
    fclose(f);
}

static bool matches(const std::string &s, const char *q) {
    if (!q[0]) return true;
    std::string a = s, b = q;
    for (auto &c : a) c = (char)tolower(c);
    for (auto &c : b) c = (char)tolower(c);
    return a.find(b) != std::string::npos;
}

static void line(const std::string &l) {
    if (l.rfind("- ", 0) == 0) { ImGui::Bullet(); ImGui::TextUnformatted(l.c_str() + 2); }
    else if (l.rfind("  ", 0) == 0) { ImGui::Indent(); ImGui::TextUnformatted(l.c_str() + 2); ImGui::Unindent(); }
    else if (!l.empty()) ImGui::TextWrapped("%s", l.c_str());
}

// Returns false when the page was closed (Back).
bool spoiler_view_draw(const std::string &seed, float scale) {
    static bool revealed;
    static std::string revealed_for;
    if (revealed_for != seed) { revealed = getenv("LARECOMP_SPOILER_TEST") != NULL; revealed_for = seed; }
    if (g_loaded != seed) load(seed);
    if (ImGui::Button("< Back")) return false;
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Spoiler log - %s", seed.c_str());
    ImGui::Separator();
    if (g_missing) {
        ImGui::TextWrapped("No readable spoiler for this seed yet. Generate it with: tools/rando/spoiler_view.py rando/%s",
                           seed.c_str());
        return true;
    }
    if (!revealed) {
        ImGui::Spacing();
        ImGui::TextWrapped("This shows where every item is, plus a step-by-step route to beat the seed.");
        if (ImGui::Button("Reveal spoilers", ImVec2(320 * scale, 0))) revealed = true;
        return true;
    }
    static char q[64];
    ImGui::SetNextItemWidth(420 * scale);
    ui_input_text("##q", q, sizeof q, 0, "search (item, place...)", "Search the spoiler log");
    ImGui::BeginChild("##spoiler", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NavFlattened);
    for (size_t i = 0; i < g_doc.size(); i++) {
        Node &n = g_doc[i];
        ImGui::PushID((int)i);
        if (n.title.empty() || ImGui::CollapsingHeader(n.title.c_str(), i <= 1 ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
            for (auto &l : n.lines) if (matches(l, q)) line(l);
            for (size_t k = 0; k < n.subs.size(); k++) {
                auto &sb = n.subs[k];
                bool any = matches(sb.first, q);
                for (auto &l : sb.second) any |= matches(l, q);
                if (!any) continue;
                ImGui::PushID((int)k);
                if (q[0]) ImGui::SetNextItemOpen(true);
                if (ImGui::TreeNode(sb.first.c_str())) {
                    for (auto &l : sb.second) if (matches(sb.first, q) || matches(l, q)) line(l);
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    return true;
}
