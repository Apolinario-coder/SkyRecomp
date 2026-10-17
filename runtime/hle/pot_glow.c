// Golden unchecked pots (pots randomizer): pots whose hidden item hasn't been collected are drawn tinted gold.
//
// rando/<seed>/pots.tsv (written by tools/rando/pots.py) lists every item pot: level, x, y, z, flag index.
// The renderer asks pot_tint() for each draw whose shader supports the tint (runtime/nvn/maxwell/translate.c);
// a draw is a pot when its model matrix (vertex UBO 2, 3x4 row-major) is unscaled and sits exactly on a pot
// of the current level. The pot stays gold until its item's GlobalFlags bit is set.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../hle.h"
#include "../platform.h"
#include "../rando.h"

#define PLAYER_STATUS_PTR (MAIN_BASE + 0x1CC1368)  // PlayerStatus*: current level name at +0
#define EVENT_FLAGS (MAIN_BASE + 0x1CBB220 + 0x5DC0)  // live save: GlobalFlags bitfield (2048 bits)

// bushes.tsv rows may add: cut flag, hidden item scale, full item scale, room file, item record offset in it
// (see pot_reveal_tick)
typedef struct { char level[40]; float x, y, z; int flag, cut; float small, full; char room[16]; int rec; } Pot;
static Pot *g_pots;
static int g_npots = -1;
// gold = luminance remapped onto a gold ramp (keeps the pot's shading), plus a slow shimmer
// (colour target is linear HDR: lum ~0.05-0.3 on a pot, so the curve lifts it)
static float g_gold[7] = {1.85f, 1.06f, 0.09f, 1.0f, 0.50f, 0.34f, 0.03f};  // warm gold (tuned 2026-10-08)
void pot_gold_tint(float tint[8]);


static void load(void) {
    g_npots = 0;
    const char *e = getenv("LARECOMP_POT_TINT");  // r,g,b,mix,pulse_r,pulse_g,pulse_b (tuning)
    if (e) sscanf(e, "%f,%f,%f,%f,%f,%f,%f", &g_gold[0], &g_gold[1], &g_gold[2], &g_gold[3], &g_gold[4], &g_gold[5], &g_gold[6]);
    const char *dir = rando_seed_dir();
    if (!dir) return;
    int cap = 0;
    // pots.tsv (potsanity) and bushes.tsv (bushsanity, flags 2048+: runtime/hle/flags_ext.c): same format
    for (int k = 0; k < 2; k++) {
        char path[512];
        snprintf(path, sizeof path, "%s/%s", dir, k ? "bushes.tsv" : "pots.tsv");
        FILE *f = fopen(path, "r");
        if (!f) continue;
        int n0 = g_npots;
        char line[256];
        while (fgets(line, sizeof line, f)) {
            Pot p = {.cut = -1, .rec = -1};
            if (sscanf(line, "%39s %f %f %f %d %d %f %f %15s %d", p.level, &p.x, &p.y, &p.z, &p.flag, &p.cut,
                       &p.small, &p.full, p.room, &p.rec) < 5) continue;
            if (g_npots == cap) g_pots = realloc(g_pots, sizeof(Pot) * (cap = cap ? cap * 2 : 256));
            g_pots[g_npots++] = p;
        }
        fclose(f);
        LOG("[pots] %d gold %s from %s\n", g_npots - n0, k ? "bushes" : "pots", path);
    }
}

static const char *current_level(void) {
    uint64_t ps = LD64(PLAYER_STATUS_PTR);
    return ps ? (const char *)(uintptr_t)ps : "";
}

// Tint: rgb = mix(rgb, t0.rgb * lum^t1.w + t1.rgb * lum, t0.w)  (runtime/nvn/maxwell/translate.c).
// m: the draw's model matrix (3x4 row-major). Fills tint (float4[2]) and returns true for an unchecked pot.
// Pots of the current level, sorted by x (draws binary-search them); rebuilt when the level changes. (The Field has
// ~920 bushes: a fixed 512-entry list left most of them never gold, and a linear scan per draw was slow.)
typedef struct { int pot; float x, y, z; uint64_t seen, mesh; } Track;
static Track g_track[8];  // carried pots (see below)
static int *g_lv_idx, g_nlv = 0;
static char g_lv_name[40] = "\x01";
static uint64_t g_lv_frame = ~0ull;
// Reboot: the next seed's pots.tsv / bushes.tsv.
RT_ON_RESET(pot_glow_reset) {
    free(g_pots), free(g_lv_idx);
    g_pots = NULL, g_lv_idx = NULL;
    g_npots = -1, g_nlv = 0;
    strlcpy(g_lv_name, "\x01", sizeof g_lv_name);
    g_lv_frame = ~0ull;
    memset(g_track, 0, sizeof g_track);
}

static int by_x(const void *a, const void *b) {
    float d = g_pots[*(const int *)a].x - g_pots[*(const int *)b].x;
    return d < 0 ? -1 : d > 0;
}
static void level_pots(void) {
    extern uint64_t renderer_present_count(void);
    uint64_t f = renderer_present_count();
    if (f == g_lv_frame) return;
    g_lv_frame = f;
    const char *lv = current_level();
    if (strncmp(lv, g_lv_name, sizeof g_lv_name)) {
        strlcpy(g_lv_name, lv, sizeof g_lv_name);
        if (!g_lv_idx) g_lv_idx = malloc(sizeof(int) * (size_t)(g_npots ? g_npots : 1));
        g_nlv = 0;
        for (int i = 0; i < g_npots; i++)
            if (!strcmp(g_pots[i].level, lv)) g_lv_idx[g_nlv++] = i;
        qsort(g_lv_idx, (size_t)g_nlv, sizeof(int), by_x);
    }
    if (ENV("LARECOMP_LOG_POTS") && f % 120 == 0) LOG("[pots] frame %llu level '%s': %d item pots\n", (unsigned long long)f, lv, g_nlv);
}

// Cheap per-draw gate for the renderer: false when no item pot can be on screen.
bool pot_glow_active(void) {
    if (g_npots < 0) load();
    if (!g_npots) return false;
    level_pots();
    return g_nlv > 0;
}

// Carried pots: a gold pot that Link lifts leaves its spot, so it's followed from frame to frame: an unscaled
// draw (rotation allowed) within a short step of where a tracked gold pot was drawn in the last couple of frames
// with the same mesh (vertex buffer) is that pot. Pots stand a tile apart, so a step this small can't jump to a
// neighbour, and the mesh check keeps other draws (full-screen passes, Link) out.

bool flags_ext_get(int idx);  // runtime/hle/flags_ext.c (flags 2048+ for bushes)
void flags_ext_clear(int idx);
static bool unchecked(const Pot *p) { return !flags_ext_get(p->flag); }

static void track(int pot, float x, float y, float z, uint64_t f, uint64_t mesh) {
    int slot = 0;
    for (int i = 0; i < 8; i++) {
        if (g_track[i].pot == pot && g_track[i].seen) { slot = i; break; }
        if (g_track[i].seen < g_track[slot].seen) slot = i;
    }
    g_track[slot] = (Track){pot, x, y, z, f, mesh};
}

bool pot_tint(const float *m, float tint[8], uint64_t mesh) {
    if (!pot_glow_active()) return false;
    extern uint64_t renderer_present_count(void);
    uint64_t now = renderer_present_count();
    // unit scale on every axis (the hidden item inside is scaled down)
    float sx = m[0] * m[0] + m[4] * m[4] + m[8] * m[8], sy = m[1] * m[1] + m[5] * m[5] + m[9] * m[9],
          sz = m[2] * m[2] + m[6] * m[6] + m[10] * m[10];
    if (fabsf(sx - 1) > 0.02f || fabsf(sy - 1) > 0.02f || fabsf(sz - 1) > 0.02f) return false;
    bool upright = fabsf(m[0] - 1) <= 0.01f && fabsf(m[5] - 1) <= 0.01f && fabsf(m[10] - 1) <= 0.01f;
    float x = m[3], y = m[7], z = m[11];
    const Pot *hit = NULL;
    int lo = 0, hi = g_nlv;  // first pot with x >= x - 0.02
    while (lo < hi) { int mid = (lo + hi) / 2; if (g_pots[g_lv_idx[mid]].x < x - 0.02f) lo = mid + 1; else hi = mid; }
    for (int k = lo; k < g_nlv && upright && g_pots[g_lv_idx[k]].x <= x + 0.02f; k++) {  // standing on its spot
        const Pot *p = &g_pots[g_lv_idx[k]];
        if (fabsf(p->x - x) > 0.02f || fabsf(p->z - z) > 0.02f || fabsf(p->y - y) > 0.05f) continue;
        if (!unchecked(p)) return false;  // already checked
        hit = p;
        track(g_lv_idx[k], x, y, z, now, mesh);
        break;
    }
    for (int i = 0; i < 8 && !hit; i++) {  // being carried / thrown
        Track *t = &g_track[i];
        if (!t->seen || now - t->seen > 2 || !mesh || t->mesh != mesh || !unchecked(&g_pots[t->pot])) continue;
        if (strcmp(g_pots[t->pot].level, current_level())) { t->seen = 0; continue; }
        float dx = t->x - x, dy = t->y - y, dz = t->z - z;
        if (dx * dx + dy * dy + dz * dz > 0.25f * 0.25f) continue;
        hit = &g_pots[t->pot];
        t->x = x; t->y = y; t->z = z; t->seen = now;
    }
    if (hit) {
        pot_gold_tint(tint);
        return true;
    }
    return false;
}

// Bushsanity: the item inside a bush is small so the leaves hide it, which left it near invisible once the bush was
// cut (players walked past it and took the bush's random rupee instead). A bush with an item has a cut flag as its
// switch 0 (tools/rando/bushes.py: the game sets it when the bush is cut, and keeps such a bush cut); once it's set
// the item is shown full size: the item's actor record in the loaded room file (pos xyz, rotation xyz in degrees,
// scale xyz) gets the full scale, used by every spawn in this level load. Bushes still grow back as in the game: once
// Link is more than a screen away (or in another level) the cut flag is cleared and the record made small again, so
// the bush is there on his next visit - gold until its item is taken, and cutting it shows the same item again.
// Room files (.leb) sit whole at a page-aligned address, so the record is at a page starting with the .leb header +
// the record's offset in the file (bushes.tsv), checked (position, hidden scale) before it's written: a pass over the
// page starts of guest data, done only when the set of such items around Link changes (a bush cut, another screen).
uint64_t doorjump_player(uint64_t frame);                         // runtime/hle/doorjump.c
int kernel_data_regions(uint64_t *addr, uint64_t *size, int max);  // runtime/kernel.c
#define LEB_MAGIC 0x0017013du  // first 4 bytes of every .leb (the next 4 change once the game has loaded it)
static uint64_t g_reveal_sig = ~0ull;
static int g_reveal_cell = -1;
static uint64_t *g_rec;  // per item spot: where its record was patched (re-checked before it's trusted)
RT_ON_RESET(pot_reveal_reset) {
    free(g_rec);
    g_rec = NULL;
    g_reveal_sig = ~0ull;
    g_reveal_cell = -1;
}

static bool rec_is(const Pot *p, uint64_t a, float s) {
    const float *t = (const float *)(uintptr_t)a;
    return a && t[0] == p->x && t[1] == p->y && t[2] == p->z && t[6] == s && t[7] == s && t[8] == s;
}

void pot_reveal_tick(uint64_t frame) {
    if (!pot_glow_active()) return;
    uint64_t pc = doorjump_player(frame);
    if (!pc) return;
    if (!g_rec) g_rec = calloc((size_t)g_npots, sizeof *g_rec);
    const float *lp = (const float *)(uintptr_t)(pc + 0x80);
    float lx = lp[0], lz = lp[2];
    int cx = (int)floorf(lx / 15.0f), cz = (int)floorf(lz / 12.0f), cell = cx * 64 + cz, want[64], nw = 0;
    static char level[40];
    bool new_level = strncmp(current_level(), level, sizeof level) != 0;
    if (new_level || cell != g_reveal_cell) {  // bushes regrow (as in the game): Link left their screen
        strlcpy(level, current_level(), sizeof level);
        for (int i = 0; i < g_npots; i++) {
            Pot *p = &g_pots[i];
            if (p->cut < 0 || !flags_ext_get(p->cut)) continue;
            if (!new_level && !strcmp(p->level, level) && abs((int)floorf(p->x / 15.0f) - cx) <= 1 &&
                abs((int)floorf(p->z / 12.0f) - cz) <= 1) continue;  // (its room may still be loaded: next time)
            flags_ext_clear(p->cut);
            if (rec_is(p, g_rec[i], p->full)) {  // hidden in the bush again
                float *t = (float *)(uintptr_t)g_rec[i];
                t[6] = t[7] = t[8] = p->small;
            }
            g_rec[i] = 0;
        }
    }
    uint64_t sig = 1469598103934665603ull;
    for (int k = 0; k < g_nlv && nw < 64; k++) {  // cut, unchecked items within a screen of Link
        const Pot *p = &g_pots[g_lv_idx[k]];
        if (p->cut < 0 || p->rec < 0 || fabsf(p->x - lx) > 16 || fabsf(p->z - lz) > 13) continue;
        if (!flags_ext_get(p->cut) || !unchecked(p) || rec_is(p, g_rec[g_lv_idx[k]], p->full)) continue;
        want[nw++] = g_lv_idx[k];
        sig = (sig ^ (uint64_t)g_lv_idx[k]) * 1099511628211ull;
    }
    if (cell != g_reveal_cell) g_reveal_sig = ~0ull;  // (another screen: maybe newly loaded room files)
    g_reveal_cell = cell;
    if (!nw || sig == g_reveal_sig) return;
    g_reveal_sig = sig;
    uint64_t ra[64], rs[64], t0 = plat_now_ns();
    int nr = kernel_data_regions(ra, rs, 64), done = 0;
    for (int r = 0; r < nr; r++)
        for (uint64_t a = (ra[r] + 0xFFF) & ~0xFFFull; a + 0x1000 <= ra[r] + rs[r]; a += 0x1000) {
            if (*(const uint32_t *)(uintptr_t)a != LEB_MAGIC) continue;
            for (int j = 0; j < nw; j++) {
                const Pot *p = &g_pots[want[j]];
                uint64_t rec = a + (uint64_t)p->rec;
                if (rec + 36 > ra[r] + rs[r] || !rec_is(p, rec, p->small)) continue;
                float *t = (float *)(uintptr_t)rec;
                t[6] = t[7] = t[8] = p->full;
                g_rec[want[j]] = rec;
                done++;
            }
        }
    LOG("[pots] %d cut bush item(s) around Link: %d shown full size (%.1f ms)\n", nw, done, (plat_now_ns() - t0) / 1e6);
}

// The item that was out already when its bush was cut keeps the scale it spawned with: until the room is loaded again
// its draws are scaled up here instead (m: the draw's model matrix, vertex UBO 2, 3x4 row-major, in guest memory -
// scaled in place, so only a matrix still at the hidden scale is touched: a second pass over the same UBO, e.g. the
// shadow map's, leaves it alone). Rigid item models only; the rest show full size from the next room load on.
void pot_item_draw(float *m) {
    if (!pot_glow_active()) return;
    float s2 = m[0] * m[0] + m[4] * m[4] + m[8] * m[8];
    if (s2 > 0.81f || s2 < 1e-4f) return;  // (hidden items are drawn below 0.9)
    float s = sqrtf(s2), x = m[3], y = m[7], z = m[11];
    int lo = 0, hi = g_nlv;
    while (lo < hi) { int mid = (lo + hi) / 2; if (g_pots[g_lv_idx[mid]].x < x - 0.02f) lo = mid + 1; else hi = mid; }
    for (int k = lo; k < g_nlv && g_pots[g_lv_idx[k]].x <= x + 0.02f; k++) {
        const Pot *p = &g_pots[g_lv_idx[k]];
        if (p->cut < 0 || fabsf(p->z - z) > 0.02f || fabsf(p->y - y) > 1.0f || fabsf(s - p->small) > 0.01f) continue;
        if (!flags_ext_get(p->cut) || !unchecked(p)) return;
        for (int i = 0; i < 11; i++)
            if ((i & 3) != 3) m[i] *= p->full / p->small;
        return;
    }
}

// Instanced draws (bushes: one draw per batch, each instance's 4x4 row-major model matrix in vertex storage buffer
// 0, random rotation about y): true when this instance stands on an unchecked item spot of the current level.
bool pot_instance_gold(const float *m) {
    float sx = m[0] * m[0] + m[4] * m[4] + m[8] * m[8], sy = m[1] * m[1] + m[5] * m[5] + m[9] * m[9];
    if (fabsf(sx - 1) > 0.05f || fabsf(sy - 1) > 0.05f) return false;
    float x = m[3], y = m[7], z = m[11];
    int lo = 0, hi = g_nlv;
    while (lo < hi) { int mid = (lo + hi) / 2; if (g_pots[g_lv_idx[mid]].x < x - 0.02f) lo = mid + 1; else hi = mid; }
    for (int k = lo; k < g_nlv && g_pots[g_lv_idx[k]].x <= x + 0.02f; k++) {
        const Pot *p = &g_pots[g_lv_idx[k]];
        if (fabsf(p->z - z) > 0.02f || fabsf(p->y - y) > 0.05f) continue;
        return unchecked(p);
    }
    return false;
}

// the tint pot_tint gives a gold pot this frame
void pot_gold_tint(float tint[8]) {
    extern uint64_t renderer_present_count(void);
    float t = (float)renderer_present_count() / 60.0f;
    float pulse = 0.5f + 0.5f * sinf(t * 2.5f);
    tint[0] = g_gold[0]; tint[1] = g_gold[1]; tint[2] = g_gold[2]; tint[3] = g_gold[3];
    tint[4] = g_gold[4] * pulse; tint[5] = g_gold[5] * pulse; tint[6] = g_gold[6] * pulse; tint[7] = 0.8f;
}

// True when the current level has item pots: their hidden item actors' 2D twinkle is drawn at a bogus spot
// (it projects a bone the swapped-in item models don't have), so the renderer drops it in these levels.
bool pot_level_active(void) {
    if (g_npots < 0) load();
    if (!g_npots) return false;
    static uint64_t frame = ~0ull;
    static bool active;
    extern uint64_t renderer_present_count(void);
    if (renderer_present_count() == frame) return active;
    frame = renderer_present_count();
    level_pots();
    active = g_nlv > 0;
    return active;
}

// LARECOMP_POT_PROBE=1 (debug): log draws whose model matrix sits within a tile of an item pot / bush of this level
// (whatever their scale / rotation / shader) - finds how a kind of object is drawn.
void pot_probe(const float *m, bool tintable, uint64_t mesh, uint32_t inst) {
    if (!pot_glow_active()) return;
    extern uint64_t renderer_present_count(void);
    static uint64_t frame;
    static int n;
    uint64_t f = renderer_present_count();
    if (f != frame) { frame = f; n = 0; }
    if (f % 300 || n >= 24) return;
    float x = m[3], y = m[7], z = m[11];
    for (int k = 0; k < g_nlv; k++) {
        const Pot *p = &g_pots[g_lv_idx[k]];
        if (fabsf(p->x - x) > 1.0f || fabsf(p->z - z) > 1.0f || fabsf(p->y - y) > 2.0f) continue;
        n++;
        LOG("[potprobe] f%llu pot (%.2f %.2f %.2f) draw (%.2f %.2f %.2f) diag %.3f %.3f %.3f tint %d mesh %llx arg3 %u\n",
            (unsigned long long)f, p->x, p->y, p->z, x, y, z, m[0], m[5], m[10], tintable, (unsigned long long)mesh, inst);
        break;
    }
}
