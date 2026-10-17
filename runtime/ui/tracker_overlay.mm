// Check tracker for randomizer seeds: while the pause menu's Map page is shown (tools/patches.py MAP_PAGE_SITE),
// every item check is drawn on the island map: green = reachable now with Link's current items (LAS logic),
// red = not reachable yet; collected checks (their GlobalFlags bit is set) are hidden. Dungeons and houses with
// item pots are one badge with "reachable/remaining". Never shows what a check holds.
// Data: rando/<seed>/tracker.tsv (tools/rando/tracker.py). Reachability = LAS's fixpoint over its logic nodes,
// starting from the save's inventory (followers count once their location is reachable).
#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "imgui.h"

extern "C" {
#include "../hle.h"
#include "../rando.h"
#include "../settings.h"
uint64_t renderer_present_count(void);
int kernel_data_regions(uint64_t *addr, uint64_t *size, int max);
const char *souls_at(int i, int *kind, bool *owned);  // runtime/hle/souls.c
}

#define SAVE (MAIN_BASE + 0x1CBB220)
#define EVENT_FLAGS (SAVE + 0x5DC0)

static volatile uint64_t g_map_frame, g_map_opened;
static volatile int g_map_zoomed;  // the player zoomed the map (right stick): markers would no longer line up
static volatile uint64_t g_map_obj;  // x0 of the map page update (main+0xe93800)
static void map_debug_dump(uint64_t f);
extern "C" void rt_map_page_tick(Cpu *c) {
    uint64_t f = renderer_present_count();
    if (f - g_map_frame > 2) { g_map_opened = f; g_map_zoomed = 0; }  // (re)opened at the default zoom
    g_map_frame = f;
    g_map_obj = c->x[0];
    map_debug_dump(f);
}
// LARECOMP_MAP_DUMP=<frame>[,<frame>...]: save the map page object (0x4000 bytes) to build/mapobj_<frame>.bin (RE)
static void map_debug_dump(uint64_t f) {
    static char spec[256];
    static int init;
    if (!init) { init = 1; const char *e = getenv("LARECOMP_MAP_DUMP"); if (e) snprintf(spec, sizeof spec, "%s", e); }
    if (!spec[0]) return;
    char tmp[256];
    snprintf(tmp, sizeof tmp, "%s", spec);
    for (char *t = strtok(tmp, ","); t; t = strtok(NULL, ","))
        if ((uint64_t)atol(t) == f) {
            char path[64];
            snprintf(path, sizeof path, "build/mapobj_%llu.bin", (unsigned long long)f);
            FILE *o = fopen(path, "wb");
            if (o) {  // the object, then every valid pointer in its first 0x1000 bytes: [u32 off][u64 ptr][0x1000]
                fwrite((const void *)(uintptr_t)g_map_obj, 1, 0x4000, o);
                uint64_t ra[256], rs[256];
                int nr = kernel_data_regions(ra, rs, 256);
                for (uint32_t off = 0; off < 0x1000; off += 8) {
                    uint64_t p = *(uint64_t *)(uintptr_t)(g_map_obj + off);
                    bool ok = false;
                    for (int i = 0; i < nr && !ok; i++) ok = p >= ra[i] && p + 0x1000 <= ra[i] + rs[i];
                    if (!ok) continue;
                    fwrite(&off, 4, 1, o);
                    fwrite(&p, 8, 1, o);
                    fwrite((const void *)(uintptr_t)p, 1, 0x1000, o);
                }
                fclose(o);
            }
            LOG("[map] obj %llx dumped to %s\n", (unsigned long long)g_map_obj, path);
        }
}
extern "C" void input_note_rstick(int32_t ry) {
    if ((ry > 16000 || ry < -16000) && renderer_present_count() - g_map_frame <= 2) g_map_zoomed = 1;
}

// ---- data
namespace {  // file-local types (spoiler_view.mm also has a Node)
enum Op : int32_t { OP_AND = -1, OP_OR = -2, OP_NOT = -3 };  // >= 0: push token (id | count << 20)
struct Node { std::string name; int type; int id, region; std::vector<int32_t> cond, pit, heavy; };
struct Check { std::string name; float x, y; int group, flag, node; };
struct Group { std::string label; float x, y; };
enum { T_REGION, T_ITEM, T_ROOM, T_INTERACTION, T_ENEMY, T_TRICK, T_FOLLOWER };
}  // namespace

static std::unordered_map<std::string, int> g_tok;
static std::vector<std::string> g_tok_names;
static std::vector<Node> g_nodes;
static std::vector<Check> g_checks;
static std::vector<Group> g_groups;
static std::vector<int> g_seed_settings;
static std::unordered_map<std::string, int> g_flag_idx;  // flags.tsv (trade flags)
static int g_loaded = 0;

static int tok(const std::string &s) {
    auto it = g_tok.find(s);
    if (it != g_tok.end()) return it->second;
    int id = (int)g_tok_names.size();
    g_tok[s] = id;
    g_tok_names.push_back(s);
    return id;
}

// condition -> RPN. Grammar: or := and ('|' and)*, and := not ('&' not)*, not := '!' not | '(' or ')' | token[:n]
struct Parser {
    const char *p;
    std::vector<int32_t> out;
    void ws() { while (*p == ' ') p++; }
    void prim() {
        ws();
        if (*p == '!') { p++; prim(); out.push_back(OP_NOT); return; }
        if (*p == '(') { p++; expr(); ws(); if (*p == ')') p++; return; }
        const char *s = p;
        while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '-' || *p == '[' || *p == ']') p++;
        std::string name(s, p - s);
        int count = 1;
        if (*p == ':') { p++; count = atoi(p); while (*p >= '0' && *p <= '9') p++; }
        if (name.empty()) { p++; return; }
        out.push_back(tok(name) | (count << 20));
    }
    void conj() { prim(); for (ws(); *p == '&'; ws()) { p++; prim(); out.push_back(OP_AND); } }
    void expr() { conj(); for (ws(); *p == '|'; ws()) { p++; conj(); out.push_back(OP_OR); } }
};

static std::vector<int32_t> compile(const char *c) {
    if (!c || !strcmp(c, "-") || !*c) return {};
    Parser ps{c, {}};
    ps.expr();
    return ps.out;
}

static void load(void) {
    g_loaded = -1;
    const char *dir = rando_seed_dir();
    if (!dir) return;
    char path[512];
    snprintf(path, sizeof path, "%s/tracker.tsv", dir);
    FILE *f = fopen(path, "r");
    if (!f) return;
    static char line[16384];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\n")] = 0;
        std::vector<char *> c;
        for (char *s = line, *t; (t = strsep(&s, "\t"));) c.push_back(t);
        if (c[0][0] == 'S' && c.size() >= 2) g_seed_settings.push_back(tok(c[1]));
        else if (c[0][0] == 'N' && c.size() >= 7) {
            static const char *types[] = {"region", "item", "room", "interaction", "enemy", "trick", "follower"};
            Node n;
            n.type = 0;
            for (int i = 0; i < 7; i++) if (!strcmp(c[1], types[i])) n.type = i;
            n.name = c[2];
            n.id = tok(c[2]);
            n.region = strcmp(c[3], "-") ? tok(c[3]) : -1;
            n.cond = compile(c[4]);
            n.pit = compile(c[5]);
            n.heavy = compile(c[6]);
            g_nodes.push_back(std::move(n));
        } else if (c[0][0] == 'G' && c.size() >= 4) g_groups.push_back({c[1], (float)atof(c[2]), (float)atof(c[3])});
        else if (c[0][0] == 'C' && c.size() >= 6)
            g_checks.push_back({c[1], (float)atof(c[2]), (float)atof(c[3]), atoi(c[4]), atoi(c[5]), tok(c[1])});
    }
    fclose(f);
    snprintf(path, sizeof path, "%s/flags.tsv", dir);
    if ((f = fopen(path, "r"))) {
        char name[128];
        int idx;
        while (fscanf(f, "%127s %d", name, &idx) == 2) g_flag_idx[name] = idx;
        fclose(f);
    }
    g_loaded = 1;
    LOG("[tracker] %zu logic nodes, %zu checks, %zu groups\n", g_nodes.size(), g_checks.size(), g_groups.size());
}

// ---- reachability
static std::vector<int> g_acc;  // count per token

RT_ON_RESET(tracker_reset) {  // (reboot: the next seed's tracker.tsv; the map page object was guest memory)
    g_tok.clear(), g_tok_names.clear(), g_nodes.clear(), g_checks.clear(), g_groups.clear();
    g_seed_settings.clear(), g_flag_idx.clear(), g_acc.clear();
    g_loaded = 0;
    g_map_frame = g_map_opened = g_map_obj = 0;
    g_map_zoomed = 0;
}
extern "C" bool flags_ext_get(int idx);  // runtime/hle/flags_ext.c (bushes, digs: 2048+)
static bool flag_set(int i) {
    if (i >= 2048) return flags_ext_get(i);
    return i >= 0 && (LD8(EVENT_FLAGS + (i >> 3)) >> (i & 7)) & 1;
}
static int &A(int id) {  // access count; tokens interned after sizing (e.g. "x[pit]") grow the table
    if (id >= (int)g_acc.size()) g_acc.resize(id + 64, 0);
    return g_acc[id];
}
static void give(const char *name, int n = 1) { if (n > 0) A(tok(name)) += n; }

static bool eval(const std::vector<int32_t> &rpn) {
    if (rpn.empty()) return true;
    bool st[256];
    int sp = 0;
    for (int32_t o : rpn) {
        if (o == OP_AND) { sp--; st[sp - 1] = st[sp - 1] && st[sp]; }
        else if (o == OP_OR) { sp--; st[sp - 1] = st[sp - 1] || st[sp]; }
        else if (o == OP_NOT) st[sp - 1] = !st[sp - 1];
        else if (sp < 256) { int id = o & 0xFFFFF, cnt = o >> 20; st[sp++] = id < (int)g_acc.size() && g_acc[id] >= cnt; }
    }
    return sp ? st[sp - 1] : true;
}

static void inventory(void) {
    uint32_t items = LD32(SAVE + 0x5568), keyitems = LD32(SAVE + 0x556C);
    uint8_t upg = LD8(SAVE + 0x555C), songs = LD8(SAVE + 0x555B);
    auto b = [](uint32_t v, int i) { return (int)((v >> i) & 1); };
    give("sword", b(items, 0) + (b(items, 0) && b(upg, 0)));
    give("shield", b(items, 1) + (b(items, 1) && b(upg, 1)));
    give("bracelet", b(items, 10) + (b(items, 10) && b(upg, 2)));
    static const struct { const char *n; int bit; } it[] = {{"bomb", 2}, {"bow", 3}, {"hookshot", 4}, {"boomerang", 5}, {"rod", 6},
                                                             {"shovel", 7}, {"powder", 8}, {"feather", 9}, {"boots", 11}, {"ocarina", 12}};
    for (auto &x : it) give(x.n, b(items, x.bit));
    give("bottle", b(items, 13) + b(items, 14) + b(items, 15));
    static const struct { const char *n; int bit; } ki[] = {
        {"flippers", 0}, {"medicine", 1}, {"tail-key", 2}, {"slime-key", 3}, {"angler-key", 4}, {"face-key", 5}, {"bird-key", 6},
        {"full-moon-cello", 7}, {"conch-horn", 8}, {"sea-lilys-bell", 9}, {"surf-harp", 10}, {"wind-marimba", 11},
        {"coral-triangle", 12}, {"evening-calm-organ", 13}, {"thunder-drum", 14}, {"mushroom", 15}};
    for (auto &x : ki) give(x.n, b(keyitems, x.bit));
    give("shell-sensor", LD8(SAVE + 0x556E) & 1);
    give("song-ballad", b(songs, 0));
    give("song-mambo", b(songs, 1));
    give("song-soul", b(songs, 2));
    give("red-tunic", b(upg, 6));
    give("blue-tunic", b(upg, 7));
    give("powder-capacity", b(upg, 3));
    give("bomb-capacity", b(upg, 4));
    give("arrow-capacity", b(upg, 5));
    give("golden-leaf", __builtin_popcount(LD8(SAVE + 0x5582) & 0x1F));
    give("seashell", __builtin_popcountll(LD64(SAVE + 0x5548) & ((1ull << 50) - 1)));
    static const struct { const char *tok, *flag; int slot; } trade[] = {
        {"yoshi-doll", "TradeYoshiDollGet", 1}, {"ribbon", "TradeRibbonGet", 2}, {"dog-food", "TradeDogFoodGet", 3},
        {"bananas", "TradeBananasGet", 4}, {"stick", "TradeStickGet", 5}, {"honeycomb", "TradeHoneycombGet", 6},
        {"pineapple", "TradePineappleGet", 7}, {"hibiscus", "TradeHibiscusGet", 8}, {"letter", "TradeLetterGet", 9},
        {"broom", "TradeBroomGet", 10}, {"fishing-hook", "TradeFishingHookGet", 11}, {"necklace", "TradeNecklaceGet", 12},
        {"mermaid-scale", "TradeMermaidsScaleGet", 13}, {"magnifying-lens", "", 14}};
    int held = LD8(SAVE + 0x555A);
    for (auto &t : trade) {
        auto f = g_flag_idx.find(t.flag);
        give(t.tok, held == t.slot || (f != g_flag_idx.end() && flag_set(f->second)));
    }
    static const struct { const char *tok, *flag; } pets[] = {  // pets found (runtime/hle/pets.c)
        {"rooster", "unused0427"}, {"marin-companion", "unused0428"}, {"ghost", "unused0429"}};
    for (auto &t : pets) {
        auto f = g_flag_idx.find(t.flag);
        give(t.tok, f != g_flag_idx.end() && flag_set(f->second));
    }
    for (int i = 0;; i++) {  // boss / mini-boss souls (runtime/hle/souls.c): token soul-<name, lower case, dashes>
        int kind;
        bool owned;
        const char *nm = souls_at(i, &kind, &owned);
        if (!nm) break;
        char t[48] = "soul-";
        for (size_t k = 5; *nm && k < sizeof t - 1; nm++, k++) t[k] = *nm == ' ' ? '-' : (char)tolower((unsigned char)*nm);
        give(t, owned);
    }
    static const char *dn[] = {"D1", "D2", "D3", "D4", "D5", "D6", "D7", "D8"};
    for (int d = 0; d < 9; d++) {  // dungeon records: dungeon n = record 2(n-1), Color Dungeon = 18
        int rec = d < 8 ? 2 * d : 18;
        uint64_t p = SAVE + 0xE0 + 0x10ull * rec;
        char name[32];
        snprintf(name, sizeof name, "key-%s", d < 8 ? dn[d] : "D0");
        give(name, LD8(p));
        snprintf(name, sizeof name, "nightmare-key-%s", d < 8 ? dn[d] : "D0");
        give(name, (LD8(p + 1) >> 3) & 1);
    }
}

static void solve(void) {
    g_acc.assign(g_tok_names.size() + 64, 0);
    for (int s : g_seed_settings) A(s) = 1;
    inventory();
    std::vector<char> done(g_nodes.size(), 0);
    for (bool grew = true; grew;) {
        grew = false;
        for (size_t i = 0; i < g_nodes.size(); i++) {
            Node &n = g_nodes[i];
            if (done[i]) continue;
            bool ok;
            if (n.type == T_ENEMY) {
                if (eval(n.cond)) {
                    done[i] = 1;
                    A(n.id) = 1;
                    A(tok(n.name + "[pit]")) = 1;
                    A(tok(n.name + "[heavy]")) = 1;
                    grew = true;
                    continue;
                }
                if (!n.pit.empty() && !A(tok(n.name + "[pit]")) && eval(n.pit)) { A(tok(n.name + "[pit]")) = 1; grew = true; }
                if (!n.heavy.empty() && !A(tok(n.name + "[heavy]")) && eval(n.heavy)) { A(tok(n.name + "[heavy]")) = 1; grew = true; }
                continue;
            }
            ok = (n.type != T_ITEM && n.type != T_FOLLOWER) || n.region < 0 || A(n.region) > 0;
            ok = ok && eval(n.cond);
            if (!ok) continue;
            done[i] = 1;
            A(n.id) = 1;
            if (n.type == T_FOLLOWER || n.name == "moblin-cave") give("bow-wow");
            if (n.name == "rooster-statue") give("rooster");
            grew = true;
        }
    }
}

static void debug_dump(void) {
    if (!ENV("LARECOMP_LOG_TRACKER")) return;
    int nd = 0;
    for (auto &n : g_nodes) nd += A(n.id) > 0;
    LOG("[tracker] solved: %d/%zu nodes; mabe %d tarin %d sword %d mabe-bushes %d\n", nd, g_nodes.size(), A(tok("mabe")),
        A(tok("tarin")), A(tok("sword")), A(tok("mabe-bushes")));
    for (auto &n : g_nodes)
        if (n.name == "tarin" || n.name == "mabe-bushes" || n.name == "mabe") {
            std::string r;
            for (int32_t o : n.cond) { char b[32]; snprintf(b, sizeof b, " %d", o); r += b; }
            LOG("[tracker]  %s type %d region %d rpn%s\n", n.name.c_str(), n.type, n.region, r.c_str());
        }
}

// ---- drawing
static bool map_page_shown(void) {
    uint64_t f = g_map_frame;
    return f && renderer_present_count() - f <= 2;
}

extern "C" bool tracker_overlay_wanted(void) {
    if (!rando_seed_dir() || !g_settings.tracker || !map_page_shown()) return false;
    if (!g_loaded) load();
    return g_loaded > 0;
}

static std::string pretty(const std::string &n) {
    std::string s = n;
    for (char &ch : s) if (ch == '-') ch = ' ';
    if (!s.empty()) s[0] = (char)toupper(s[0]);
    return s;
}

extern "C" void tracker_overlay_draw(ImVec2 display, ImFont *font) {
    static uint64_t solved_at = ~0ull, opened_at;
    uint64_t now = renderer_present_count();
    if (now - g_map_frame > 2 || solved_at == ~0ull || now - solved_at > 30) {  // re-solve when (re)opened, then every 0.5 s
        solve();
        if (solved_at == ~0ull) debug_dump();
        solved_at = now;
    }
    (void)opened_at;
    // the game's UI is a centred 16:9 box; the island parchment spans (187,123)-(1096,630) of 1280x720 = 16x16 screens
    float bw = fminf(display.x, display.y * 16.0f / 9.0f), bh = bw * 9.0f / 16.0f;
    float ox = (display.x - bw) * 0.5f, oy = (display.y - bh) * 0.5f, u = bw / 1280.0f;
    // zoom / scroll: the map content pane N_MapCore_00 (map page object + 0x640) has local translate (+0x30, layout
    // units, y up, origin = screen centre) and scale (+0x48); at the default view they are (0,0) and 1
    float mtx = 0, mty = 0, msc = 1;
    if (uint64_t pane = g_map_obj ? LD64(g_map_obj + 0x640) : 0) {
        if (!memcmp((const void *)(uintptr_t)(pane + 0xB0), "N_MapCore", 9)) {
            mtx = *(float *)(uintptr_t)(pane + 0x30);
            mty = *(float *)(uintptr_t)(pane + 0x34);
            msc = *(float *)(uintptr_t)(pane + 0x48);
            if (!(msc > 0.1f && msc < 20)) { mtx = mty = 0; msc = 1; }
        }
    }
    auto to_screen = [&](float cx, float cy) {
        float sx = 187 + cx * (909.0f / 16), sy = 123 + cy * (507.0f / 16);  // default view, 1280x720 UI space
        sx = 640 + mtx + msc * (sx - 640);
        sy = 360 - (mty + msc * (360 - sy));
        return ImVec2(ox + sx * u, oy + sy * u);
    };
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    dl->PushClipRect(ImVec2(ox + 60 * u, oy + 112 * u), ImVec2(ox + 1222 * u, oy + 640 * u), true);  // the map window
    ImVec2 mouse = ImGui::GetIO().MousePos;
    const char *hover = NULL;
    std::vector<int> gr_reach(g_groups.size(), 0), gr_left(g_groups.size(), 0);
    int reach = 0, left = 0;
    // stack several checks of one screen side by side; a screen with more (bushsanity: up to ~50 bushes) gets one
    // small "reachable/left" badge instead of a wall of dots
    auto spot = [](const Check &c) { return (int)(c.x * 4) * 1000 + (int)(c.y * 4); };
    std::unordered_map<int, int> slot, crowd, crowd_reach;
    std::unordered_map<int, const Check *> crowd_at;
    for (const Check &c : g_checks)
        if (c.group < 0 && !flag_set(c.flag)) crowd[spot(c)]++;
    for (const Check &c : g_checks) {
        if (flag_set(c.flag)) continue;
        bool ok = A(c.node) > 0;
        left++;
        reach += ok;
        if (c.group >= 0) { gr_left[c.group]++; gr_reach[c.group] += ok; continue; }
        int key = spot(c);
        if (crowd[key] > 8) { crowd_reach[key] += ok; crowd_at.emplace(key, &c); continue; }
        int k = slot[key]++;
        ImVec2 p = to_screen(c.x, c.y);
        p.x += ((k % 4) - 1.5f) * 9 * u;
        p.y += ((k / 4) - 0.5f) * 9 * u;
        float r = 4.2f * u;
        dl->AddCircleFilled(p, r + 1.2f * u, IM_COL32(20, 14, 6, 220));
        dl->AddCircleFilled(p, r, ok ? IM_COL32(90, 230, 90, 255) : IM_COL32(230, 70, 60, 255));
        bool vis = p.x > ox + 60 * u && p.x < ox + 1222 * u && p.y > oy + 112 * u && p.y < oy + 640 * u;
        if (vis && fabsf(mouse.x - p.x) < r * 1.6f && fabsf(mouse.y - p.y) < r * 1.6f) hover = c.name.c_str();
    }
    std::vector<ImVec4> placed;  // badge rects already drawn (nudge overlapping ones down)
    static std::string hover_crowd;
    for (auto &e : crowd_at) {
        const Check &c = *e.second;
        int n = crowd[e.first], ok = crowd_reach[e.first];
        ImVec2 p = to_screen(c.x, c.y);
        char txt[16];
        snprintf(txt, sizeof txt, "%d/%d", ok, n);
        float fs = 13 * u * (msc > 1 ? fminf(msc, 1.6f) : 1.0f);
        ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0, txt);
        ImVec2 a(p.x - ts.x * 0.5f - 6 * u, p.y - ts.y * 0.5f - 1.5f * u), b(p.x + ts.x * 0.5f + 4 * u, p.y + ts.y * 0.5f + 1.5f * u);
        placed.push_back(ImVec4(a.x, a.y, b.x, b.y));
        dl->AddRectFilled(a, b, IM_COL32(20, 14, 6, 220), ts.y);
        dl->AddCircleFilled(ImVec2(a.x + 5 * u, p.y), 3 * u, ok ? IM_COL32(90, 230, 90, 255) : IM_COL32(230, 70, 60, 255));
        dl->AddText(font, fs, ImVec2(a.x + 9 * u, a.y + 1.5f * u), IM_COL32(255, 250, 235, 255), txt);
        bool vis = p.x > ox + 60 * u && p.x < ox + 1222 * u && p.y > oy + 112 * u && p.y < oy + 640 * u;
        if (vis && mouse.x >= a.x && mouse.x <= b.x && mouse.y >= a.y && mouse.y <= b.y) {
            std::string nm = pretty(c.name);
            size_t dash = c.name.rfind('-');  // "bush-13H-4" -> "Bushes 13H"
            if (!c.name.compare(0, 5, "bush-") && dash > 5) nm = "Bushes " + c.name.substr(5, dash - 5);
            hover_crowd = nm + " (" + std::to_string(n) + " checks)";
            hover = hover_crowd.c_str();
        }
    }
    const char *hover_group = NULL;
    static std::string hover_group_s;
    for (size_t g = 0; g < g_groups.size(); g++) {
        if (!gr_left[g]) continue;
        ImVec2 p = to_screen(g_groups[g].x, g_groups[g].y);
        bool dungeon = g_groups[g].label.size() == 2 && g_groups[g].label[0] == 'D';
        char txt[48];
        if (dungeon) snprintf(txt, sizeof txt, "%s %d/%d", g_groups[g].label.c_str(), gr_reach[g], gr_left[g]);
        else snprintf(txt, sizeof txt, "%d/%d", gr_reach[g], gr_left[g]);
        float fs = (dungeon ? 18 : 15) * u;
        ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0, txt);
        ImVec2 a(p.x - ts.x * 0.5f - 5 * u, p.y - ts.y * 0.5f - 2 * u), b(p.x + ts.x * 0.5f + 5 * u, p.y + ts.y * 0.5f + 2 * u);
        for (int tries = 0; tries < 6; tries++) {
            bool hit = false;
            for (auto &r : placed)
                if (a.x < r.z && b.x > r.x && a.y < r.w && b.y > r.y) { hit = true; break; }
            if (!hit) break;
            float dy = b.y - a.y + 2 * u;
            a.y += dy; b.y += dy;
        }
        placed.push_back(ImVec4(a.x, a.y, b.x, b.y));
        dl->AddRectFilled(a, b, gr_reach[g] ? IM_COL32(30, 90, 30, 230) : IM_COL32(110, 30, 25, 230), 5 * u);
        dl->AddText(font, fs, ImVec2(a.x + 5 * u, a.y + 2 * u), IM_COL32(255, 250, 235, 255), txt);
        if (mouse.x >= a.x && mouse.x <= b.x && mouse.y >= a.y && mouse.y <= b.y) {
            hover_group_s = g_groups[g].label + (dungeon ? "" : " (item pots)");
            hover_group = hover_group_s.c_str();
        }
    }
    dl->PopClipRect();
    if (!hover && hover_group) hover = hover_group;
    char sum[64];
    snprintf(sum, sizeof sum, "Checks: %d reachable / %d left", reach, left);
    dl->AddText(font, 20 * u, ImVec2(ox + 190 * u, oy + 640 * u), IM_COL32(255, 245, 225, 255), sum);
    if (hover) {
        std::string t = hover == hover_group ? std::string(hover) : pretty(hover);
        ImVec2 ts = font->CalcTextSizeA(20 * u, FLT_MAX, 0, t.c_str());
        ImVec2 a(mouse.x + 12 * u, mouse.y - ts.y - 6 * u);
        dl->AddRectFilled(ImVec2(a.x - 4 * u, a.y - 3 * u), ImVec2(a.x + ts.x + 4 * u, a.y + ts.y + 3 * u), IM_COL32(25, 18, 8, 235), 4 * u);
        dl->AddText(font, 20 * u, a, IM_COL32(255, 245, 225, 255), t.c_str());
    }
}
