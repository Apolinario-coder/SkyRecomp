// Chaos dungeon room shuffle: matching letters on the dungeon map (user 2026-10-08). Every doorway pair that leads
// somewhere else (rando/<seed>/door_jumps.tsv rows with a target, runtime/hle/doorjump.c) gets a letter, A-A, B-B...
// drawn at both doorways. A pair shows once Link has walked through either end, or all of them with the dungeon's
// Map (Randomizer tab: off / as explored / as explored + all with the Map; larecomp.ini chaos_letters).
//
// Placement: the dungeon map page (main+0xe78740 runs once per frame while it's shown; tools/patches.py
// DGN_MAP_SITE) holds its layout panes at page+0x8 -> +0x10: N_Map_00 (+0x70: zoom / scroll), N_MapPosition_00
// (+0x7a8: the dungeon's offset - Face Shrine -16, Tail Cave +48). The room grid P_Grid_00 is 652 x 518 layout
// units, 8 x 8 cells of 81.5 x 64.75 (1280 x 720 layout, origin at the centre, y up); found 2026-10-09 with
// LARECOMP_PANE_SCAN. Zoom and scroll live in N_MapOrigin_00 (page+0x8 -> +0x10 -> +0xf0 -> +0x6a8: translate
// +0x30/+0x34, scale +0x48; 1 at rest, 2 zoomed), found by scanning before/after a right-stick zoom.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "imgui.h"

extern "C" {
#include "../hle.h"
#include "../rando.h"
#include "../settings.h"
uint64_t renderer_present_count(void);
int doorjump_chaos_pairs(const char *level, int *ids, int max);
void doorjump_chaos_pair(int id, int *col, int *row, char *side, int *slot);
bool doorjump_chaos_seen(int id);
bool doorjump_chaos_twoway(int id);
}

#define PLAYER_STATUS_PTR (MAIN_BASE + 0x1CC1368)  // PlayerStatus*: current level name at +0
#define DUNGEON_RECORDS (MAIN_BASE + 0x1CBB220 + 0xE0)  // live save: 0x10-byte records (+1 bit 1 = Map)

static volatile uint64_t g_dm_frame, g_dm_obj;

RT_ON_RESET(chaos_overlay_reset) { g_dm_frame = g_dm_obj = 0; }

extern "C" void rt_dgnmap_tick(Cpu *c) {
    g_dm_frame = renderer_present_count();
    g_dm_obj = c->x[0];
}

extern "C" uint64_t chaos_dgnmap_obj(void) { return g_dm_obj; }  // (LARECOMP_PANE_SCAN)

static const char *current_level(void) {
    uint64_t ps = LD64(PLAYER_STATUS_PTR);
    return ps ? (const char *)(uintptr_t)ps : "";
}

static bool have_map(const char *lv) {  // Lv01..Lv08 -> record 2(n-1), Lv10 (Color Dungeon) -> 18
    if (strncmp(lv, "Lv", 2)) return false;
    int n = (lv[2] - '0') * 10 + (lv[3] - '0');
    int rec = n == 10 ? 18 : 2 * (n - 1);
    if (n < 1 || n > 10 || n == 9) return false;
    return (LD8(DUNGEON_RECORDS + 0x10ull * rec + 1) >> 1) & 1;
}

static uint64_t pane(uint64_t p, const char *name) {  // the pane at p if it has that name
    if (!p) return 0;
    return strcmp((const char *)(uintptr_t)(p + 0xB0), name) ? 0 : p;
}

extern "C" bool chaos_overlay_wanted(void) {
    if (g_settings.chaos_letters == CHAOS_LETTERS_OFF) return false;
    if (!rando_seed_dir() || renderer_present_count() - g_dm_frame > 2 || !g_dm_obj) return false;
    int ids[4];
    return doorjump_chaos_pairs(current_level(), ids, 4) > 0;
}

// rando/<seed>/dungeon_map.tsv (tools/rando/dungeon_gen.py): level, cell, x, y - the map page shift of the floor each
// cell is drawn on. The page shows one floor (its shift is N_MapPosition_00's): doorways of the other floors' cells
// are left out (Key Cavern's B1 letters were drawn on the 1F page, off its paper).
namespace {
struct MapCell { char level[24]; int col, row; float x, y; };
}
static MapCell g_cells[640];
static int g_ncells = -1;
RT_ON_RESET(chaos_overlay_cells_reset) { g_ncells = -1; }

static bool cell_shift(const char *lv, int col, int row, float *x, float *y) {
    if (g_ncells < 0) {
        g_ncells = 0;
        char p[512];
        const char *dir = rando_seed_dir();
        FILE *f = dir ? (snprintf(p, sizeof p, "%s/dungeon_map.tsv", dir), fopen(p, "r")) : NULL;
        char line[128], cell[8];
        while (f && g_ncells < 640 && fgets(line, sizeof line, f)) {
            MapCell &m = g_cells[g_ncells];
            if (sscanf(line, "%23[^\t]\t%7[^\t]\t%f\t%f", m.level, cell, &m.x, &m.y) == 4 && strlen(cell) == 3) {
                m.row = (cell[0] - '0') * 10 + (cell[1] - '0');
                m.col = cell[2] - 'A';
                g_ncells++;
            }
        }
        if (f) fclose(f);
    }
    for (int i = 0; i < g_ncells; i++)
        if (g_cells[i].col == col && g_cells[i].row == row && !strcmp(g_cells[i].level, lv)) {
            *x = g_cells[i].x, *y = g_cells[i].y;
            return true;
        }
    return false;
}

namespace {
struct Door { int col, row, slot; char side; int label; int to; bool shown; };  // to: label it leads to (one-way), -1
}

static void label_text(int i, char *out) {  // A..Z, then AA, AB...
    if (i < 26) snprintf(out, 4, "%c", 'A' + i);
    else snprintf(out, 4, "%c%c", 'A' + i / 26 - 1, 'A' + i % 26);
}

extern "C" void chaos_overlay_draw(ImVec2 display, ImFont *font) {
    uint64_t p1 = LD64(g_dm_obj + 0x8);
    uint64_t p2 = p1 ? LD64(p1 + 0x10) : 0;
    uint64_t grid = p2 ? LD64(p2 + 0xf0) : 0;
    uint64_t map = p2 ? pane(LD64(p2 + 0x70), "N_Map_00") : 0;
    uint64_t pos = p2 ? pane(LD64(p2 + 0x7a8), "N_MapPosition_00") : 0;
    uint64_t org = grid ? pane(LD64(grid + 0x6a8), "N_MapOrigin_00") : 0;
    if (!map || !pos) return;
    const float *mf = (const float *)(uintptr_t)map, *pf = (const float *)(uintptr_t)pos;
    float mtx = mf[0x30 / 4], mty = mf[0x34 / 4], msc = mf[0x48 / 4];
    if (!(msc > 0.1f && msc < 20)) { mtx = mty = 0; msc = 1; }
    // zoom (right stick) and scroll: N_MapOrigin_00 (t 0,0 s 1 at rest; zoomed 2x it centres the cursor's room)
    float otx = 0, oty = 0, osc = 1;
    if (org) {
        const float *of = (const float *)(uintptr_t)org;
        if (of[0x48 / 4] > 0.1f && of[0x48 / 4] < 20) otx = of[0x30 / 4], oty = of[0x34 / 4], osc = of[0x48 / 4];
    }
    float ptx = pf[0x30 / 4], pty = pf[0x34 / 4];
    // the game's UI is a centred 16:9 box, laid out at 1280 x 720
    float bw = fminf(display.x, display.y * 16.0f / 9.0f), bh = bw * 9.0f / 16.0f;
    float ox = (display.x - bw) * 0.5f, oy = (display.y - bh) * 0.5f, u = bw / 1280.0f;
    const float CW = 81.5f, CH = 64.75f;
    float sc = msc * osc;
    auto to_screen = [&](float lx, float ly) {  // layout (centre origin, y up) inside the map -> screen
        float x = mtx + msc * (otx + osc * (ptx + lx)), y = mty + msc * (oty + osc * (pty + ly));
        return ImVec2(ox + (640 + x) * u, oy + (360 - y) * u);
    };
    const char *lv = current_level();
    bool all = g_settings.chaos_letters == CHAOS_LETTERS_MAP && have_map(lv);
    int ids[296];
    int n = doorjump_chaos_pairs(lv, ids, 296);
    // Doorways and their labels. A two-way pair (Chaos) shares one letter, A-A. Madness rows are one-way: each doorway
    // gets its own letter plus a small arrow tag naming the doorway it leads to, shown once Link has walked through it
    // (or all with the Map); the doorway he arrived at shows its own letter.
    Door doors[600];
    int nd = 0, nlabel = 0;
    auto door = [&](int col, int row, char side, int slot) {
        for (int k = 0; k < nd; k++)
            if (doors[k].col == col && doors[k].row == row && doors[k].side == side && doors[k].slot == slot) return k;
        if (nd == 600) return -1;
        doors[nd] = Door{col, row, slot, side, -1, -1, false};
        return nd++;
    };
    for (int i = 0; i < n; i++) {
        int col[2], row[2], slot[2];
        char side[2];
        doorjump_chaos_pair(ids[i], col, row, side, slot);
        bool shown = all || doorjump_chaos_seen(ids[i]), two = doorjump_chaos_twoway(ids[i]);
        int a = door(col[0], row[0], side[0], slot[0]), b = door(col[1], row[1], side[1], slot[1]);
        if (a < 0 || b < 0) continue;
        if (two) {
            doors[a].label = doors[b].label = nlabel++;
        } else {
            if (doors[a].label < 0) doors[a].label = nlabel++;
            if (doors[b].label < 0) doors[b].label = nlabel++;
            if (shown) doors[a].to = doors[b].label;
        }
        if (shown) doors[a].shown = doors[b].shown = true;
    }
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    // only inside the map window (zoomed in, the map runs under the page's header and footer)
    dl->PushClipRect(ImVec2(ox + 64 * u, oy + 116 * u), ImVec2(ox + 1216 * u, oy + 636 * u), true);
    float fs = 22 * u * (sc > 1 ? fminf(sc, 2.0f) : 1.0f);
    for (int k = 0; k < nd; k++) {
        const Door &d = doors[k];
        if (!d.shown) continue;
        float fx, fy;  // (a cell on another floor: not on this page)
        if (cell_shift(lv, d.col, d.row, &fx, &fy) && (fabsf(fx - ptx) > 0.5f || fabsf(fy - pty) > 0.5f)) continue;
        // the doorway: on the cell's edge at its centre (slot in half tiles; a cell is 15 x 12 units), pulled
        // a little into the room so the letter sits on the room's map image
        float cx = -326 + (d.col + 0.5f) * CW, cy = 259 - (d.row - 0.5f) * CH;
        float ax = (d.slot * 0.75f / 15.0f - 0.5f) * CW, az = (d.slot * 0.75f / 12.0f - 0.5f) * CH;
        float lx = cx, ly = cy, in = 11;
        switch (d.side) {
        case 'N': lx = cx + ax; ly = cy + CH / 2 - in; break;
        case 'S': lx = cx + ax; ly = cy - CH / 2 + in; break;
        case 'W': lx = cx - CW / 2 + in; ly = cy - az; break;
        default: lx = cx + CW / 2 - in; ly = cy - az; break;
        }
        ImVec2 p = to_screen(lx, ly);
        char label[4];
        label_text(d.label, label);
        float r = fs * 0.62f;
        dl->AddCircleFilled(p, r, IM_COL32(40, 22, 10, 220));
        dl->AddCircle(p, r, IM_COL32(250, 215, 120, 255), 0, 1.5f * u);
        ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0, label);
        dl->AddText(font, fs, ImVec2(p.x - ts.x * 0.5f, p.y - ts.y * 0.5f), IM_COL32(255, 240, 200, 255), label);
        if (d.to >= 0) {  // one-way: a small tag under the letter, a triangle + the target's letter (no arrow glyph
            char to[4];   // in the game fonts)
            label_text(d.to, to);
            float tfs = fs * 0.7f;
            ImVec2 tt = font->CalcTextSizeA(tfs, FLT_MAX, 0, to);
            float tw = tt.x + tfs * 0.75f, th = tfs;
            ImVec2 a(p.x - tw * 0.5f, p.y + r * 0.85f), b(a.x + tw, a.y + th);
            dl->AddRectFilled(a, b, IM_COL32(90, 30, 20, 230), th * 0.4f);
            float ay = (a.y + b.y) * 0.5f, ax0 = a.x + tfs * 0.18f;
            dl->AddTriangleFilled(ImVec2(ax0, ay - tfs * 0.22f), ImVec2(ax0, ay + tfs * 0.22f),
                                  ImVec2(ax0 + tfs * 0.38f, ay), IM_COL32(255, 200, 120, 255));
            dl->AddText(font, tfs, ImVec2(b.x - tt.x - tfs * 0.12f, ay - tt.y * 0.5f), IM_COL32(255, 240, 200, 255), to);
        }
    }
    dl->PopClipRect();
}
