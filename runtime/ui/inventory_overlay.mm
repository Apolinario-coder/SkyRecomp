// Inventory overlay for randomizer seeds: while the pause menu's Items page is shown, a panel along its bottom
// lists every dungeon's small keys (count) plus Nightmare Key / Map / Compass / Stone Beak, read live from the
// game's per-dungeon records (keysanity), and the trade sequence (found / in hand / traded away; keysanity or trade
// quest gifts). (Randomized keys can be found anywhere, and the game only shows the current
// dungeon's keys.) With boss / mini-boss souls on, a column right of the item grid shows each dungeon's souls.
// Pets (runtime/hle/pets.c): the Ghost item sits in the equipment panel's free slot (left of the Secret Medicine),
// ticked once laid to rest; a companion card left of the panel shows who follows Link and switches it (the page
// cursor reaches it with LEFT from the grid, pets.c). The Items page is detected by tools/patches.py ITEMS_PAGE_SITE.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include "imgui.h"

extern "C" {
#include "../hle.h"
#include "../rando.h"
#include "../settings.h"
uint64_t renderer_present_count(void);
int souls_owned(int dungeon);  // runtime/hle/souls.c: -1 = no boss souls in this seed
int pets_ghost_state(void);    // runtime/hle/pets.c: -1 no Ghost item, 0 found, 1 laid to rest
int pets_menu_options(int out[6]);
int pets_current(void);
int pets_menu_slot(void);      // highlighted choice while the cursor is on the companion slot, else -1
const char *souls_at(int i, int *kind, bool *owned);
}
#include "../input_bind.h"

#define DUNGEON_RECORDS (MAIN_BASE + 0x1CBB220 + 0xE0)  // live save: 0x10-byte records (+0 keys, +1 item bits)

static volatile uint64_t g_items_frame;
static uint64_t g_items_obj;  // the Items page (x0 of ITEMS_PAGE_SITE); +0x4020 u32 = its cursor (0-11, 4 columns)

#define EVENT_FLAGS (MAIN_BASE + 0x1CBB220 + 0x5DC0)  // live save: GlobalFlags bitfield
#define TRADE_SLOT (MAIN_BASE + 0x1CBB220 + 0x555A)   // live save: current trade item (1..14, 0 = none)

// Trade sequence (save TRADE_SLOT values 1..14), the LAS flag set when each one is obtained, and the flag the trade
// NPC's flow sets when it takes the item (its "...Drop" = what it hands over; tools/rando/seed_tables.py exports them).
// Trade quest shuffle lets Link carry several at once, so the row tells which he still has. TradeBananasDone is
// seed_tables.py's alias (KikiGone with open-bridge, else StickDrop - which open-bridge sets at the start).
static const struct { const char *name, *flag, *done; } kTrade[] = {
    {"Doll", "TradeYoshiDollGet", "RibbonDrop"}, {"Ribbon", "TradeRibbonGet", "DogfoodDrop"},
    {"Dog Food", "TradeDogFoodGet", "BananaDrop"}, {"Bananas", "TradeBananasGet", "TradeBananasDone"},
    {"Stick", "TradeStickGet", "HoneycombDrop"}, {"Honey", "TradeHoneycombGet", "PineappleGet"},
    {"Pineapple", "TradePineappleGet", "HibiscusDrop"}, {"Hibiscus", "TradeHibiscusGet", "LetterDrop"},
    {"Letter", "TradeLetterGet", "BroomDrop"}, {"Broom", "TradeBroomGet", "FishhookDrop"},
    {"Hook", "TradeFishingHookGet", "BrassiereDrop"}, {"Necklace", "TradeNecklaceGet", "ScaleDrop"},
    {"Scale", "TradeMermaidsScaleGet", "MarthaStatueOpened"}, {"Lens", NULL, NULL}};
static int g_trade_flag[14], g_trade_done[14];  // -1 = not in the seed's flags.tsv (older seeds: no "done" flags)
static int g_trade_loaded;

RT_ON_RESET(inventory_overlay_reset) { g_trade_loaded = 0; g_items_frame = 0, g_items_obj = 0; }  // (the next seed's flags.tsv)

static void load_trade_flags(void) {
    g_trade_loaded = 1;
    for (int i = 0; i < 14; i++) g_trade_flag[i] = g_trade_done[i] = -1;
    char path[512];
    snprintf(path, sizeof path, "%s/flags.tsv", rando_seed_dir());
    FILE *f = fopen(path, "r");
    if (!f) return;
    char name[96];
    int idx;
    while (fscanf(f, "%95s %d", name, &idx) == 2)
        for (int i = 0; i < 14; i++) {
            if (kTrade[i].flag && !strcmp(kTrade[i].flag, name)) g_trade_flag[i] = idx;
            if (kTrade[i].done && !strcmp(kTrade[i].done, name)) g_trade_done[i] = idx;
        }
    fclose(f);
}

static bool flag_set(int i) { return i >= 0 && i < 2048 && (LD8(EVENT_FLAGS + (i >> 3)) >> (i & 7)) & 1; }

extern "C" Cpu *kernel_current_cpu(void);
extern "C" void rt_items_page_tick(void) {
    g_items_frame = renderer_present_count();
    Cpu *c = kernel_current_cpu();
    if (c) g_items_obj = c->x[0];
}

static bool items_page_shown(void) {
    uint64_t f = g_items_frame;
    return f && renderer_present_count() - f <= 2;
}
// pets.c (game thread): the grid cursor while the Items page is shown, else -1
extern "C" int inventory_items_cursor(void) {
    if (!items_page_shown() || !g_items_obj) return -1;
    uint32_t i = LD32(g_items_obj + 0x4020);
    return i < 12 ? (int)i : -1;
}

static bool companion_card(void) {
    int o[6];
    return pets_menu_options(o) >= 2;
}

// The seed's settings.json, read once per seed.
static const char *seed_settings(void) {
    const char *dir = rando_seed_dir();
    if (!dir) return NULL;
    static std::string seed;
    static char buf[1 << 16];
    if (seed != dir) {
        seed = dir;
        buf[0] = 0;
        if (FILE *f = fopen((seed + "/settings.json").c_str(), "rb")) {
            size_t n = fread(buf, 1, sizeof buf - 1, f);
            buf[n] = 0;
            fclose(f);
        }
    }
    return buf;
}

// The dungeon keys panel: on with keysanity (the seed's settings.json "keysanity" isn't "off"; keys can be anywhere,
// so the Items screen shows what each dungeon's are).
static bool keys_shown(void) {
    const char *s = seed_settings();
    const char *k = s ? strstr(s, "\"keysanity\":") : NULL;
    static const char off[] = "\"keysanity\": \"off\"";
    return k && strncmp(k, off, sizeof off - 1) != 0;
}

// The trade sequence row: with keysanity (under the keys) or with the trade quest gifts shuffled ("include-trade-gifts":
// the trade items can come from anywhere) - then on its own (user 2026-10-11).
static bool trade_shown(void) {
    const char *s = seed_settings();
    return keys_shown() || (s && strstr(s, "\"include-trade-gifts\": true"));
}

extern "C" int rt_tunic_upgrades(void);  // runtime/hle/qol.c
extern "C" bool inventory_overlay_wanted(void) {
    return (trade_shown() || pets_ghost_state() >= 0 || companion_card() || rt_tunic_upgrades()) &&
           rando_seed_dir() && items_page_shown();
}

// ---- pets. Drawn glyphs (no game art): the Ghost, and a face per companion for the card.
static ImU32 mul_alpha(ImU32 c, float a) { return (c & 0x00ffffff) | (ImU32)(((c >> 24) & 0xff) * a) << 24; }

// The Ghost: a round head over a body with a scalloped hem and a tail; closed happy eyes, a blush
static void ghost_icon(ImDrawList *dl, ImVec2 c, float s) {
    ImU32 body = IM_COL32(236, 238, 255, 255), shade = IM_COL32(176, 182, 226, 255), line = IM_COL32(70, 70, 120, 255);
    dl->AddCircleFilled(ImVec2(c.x + s * 0.04f, c.y + s * 0.08f), s * 0.42f, IM_COL32(0, 0, 0, 60), 32);  // shadow
    float hr = s * 0.36f, hy = c.y - s * 0.08f, hem = c.y + s * 0.30f;
    dl->AddCircleFilled(ImVec2(c.x, hy), hr, body, 32);
    dl->AddRectFilled(ImVec2(c.x - hr, hy), ImVec2(c.x + hr, hem), body);
    for (int k = 0; k < 3; k++)  // hem scallops
        dl->AddCircleFilled(ImVec2(c.x - hr + hr / 3 + k * hr * 2 / 3, hem), hr / 3, body, 16);
    dl->AddTriangleFilled(ImVec2(c.x - hr, hy + s * 0.18f), ImVec2(c.x - hr + s * 0.14f, hem + s * 0.06f),
                          ImVec2(c.x - hr - s * 0.16f, hem + s * 0.14f), body);  // tail
    dl->AddCircleFilled(ImVec2(c.x + s * 0.16f, c.y + s * 0.14f), s * 0.14f, mul_alpha(shade, 0.45f), 20);
    // outline: head arc, sides, scallops, tail
    float t = s * 0.045f;
    dl->PathArcTo(ImVec2(c.x, hy), hr, 3.14159f, 6.28318f, 24);
    dl->PathLineTo(ImVec2(c.x + hr, hem));
    for (int k = 2; k >= 0; k--) dl->PathArcTo(ImVec2(c.x - hr + hr / 3 + k * hr * 2 / 3, hem), hr / 3, 0.0f, 3.14159f, 10);
    dl->PathLineTo(ImVec2(c.x - hr - s * 0.16f, hem + s * 0.14f));
    dl->PathLineTo(ImVec2(c.x - hr, hy + s * 0.18f));
    dl->PathStroke(line, ImDrawFlags_Closed, t);
    for (int e = -1; e <= 1; e += 2) {
        ImVec2 ec(c.x + e * s * 0.13f, c.y - s * 0.08f);
        dl->AddBezierQuadratic(ImVec2(ec.x - s * 0.06f, ec.y + s * 0.02f), ImVec2(ec.x, ec.y - s * 0.07f),
                               ImVec2(ec.x + s * 0.06f, ec.y + s * 0.02f), line, t, 10);
        dl->AddCircleFilled(ImVec2(ec.x + e * s * 0.04f, ec.y + s * 0.09f), s * 0.045f, IM_COL32(255, 150, 170, 200), 12);
    }
    dl->AddCircleFilled(ImVec2(c.x, c.y + s * 0.05f), s * 0.035f, line, 10);  // little "o" mouth
}

// "Turned in": a green tick with a dark outline over the slot's lower right
static void tick(ImDrawList *dl, ImVec2 c, float s) {
    ImVec2 p[3] = {ImVec2(c.x - s * 0.30f, c.y), ImVec2(c.x - s * 0.08f, c.y + s * 0.22f), ImVec2(c.x + s * 0.34f, c.y - s * 0.26f)};
    dl->AddPolyline(p, 3, IM_COL32(30, 50, 20, 255), 0, s * 0.22f);
    dl->AddPolyline(p, 3, IM_COL32(120, 230, 90, 255), 0, s * 0.12f);
}

// Companion faces: BowWow (black chain chomp, white eye, red mouth), Marin (red hair, hibiscus), the Flying Rooster
// (white bird, red comb, yellow beak), the Ghost (older seeds: he follows), none (an empty ring)
static void companion_icon(ImDrawList *dl, ImVec2 c, float s, int who) {
    float r = s * 0.42f;
    switch (who) {
    case 1:
        dl->AddCircleFilled(c, r, IM_COL32(28, 28, 34, 255), 32);
        dl->AddCircleFilled(ImVec2(c.x - r * 0.35f, c.y - r * 0.45f), r * 0.18f, IM_COL32(120, 120, 140, 160), 12);
        dl->AddCircleFilled(ImVec2(c.x + r * 0.25f, c.y - r * 0.25f), r * 0.26f, IM_COL32(255, 255, 255, 255), 16);
        dl->AddCircleFilled(ImVec2(c.x + r * 0.30f, c.y - r * 0.25f), r * 0.11f, IM_COL32(20, 20, 20, 255), 10);
        dl->AddTriangleFilled(ImVec2(c.x - r * 0.35f, c.y + r * 0.10f), ImVec2(c.x + r * 0.85f, c.y + r * 0.15f),
                              ImVec2(c.x + r * 0.35f, c.y + r * 0.70f), IM_COL32(200, 40, 50, 255));
        for (int t = 0; t < 3; t++)  // teeth
            dl->AddTriangleFilled(ImVec2(c.x - r * 0.10f + t * r * 0.28f, c.y + r * 0.12f),
                                  ImVec2(c.x + r * 0.18f + t * r * 0.28f, c.y + r * 0.13f),
                                  ImVec2(c.x + r * 0.04f + t * r * 0.28f, c.y + r * 0.32f), IM_COL32(255, 255, 255, 255));
        break;
    case 2:
        dl->AddCircleFilled(ImVec2(c.x, c.y + r * 0.05f), r, IM_COL32(190, 60, 50, 255), 32);  // hair
        dl->AddCircleFilled(ImVec2(c.x, c.y + r * 0.18f), r * 0.66f, IM_COL32(255, 222, 190, 255), 28);  // face
        dl->PathArcTo(ImVec2(c.x, c.y - r * 0.05f), r * 0.70f, 3.14159f, 6.28318f, 16);  // fringe
        dl->PathFillConvex(IM_COL32(190, 60, 50, 255));
        for (int e = -1; e <= 1; e += 2)
            dl->AddCircleFilled(ImVec2(c.x + e * r * 0.25f, c.y + r * 0.22f), r * 0.09f, IM_COL32(60, 40, 40, 255), 10);
        for (int k = 0; k < 5; k++) {  // hibiscus
            float a = k * 1.2566f;
            dl->AddCircleFilled(ImVec2(c.x + r * 0.62f + cosf(a) * r * 0.16f, c.y - r * 0.45f + sinf(a) * r * 0.16f),
                                r * 0.14f, IM_COL32(255, 90, 140, 255), 10);
        }
        dl->AddCircleFilled(ImVec2(c.x + r * 0.62f, c.y - r * 0.45f), r * 0.08f, IM_COL32(255, 230, 120, 255), 8);
        break;
    case 4:
        dl->AddTriangleFilled(ImVec2(c.x - r * 0.25f, c.y - r * 0.55f), ImVec2(c.x + r * 0.05f, c.y - r * 1.0f),
                              ImVec2(c.x + r * 0.30f, c.y - r * 0.55f), IM_COL32(220, 40, 40, 255));  // comb
        dl->AddCircleFilled(c, r * 0.80f, IM_COL32(250, 250, 245, 255), 28);
        dl->AddTriangleFilled(ImVec2(c.x + r * 0.62f, c.y - r * 0.15f), ImVec2(c.x + r * 1.05f, c.y + r * 0.02f),
                              ImVec2(c.x + r * 0.62f, c.y + r * 0.18f), IM_COL32(250, 190, 40, 255));  // beak
        dl->AddCircleFilled(ImVec2(c.x + r * 0.55f, c.y + r * 0.35f), r * 0.13f, IM_COL32(220, 40, 40, 255), 10);
        dl->AddCircleFilled(ImVec2(c.x + r * 0.30f, c.y - r * 0.18f), r * 0.10f, IM_COL32(20, 20, 20, 255), 10);
        dl->AddBezierQuadratic(ImVec2(c.x - r * 0.55f, c.y + r * 0.05f), ImVec2(c.x - r * 0.15f, c.y + r * 0.55f),
                               ImVec2(c.x + r * 0.20f, c.y + r * 0.15f), IM_COL32(190, 190, 200, 255), r * 0.10f, 12);
        break;
    case 3: ghost_icon(dl, c, s * 0.95f); break;
    default:
        dl->AddCircle(c, r * 0.7f, IM_COL32(150, 128, 90, 220), 28, s * 0.05f);
        dl->AddLine(ImVec2(c.x - r * 0.45f, c.y + r * 0.45f), ImVec2(c.x + r * 0.45f, c.y - r * 0.45f),
                    IM_COL32(150, 128, 90, 220), s * 0.05f);
    }
}

// The confirm button (Switch A, wherever it is bound) as the game's prompts draw it: a light disc with the
// controller's own letter / symbol (texpatch.c relabels the game's glyphs the same way)
static void confirm_glyph(ImDrawList *dl, ImFont *font, ImVec2 c, float r) {
    int h = bind_prompt_host(BIND_A), fam = bind_label_family();
    ImU32 bg = IM_COL32(240, 232, 210, 255), fg = IM_COL32(52, 38, 20, 255);
    dl->AddCircleFilled(c, r, bg, 24);
    float t = r * 0.16f;
    if (fam == 2 && h >= 0 && h <= 3) {  // PlayStation: cross / circle / square / triangle (south, east, west, north)
        float k = r * 0.45f;
        if (h == 0) {
            dl->AddLine(ImVec2(c.x - k, c.y - k), ImVec2(c.x + k, c.y + k), fg, t);
            dl->AddLine(ImVec2(c.x - k, c.y + k), ImVec2(c.x + k, c.y - k), fg, t);
        } else if (h == 1) {
            dl->AddCircle(c, k, fg, 20, t);
        } else if (h == 2) {
            dl->AddRect(ImVec2(c.x - k * 0.85f, c.y - k * 0.85f), ImVec2(c.x + k * 0.85f, c.y + k * 0.85f), fg, 0, 0, t);
        } else {
            dl->AddTriangle(ImVec2(c.x, c.y - k), ImVec2(c.x + k, c.y + k * 0.7f), ImVec2(c.x - k, c.y + k * 0.7f), fg, t);
        }
        return;
    }
    static const char *const nin[4] = {"B", "A", "Y", "X"}, *const xb[4] = {"A", "B", "X", "Y"};  // south east west north
    const char *l = h >= 0 && h <= 3 ? (fam == 1 ? xb[h] : nin[h]) : bind_pad_label(h, fam);
    float fs = r * 1.45f;
    ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0, l);
    dl->AddText(font, fs, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), fg, l);
}

// The game's cursor look: four yellow corner brackets
static void cursor_frame(ImDrawList *dl, ImVec2 a, ImVec2 b, float u) {
    ImU32 col = IM_COL32(250, 226, 90, 255);
    float l = 16 * u, t = 4 * u;
    ImVec2 c[4] = {a, ImVec2(b.x, a.y), b, ImVec2(a.x, b.y)};
    for (int k = 0; k < 4; k++) {
        float sx = k == 1 || k == 2 ? -1.0f : 1.0f, sy = k >= 2 ? -1.0f : 1.0f;
        ImVec2 p[3] = {ImVec2(c[k].x, c[k].y + sy * l), c[k], ImVec2(c[k].x + sx * l, c[k].y)};
        dl->AddPolyline(p, 3, col, 0, t);
    }
}

static void pets_draw(ImDrawList *dl, ImFont *font, float ox, float oy, float u) {
    // the Ghost: the equipment panel's bottom-left slot (game layout 188, 444; the Secret Medicine's is 242, 444)
    int g = pets_ghost_state();
    if (g >= 0) {
        ImVec2 c(ox + 188 * u, oy + 444 * u);
        ghost_icon(dl, c, 50 * u);
        if (g > 0) tick(dl, ImVec2(c.x + 12 * u, c.y + 12 * u), 30 * u);
    }
    // companion card: the free strip left of the panel, under the heart container
    int o[6], n = pets_menu_options(o), cur = pets_current();
    if (n < 2) return;
    static const char *const kShort[5] = {"None", "BowWow", "Marin", "Ghost", "Rooster"};
    ImVec2 a(ox + 62 * u, oy + 214 * u), b(ox + 140 * u, oy + 486 * u);
    float cx = (a.x + b.x) * 0.5f;
    dl->AddRectFilled(a, b, IM_COL32(52, 38, 20, 215), 12 * u);
    dl->AddRect(a, b, IM_COL32(214, 176, 92, 230), 12 * u, 0, 2 * u);
    auto text = [&](const char *t, float y, float fs, ImU32 col) {
        ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0, t);
        dl->AddText(font, fs, ImVec2(cx - ts.x * 0.5f, y), col, t);
    };
    text("Companion", a.y + 8 * u, 15 * u, IM_COL32(240, 206, 110, 255));
    ImVec2 ic(cx, a.y + 64 * u);
    dl->AddCircleFilled(ic, 28 * u, IM_COL32(120, 92, 40, 235), 32);
    dl->AddCircle(ic, 28 * u, IM_COL32(255, 220, 120, 255), 32, 2 * u);
    companion_icon(dl, ic, 44 * u, cur);
    // every choice: the one following lit, the slot's own highlight (cursor on the slot) outlined
    int sel = pets_menu_slot();
    for (int i = 0; i < n; i++) {
        float y = a.y + 104 * u + i * 24 * u;
        bool on = o[i] == cur;
        ImVec2 ra(a.x + 6 * u, y - 2 * u), rb(b.x - 6 * u, y + 20 * u);
        if (on) dl->AddRectFilled(ra, rb, IM_COL32(120, 92, 40, 235), 6 * u);
        if (i == sel) dl->AddRect(ra, rb, IM_COL32(250, 226, 90, 255), 6 * u, 0, 2 * u);
        text(kShort[o[i]], y, 16 * u, on || i == sel ? IM_COL32(255, 240, 200, 255) : IM_COL32(170, 150, 115, 230));
    }
    if (sel >= 0) {  // on the slot: the game-style cursor around the card, "(A) Choose" under the list
        cursor_frame(dl, ImVec2(a.x - 6 * u, a.y - 6 * u), ImVec2(b.x + 6 * u, b.y + 6 * u), u);
        float y = b.y - 22 * u, fs = 15 * u;
        ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0, "Choose");
        float w = 22 * u + ts.x, x0 = cx - w * 0.5f;
        confirm_glyph(dl, font, ImVec2(x0 + 8 * u, y), 9 * u);
        dl->AddText(font, fs, ImVec2(x0 + 22 * u, y - ts.y * 0.5f), IM_COL32(255, 245, 225, 255), "Choose");
    } else {  // a hint that the grid's cursor reaches it: an arrow on the card's right edge
        float y = (a.y + b.y) * 0.5f;
        dl->AddTriangleFilled(ImVec2(b.x - 4 * u, y - 7 * u), ImVec2(b.x - 4 * u, y + 7 * u), ImVec2(b.x - 12 * u, y),
                              IM_COL32(214, 176, 92, 230));
    }
}

// Treat Tunics as Upgrades (runtime/hle/qol.c): a small box under the companion card's place, the same width, with the
// two tunics - lit once owned (save +0x555C: 0x40 red, 0x80 blue)
static void tunics_draw(ImDrawList *dl, ImFont *font, float ox, float oy, float u) {
    ImVec2 a(ox + 62 * u, oy + 496 * u), b(ox + 140 * u, oy + 556 * u);
    float cx = (a.x + b.x) * 0.5f;
    dl->AddRectFilled(a, b, IM_COL32(52, 38, 20, 215), 12 * u);
    dl->AddRect(a, b, IM_COL32(214, 176, 92, 230), 12 * u, 0, 2 * u);
    const char *t = "Tunics";
    float fs = 15 * u;
    ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0, t);
    dl->AddText(font, fs, ImVec2(cx - ts.x * 0.5f, a.y + 6 * u), IM_COL32(240, 206, 110, 255), t);
    int bits = LD8(MAIN_BASE + 0x1CBB220 + 0x555C);
    static const struct { int bit; ImU32 on; } kT[2] = {{0x40, IM_COL32(222, 64, 52, 255)}, {0x80, IM_COL32(60, 120, 230, 255)}};
    for (int i = 0; i < 2; i++) {
        ImVec2 c(cx + (i ? 15 : -15) * u, a.y + 39 * u);
        if (bits & kT[i].bit) {
            dl->AddCircleFilled(c, 9 * u, kT[i].on, 24);
            dl->AddCircle(c, 9 * u, IM_COL32(255, 220, 120, 255), 24, 1.6f * u);
        } else {
            dl->AddCircle(c, 9 * u, IM_COL32(120, 100, 70, 200), 24, 1.4f * u);
        }
    }
}

struct Dungeon { const char *label; int index; };
// record = 2 x (dungeon number - 1); the Color Dungeon is record 18 (verified from the in-dungeon key HUD)
static const Dungeon kDungeons[] = {{"D1", 0}, {"D2", 2}, {"D3", 4}, {"D4", 6}, {"D5", 8},
                                    {"D6", 10}, {"D7", 12}, {"D8", 14}, {"D0", 18}};

static bool record(int i, int *keys, int *bits) {
    uint64_t p = DUNGEON_RECORDS + 0x10ull * i;
    *keys = LD8(p);
    *bits = LD8(p + 1);
    return true;
}

// A small key glyph: ring + shaft + teeth.
static void key_icon(ImDrawList *dl, ImVec2 c, float s, ImU32 col) {
    dl->AddCircle(ImVec2(c.x, c.y - s * 0.45f), s * 0.28f, col, 16, s * 0.14f);
    dl->AddRectFilled(ImVec2(c.x - s * 0.07f, c.y - s * 0.2f), ImVec2(c.x + s * 0.07f, c.y + s * 0.6f), col);
    dl->AddRectFilled(ImVec2(c.x + s * 0.07f, c.y + s * 0.3f), ImVec2(c.x + s * 0.28f, c.y + s * 0.4f), col);
    dl->AddRectFilled(ImVec2(c.x + s * 0.07f, c.y + s * 0.48f), ImVec2(c.x + s * 0.22f, c.y + s * 0.58f), col);
}

// A boss soul glyph: a wisp (round body, flame tip).
static void soul_icon(ImDrawList *dl, ImVec2 c, float s, bool owned, bool mini = false) {
    // lit: the souls' evil red as drawn in the game (runtime/hle/souls.c souls_fairy_tint): boss blood red, mini-boss ember
    ImU32 col = owned ? (mini ? IM_COL32(235, 85, 20, 255) : IM_COL32(220, 30, 36, 255)) : IM_COL32(120, 100, 70, 200);
    ImVec2 tip(c.x + s * 0.12f, c.y - s * 0.62f), l(c.x - s * 0.3f, c.y - s * 0.05f), r(c.x + s * 0.3f, c.y - s * 0.05f);
    if (owned) {
        dl->AddCircleFilled(c, s * 0.3f, col, 16);
        dl->AddTriangleFilled(l, tip, r, col);
        dl->AddCircleFilled(ImVec2(c.x - s * 0.1f, c.y - s * 0.02f), s * 0.07f, IM_COL32(60, 0, 6, 255), 8);
        dl->AddCircleFilled(ImVec2(c.x + s * 0.1f, c.y - s * 0.02f), s * 0.07f, IM_COL32(60, 0, 6, 255), 8);
    } else {
        dl->AddCircle(c, s * 0.3f, col, 16, s * 0.07f);
        dl->AddLine(l, tip, col, s * 0.07f);
        dl->AddLine(tip, r, col, s * 0.07f);
    }
}

// Souls (boss / mini-boss soul options): a column right of the item grid, one row per dungeon - its label, the
// boss's wisp, then its mini-bosses' wisps (each mini-boss kind under the dungeon it first appears in), lit = found.
static const struct { int dn; const char *label; const char *minis[2]; } kSoulRows[] = {
    {1, "D1", {"Rolling Bones"}}, {2, "D2", {"Hinox"}}, {3, "D3", {"Dodongo Snakes"}}, {4, "D4", {"Cue Ball"}},
    {5, "D5", {"Master Stalfos", "Gohma"}}, {6, "D6", {"Smasher"}}, {7, "D7", {"Grim Creeper"}}, {8, "D8", {"Blaino"}},
    {0, "D0", {"Avalaunch", "Giant Buzz Blob"}}};

static int mini_soul(const char *name) {  // -1 not in this seed, 0 missing, 1 found
    int kind;
    bool owned;
    for (int i = 0; const char *nm = souls_at(i, &kind, &owned); i++)
        if (kind == 1 && !strcmp(nm, name)) return owned;
    return -1;
}

static void souls_column(ImDrawList *dl, ImFont *font, float ox, float oy, float u) {
    bool any = false;
    for (const auto &r : kSoulRows) {
        any |= souls_owned(r.dn) >= 0;
        for (const char *m : r.minis) any |= m && mini_soul(m) >= 0;
    }
    if (!any) return;
    ImVec2 a(ox + 1138 * u, oy + 190 * u), b(ox + 1246 * u, oy + 498 * u);
    dl->AddRectFilled(a, b, IM_COL32(52, 38, 20, 205), 12 * u);
    dl->AddRect(a, b, IM_COL32(214, 176, 92, 230), 12 * u, 0, 2 * u);
    int n = (int)(sizeof kSoulRows / sizeof kSoulRows[0]);
    float rh = (b.y - a.y - 8 * u) / n, fs = 19 * u;
    for (int i = 0; i < n; i++) {
        float cy = a.y + 4 * u + rh * (i + 0.5f);
        dl->AddText(font, fs, ImVec2(a.x + 8 * u, cy - fs * 0.5f), IM_COL32(255, 245, 225, 255), kSoulRows[i].label);
        float x = a.x + 46 * u;
        int boss = souls_owned(kSoulRows[i].dn);
        if (boss >= 0) soul_icon(dl, ImVec2(x, cy + 4 * u), 24 * u, boss > 0);
        x += 22 * u;
        for (const char *m : kSoulRows[i].minis) {
            int o = m ? mini_soul(m) : -1;
            if (o >= 0) soul_icon(dl, ImVec2(x, cy + 5 * u), 17 * u, o > 0, true);  // (smaller: a mini-boss)
            x += 17 * u;
        }
    }
}

extern "C" void inventory_overlay_draw(ImVec2 display, ImFont *font) {
    // the game's UI is drawn in a centred 16:9 box; place the panel in its free strip above the bottom rule
    float bw = fminf(display.x, display.y * 16.0f / 9.0f), bh = bw * 9.0f / 16.0f;
    float ox = (display.x - bw) * 0.5f, oy = (display.y - bh) * 0.5f;
    float u = bw / 1280.0f;  // game layout units (1280x720)
    ImVec2 p0(ox + 150 * u, oy + 512 * u), p1(ox + 1130 * u, oy + 630 * u);
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    pets_draw(dl, font, ox, oy, u);
    if (rt_tunic_upgrades()) tunics_draw(dl, font, ox, oy, u);
    bool keys = keys_shown();
    if (!keys && !trade_shown()) return;
    float trade_y = p0.y + 84 * u;  // (the trade row: under the keys, or alone in a short panel at the same place)
    if (!keys) p0.y = trade_y - 8 * u, p1.y = trade_y + 34 * u;
    dl->AddRectFilled(p0, p1, IM_COL32(52, 38, 20, 205), 14 * u);
    dl->AddRect(p0, p1, IM_COL32(214, 176, 92, 230), 14 * u, 0, 2.5f * u);
    int n = keys ? (int)(sizeof kDungeons / sizeof kDungeons[0]) : 0;
    float cw = (p1.x - p0.x) / n;
    float fs = 30 * u;
    for (int k = 0; k < n; k++) {
        int keys = 0, bits = 0;
        bool ok = record(kDungeons[k].index, &keys, &bits);
        float cx = p0.x + cw * (k + 0.5f);
        ImU32 gold = IM_COL32(240, 206, 110, 255), dim = IM_COL32(150, 128, 90, 255);
        const char *lbl = kDungeons[k].label;
        ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0, lbl);
        dl->AddText(font, fs, ImVec2(cx - ts.x * 0.5f, p0.y + 4 * u), IM_COL32(255, 245, 225, 255), lbl);
        // small keys
        key_icon(dl, ImVec2(cx - 16 * u, p0.y + 50 * u), 26 * u, ok && keys ? gold : dim);
        char cnt[8];
        snprintf(cnt, sizeof cnt, "%d", ok ? keys : 0);
        dl->AddText(font, fs * 0.95f, ImVec2(cx - 2 * u, p0.y + 36 * u), ok && keys ? gold : dim, cnt);
        // Nightmare Key (bit3), map (bit1), compass (bit0), stone beak (bit2): little pips
        static const struct { int bit; ImU32 col; } pips[] = {{3, IM_COL32(230, 80, 70, 255)}, {1, IM_COL32(120, 190, 240, 255)},
                                                              {0, IM_COL32(150, 230, 140, 255)}, {2, IM_COL32(230, 230, 230, 255)}};
        for (int q = 0; q < 4; q++) {
            ImVec2 pc(cx + (q - 1.5f) * 12 * u + 24 * u, p0.y + 64 * u);
            if (ok && (bits >> pips[q].bit) & 1) dl->AddCircleFilled(pc, 4.5f * u, pips[q].col);
            else dl->AddCircle(pc, 4.5f * u, IM_COL32(120, 100, 70, 200), 12, 1.2f * u);
        }
    }
    // trade sequence: items Link has lit, the one in the trade slot outlined, traded-away ones dark with a tick
    if (!g_trade_loaded) load_trade_flags();
    int held = LD8(TRADE_SLOT);
    float tw = (p1.x - p0.x - 16 * u) / 14, ty = trade_y, th = 26 * u, tfs = 17 * u;
    for (int i = 0; i < 14; i++) {
        bool done = flag_set(g_trade_done[i]) && held != i + 1;
        bool got = !done && (flag_set(g_trade_flag[i]) || held == i + 1);
        ImVec2 a(p0.x + 8 * u + tw * i + 2 * u, ty), b(a.x + tw - 4 * u, ty + th);
        dl->AddRectFilled(a, b, got ? IM_COL32(120, 92, 40, 235) : done ? IM_COL32(48, 40, 28, 200) : IM_COL32(70, 54, 30, 160),
                          6 * u);
        if (held == i + 1) dl->AddRect(a, b, IM_COL32(255, 220, 120, 255), 6 * u, 0, 2.2f * u);
        ImVec2 ts = font->CalcTextSizeA(tfs, FLT_MAX, 0, kTrade[i].name);
        ImU32 tc = got ? IM_COL32(255, 240, 200, 255) : done ? IM_COL32(120, 160, 100, 220) : IM_COL32(150, 130, 100, 200);
        dl->AddText(font, tfs, ImVec2((a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f), tc, kTrade[i].name);
        if (done) {  // a small tick in the corner: given to its NPC
            ImVec2 k(b.x - 9 * u, a.y + 3 * u);
            ImVec2 pts[3] = {ImVec2(k.x, k.y + 4 * u), ImVec2(k.x + 2.5f * u, k.y + 6.5f * u), ImVec2(k.x + 7 * u, k.y)};
            dl->AddPolyline(pts, 3, IM_COL32(140, 210, 110, 255), 0, 1.8f * u);
        }
    }
    souls_column(dl, font, ox, oy, u);
}
