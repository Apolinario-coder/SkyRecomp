// Warp: jump Link to any level / spawn point (menu "Warp" tab).
//
// The game's event action GameControl.RequestLevelJump (main+0x7ff160) does not change level itself: it
// fills a pending level-jump request object R through main+0x81b950 -- level name at R+0xa8 (len R+0xe8),
// locator (spawn point, e.g. "Field_11C") at R+0xf0 (len R+0x130), offset vec4 at R+0x140 -- and the game
// performs the transition when it sees the pending request.
//
// R registers itself with its parent while a level loads (`str xR, [x8, #0x1c0]` at main+0x80c464 /
// main+0x80d718); tools/patches.py hooks those stores to tell us where R is. A warp then writes the same
// fields main+0x81b950 writes, from the presenter's per-frame callback.
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../hle.h"

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static char g_level[64], g_locator[64];
static volatile int g_pending;
static volatile uint64_t g_request;

static uint64_t g_req;  // our level-jump request (HLE heap)
static volatile int g_give_pending;
RT_ON_RESET(warp_reset) {  // (reboot: nothing queued; the request objects were in the wiped memory)
    g_pending = 0;
    g_request = 0;
    g_req = 0;
    g_give_pending = 0;
}

// Called from the generated code when a level registers its request object.
void rt_warp_request_obj(uint64_t r) {
    if (r != g_request) LOG("[warp] level-jump request object %llx\n", (unsigned long long)r);
    g_request = r;
}

// Any thread: queue a warp (applied by warp_frame).
// Manbo's Mambo arrival points (start modes OcarinaWarpInWait / OcarinaWarpDungeonIn) throw in the game when warped
// to directly (they expect the song's warp sequence; found 2026-10-09): use a plain start of the same screen
// (dungeons: the entrance room) instead. Points without one are blocked (warp_blocked).
static const char *const kManboRemap[][3] = {
    {"Field", "Warp_TalTalMtWest", "Field_02D"},          {"Field", "Warp_TalTalMtEastEast", "Field_02P_a"},
    {"Field", "Warp_MambogaPond", "Field_05F"},           {"Field", "Warp_MarthasBayWest", "Field_15H"},
    {"Lv01TailCave", "OcarinaWarpPoint", "Lv01TailCave_08D"}, {"Lv02BottleGrotto", "OcarinaWarpPoint", "Lv02BottleGrotto_08C"},
    {"Lv03KeyCavern", "OcarinaWarpPoint", "Lv03KeyCavern_08B"}, {"Lv04AnglersTunnel", "OcarinaWarpPoint", "Lv04AnglersTunnel_08D"},
    {"Lv05CatfishsMaw", "OcarinaWarpPoint", "Lv05CatfishsMaw_08H"}, {"Lv06FaceShrine", "OcarinaWarpPoint", "Lv06FaceShrine_08D"},
    {"Lv07EagleTower", "OcarinaWarpPoint", "Lv07EagleTower_08B_a"}, {"Lv08TurtleRock", "OcarinaWarpPoint", "Lv08TurtleRock_08D_b"},
    {"Lv09WindFishsEgg", "OcarinaWarpPoint", "Lv09WindFishsEgg_08D"}, {"Lv10ClothesDungeon", "OcarinaWarpPoint", "Lv10ClothesDungeon_08C"},
};

void warp_request(const char *level, const char *locator) {
    for (size_t i = 0; i < sizeof kManboRemap / sizeof kManboRemap[0]; i++)
        if (!strcmp(level, kManboRemap[i][0]) && !strcmp(locator, kManboRemap[i][1])) {
            LOG("[warp] %s / %s is a Manbo arrival point: using %s\n", level, locator, kManboRemap[i][2]);
            locator = kManboRemap[i][2];
            break;
        }
    pthread_mutex_lock(&g_lock);
    strlcpy(g_level, level, sizeof g_level);
    strlcpy(g_locator, locator, sizeof g_locator);
    g_pending = 1;
    pthread_mutex_unlock(&g_lock);
    LOG("[warp] queued %s / %s\n", level, locator);
}

// True while a save is loaded and Link exists in a level: the game manager's player slot ([gm + 0x4b8]) is
// null on the boot / title / file-select screens (the level name there is stale), set once gameplay starts.
int g_start_screen;  // runtime/ui/launcher.mm: the start screen is up - before the first boot nothing is loaded here

int game_in_play(void) {
    if (!g_is_las) return 1;
    if (g_start_screen) return 0;
    uint64_t gm = LD64(MAIN_BASE + 0x1c97188);
    return gm && LD64(gm + 0x4b8) != 0;
}

int warp_available(void) { return game_in_play(); }

// Spawn points the warp menu can't use: Manbo's arrival points with no plain start on their screen, and those where
// Link comes up out of deep water (start mode WaterTopOutForward): arriving there without the
// Flippers throws in the game (crash; found by the overworld soak test 2026-10-09). The warp menu and the shader
// tour leave them out until Link has the Flippers (live save +0x556C key items, bit 0).
bool warp_blocked(const char *locator) {
    if (!strcmp(locator, "Warp_DanpeiHouse") || !strcmp(locator, "Warp_ShellHouseEast")) return true;  // (Manbo, no
                                                                                                    // plain start)
    static const char *const kSwim[] = {"Field_03O", "Field_15K", "Field_03K", "EagleTowerExit_01A_b",  // WaterTopOutForward
                                        "FishingBoat_01A", "MamboCave_01A"};  // WaterSideOutLeft / Right (swim in)
    for (size_t i = 0; i < sizeof kSwim / sizeof kSwim[0]; i++)
        if (!strcmp(locator, kSwim[i])) return !(LD32(MAIN_BASE + 0x1CBB220 + 0x556C) & 1);
    return false;
}
int warp_busy(void) { return g_pending; }  // a queued warp not applied yet (shader tour)

// LARECOMP_WARP_AT=<frame>:<level>:<locator>[,<frame>:<level>:<locator>...]  (test hook)
// LARECOMP_WARP_ON_LOAD=<level>:<locator>: warp once, ~1 s after a save is loaded (hand-off to the user)
static void warp_on_load(uint64_t frame) {
    static int done;
    static uint64_t since;
    const char *e = getenv("LARECOMP_WARP_ON_LOAD");
    if (done || !e) return;
    if (!game_in_play()) { since = 0; return; }
    if (!since) since = frame;
    if (frame < since + 60) return;
    char lvl[64], loc[64];
    if (sscanf(e, "%63[^:]:%63s", lvl, loc) == 2) warp_request(lvl, loc);
    done = 1;
}

static void warp_test(uint64_t frame) {
    static const char *next;
    static int init;
    if (!init) { init = 1; next = getenv("LARECOMP_WARP_AT"); }
    long at;
    char lvl[64], loc[64];
    if (!next || !*next || sscanf(next, "%ld:%63[^:]:%63[^,]", &at, lvl, loc) != 3) return;
    if ((long)frame < at) return;
    warp_request(lvl, loc);
    next = strchr(next, ',');
    if (next) next++;
}

#define PROCESS_REQUEST (MAIN_BASE + 0x81ba80)  // performs a pending level-jump request (x0 = request)

// LARECOMP_DUMP_MEM=<frame>:<main offset>:<deref offsets, '+'-separated or ->:<bytes>  e.g. 4400:1cb9fb8:0+8:0x2000
// -> build/memdump.bin (test hook: [[main+off]+0]+8 ...)
static void mem_dump_test(uint64_t frame) {
    static int done;
    const char *e = getenv("LARECOMP_DUMP_MEM");
    if (done || !e) return;
    long at = strtol(e, (char **)&e, 0);
    if ((long)frame < at || *e != ':') return;
    done = 1;
    uint64_t p = MAIN_BASE + strtoull(e + 1, (char **)&e, 16);
    if (e[0] == '!') { p = strtoull(e + 1, (char **)&e, 16); }  // <frame>:0!<absolute hex>:... (heap addresses)
    if (*e == ':') e++;
    while (*e && *e != ':') {
        if (*e == '-') { e++; continue; }
        uint64_t off = strtoull(e, (char **)&e, 16);
        p = LD64(p) + off;
        if (*e == '+') e++;
        if (!p) break;
    }
    size_t n = *e == ':' ? strtoul(e + 1, NULL, 0) : 0x1000;
    FILE *f = fopen("build/memdump.bin", "wb");
    if (f && p) { fwrite((const void *)(uintptr_t)p, 1, n, f); }
    if (f) fclose(f);
    LOG("[memdump] frame %llu addr %llx bytes %zu\n", (unsigned long long)frame, (unsigned long long)p, n);
}

void warp_frame(uint64_t frame) {
    { extern void shader_tour_tick(uint64_t); shader_tour_tick(frame); }
    warp_test(frame);
    warp_on_load(frame);
    mem_dump_test(frame);
    if (ENV("LARECOMP_LOG_WARPAVAIL") && frame % 300 == 0) {
        uint64_t gm = LD64(MAIN_BASE + 0x1c97188), ps = LD64(MAIN_BASE + 0x1cc1368);
        LOG("[warp] frame %llu gm %llx +4b8 %llx +4a8 %llx level '%s'\n", (unsigned long long)frame, (unsigned long long)gm,
            gm ? (unsigned long long)LD64(gm + 0x4b8) : 0, gm ? (unsigned long long)LD64(gm + 0x4a8) : 0,
            ps ? (const char *)(uintptr_t)ps : "");
    }
    if (ENV("LARECOMP_LOG_KEYITEMS") && frame % 120 == 0) {  // PlayerStatus key items vs the save buffer copy
        uint64_t ps = LD64(MAIN_BASE + 0x1cc1368);
        LOG("[keyitems] frame %llu ps %llx ps+ac %08x save+556c %08x\n", (unsigned long long)frame, (unsigned long long)ps,
            ps ? *(uint32_t *)(uintptr_t)(ps + 0xac) : 0, *(uint32_t *)(uintptr_t)(MAIN_BASE + 0x1CBB220ull + 0x556c));
    }
}

// Game main thread, once per frame (from the controller read): if a warp is queued, build a request with
// the fields main+0x81b950 fills and hand it to the game's own request processor.
void warp_game_tick(Cpu *c) {
    if (!g_pending) return;
    if (!game_in_play()) {  // requested from the title screen etc.: drop it (jumping there crashes the game)
        g_pending = 0;
        LOG("[warp] ignored: not in a game\n");
        return;
    }
    if (!g_req) g_req = (uint64_t)(uintptr_t)hle_alloc(0x200);
    uint64_t req = g_req;
    pthread_mutex_lock(&g_lock);
    size_t ll = strlen(g_level), lc = strlen(g_locator);
    memset((void *)(uintptr_t)req, 0, 0x200);
    memcpy((void *)(uintptr_t)(req + 0xa8), g_level, ll + 1);
    ST64(req + 0xe8, ll);
    memcpy((void *)(uintptr_t)(req + 0xf0), g_locator, lc + 1);
    ST64(req + 0x130, lc);
    g_pending = 0;
    pthread_mutex_unlock(&g_lock);
    LOG("[warp] jumping to %s / %s\n", (char *)(uintptr_t)(req + 0xa8), (char *)(uintptr_t)(req + 0xf0));
    uint64_t saved[31], sp = c->sp, pc = c->pc;
    memcpy(saved, c->x, sizeof saved);
    c->x[0] = req;
    guest_call(c, PROCESS_REQUEST);
    memcpy(c->x, saved, sizeof saved);
    c->sp = sp;
    c->pc = pc;
}

// ---- give item (game thread): main+0x7e18b0 = AddItem(itemId, count, index, autoEquip), the call behind the
// Inventory.AddItemByKey event action (itemId = row of datasheets/Items.gsheet, e.g. 78 PanelDungeonPiece).
#define ADD_ITEM (MAIN_BASE + 0x7e18b0)
static int g_give_id, g_give_count, g_give_index;

void give_item_request(int id, int count, int index) {
    g_give_id = id; g_give_count = count; g_give_index = index;
    g_give_pending = 1;
}

// LARECOMP_FIND_FLOATS=<frame>:<f1>,<f2>,... (RE): log every address in writable guest memory holding these floats
// in a row (e.g. a camera zone's box). For the first hit, LARECOMP_FIND_FLOATS_PATH=1 logs pointer paths towards it
// (BFS, "MAIN+" = a global), LARECOMP_FIND_FLOATS_WATCH=1 reports the functions touching it (as LARECOMP_WATCH_ADDR).
static void find_floats_tick(uint64_t frame) {
    static long at = -2;
    static float want[16];
    static int nw;
    if (at == -2) {
        at = -1;
        const char *e = getenv("LARECOMP_FIND_FLOATS");
        if (e) {
            at = strtol(e, (char **)&e, 10);
            while (*e == ':' || *e == ',') { e++; if (nw < 16) want[nw++] = strtof(e, (char **)&e); }
        }
    }
    if (at < 0 || (long)frame != at || !nw) return;
    extern int kernel_data_regions(uint64_t *addr, uint64_t *size, int max);
    uint64_t ra[64], rs[64];
    int nr = kernel_data_regions(ra, rs, 64), n = 0;
    for (int i = 0; i < nr; i++) {
        const float *p = (const float *)(uintptr_t)ra[i];
        for (uint64_t k = 0; k + nw <= rs[i] / 4 && n < 200; k++) {
            int j = 0;
            while (j < nw && p[k + j] == want[j]) j++;
            if (j != nw) continue;
            uint64_t a = ra[i] + k * 4;
            LOG("[findfloats] %llx:", (unsigned long long)a);
            for (int d = -16; d < 32; d++) LOG(" %g", p[k + d]);
            LOG("\n");
            if (!n && ENV("LARECOMP_FIND_FLOATS_PATH")) {  // pointer paths from main's globals to the hit (BFS)
                uint64_t lv[64] = {a}, nx[64];
                int nl = 1;
                for (int depth = 0; depth < 5 && nl; depth++) {
                    int nn = 0;
                    for (int r = 0; r < nr; r++)
                        for (uint64_t q = 0; q + 8 <= rs[r]; q += 8) {
                            uint64_t v = *(volatile uint64_t *)(uintptr_t)(ra[r] + q), loc = ra[r] + q;
                            for (int t = 0; t < nl; t++)
                                if (v <= lv[t] && v >= lv[t] - (depth ? 0x800 : 0x10)) {
                                    bool gl = loc >= MAIN_BASE && loc < MAIN_BASE + 0x2000000;
                                    LOG("[findfloats] path d%d %s%llx -> %llx (+%llx)\n", depth, gl ? "MAIN+" : "",
                                        (unsigned long long)(gl ? loc - MAIN_BASE : loc), (unsigned long long)v,
                                        (unsigned long long)(lv[t] - v));
                                    if (!gl && nn < 64) nx[nn++] = loc;
                                }
                        }
                    memcpy(lv, nx, sizeof(uint64_t) * nn);
                    nl = nn;
                }
            }
            if (!n && ENV("LARECOMP_FIND_FLOATS_WATCH")) {  // watch the first hit +-0x40 (LARECOMP_WATCH_ADDR)
                extern uint64_t g_watch_lo, g_watch_hi;
                extern void watch_arm(void);
                g_watch_lo = a - 0x40, g_watch_hi = a + 0x40;
                watch_arm();
            }
            n++;
        }
    }
    LOG("[findfloats] %d hits\n", n);
    at = -1;
}

// LARECOMP_FIND_MOVE=<A>,<B>,<C> (RE): snapshot writable guest memory at frames A and B; at C list the floats that
// moved steadily (same step per frame A->B and B->C, step at least 1e-3/frame), e.g. Link's position while the
// scripted stick is held. Output: build/find_move.txt (address, values at A/B/C).
static void find_move_tick(uint64_t frame) {
    static long f[3] = {-2};
    static uint8_t *snap[2][64];
    static uint64_t ra[64], rs[64];
    static int nr;
    if (f[0] == -2) {
        f[0] = -1;
        const char *e = getenv("LARECOMP_FIND_MOVE");
        if (e) sscanf(e, "%ld,%ld,%ld", &f[0], &f[1], &f[2]);
    }
    if (f[0] < 0) return;
    int which = (long)frame == f[0] ? 0 : (long)frame == f[1] ? 1 : (long)frame == f[2] ? 2 : -1;
    if (which < 0) return;
    if (which == 0) {
        extern int kernel_data_regions(uint64_t *addr, uint64_t *size, int max);
        nr = kernel_data_regions(ra, rs, 64);
        uint64_t total = 0;
        for (int i = 0; i < nr; i++) {
            LOG("[findmove] region %llx size %llx\n", (unsigned long long)ra[i], (unsigned long long)rs[i]);
            if (total + rs[i] > (6ull << 30)) { rs[i] = 0; continue; }
            total += rs[i];
        }
        LOG("[findmove] %d regions, %llu MB\n", nr, (unsigned long long)(total >> 20));
    }
    if (which < 2) {
        for (int i = 0; i < nr; i++) {
            if (!rs[i]) continue;
            if (!snap[which][i]) snap[which][i] = malloc(rs[i]);
            memcpy(snap[which][i], (const void *)(uintptr_t)ra[i], rs[i]);
        }
        return;
    }
    FILE *o = fopen("build/find_move.txt", "w");
    int n = 0;
    float fm_tol = ENV("LARECOMP_FIND_MOVE_TOL") ? (float)atof(ENV("LARECOMP_FIND_MOVE_TOL")) : 0.05f;
    float dab = (float)(f[1] - f[0]), dbc = (float)(f[2] - f[1]);
    for (int i = 0; i < nr; i++) {
        if (!rs[i] || !snap[0][i] || !snap[1][i]) continue;
        const float *a = (const float *)snap[0][i], *b = (const float *)snap[1][i];
        const float *c = (const float *)(uintptr_t)ra[i];
        for (uint64_t k = 0; k < rs[i] / 4; k++) {
            float va = a[k], vb = b[k], vc = c[k];
            if (!(va == va) || !(vb == vb) || !(vc == vc) || fabsf(va) > 1e5f || fabsf(vc) > 1e5f) continue;
            float s1 = (vb - va) / dab, s2 = (vc - vb) / dbc;
            if (fabsf(s1) < 1e-3f || fabsf(s1 - s2) > fabsf(s1) * fm_tol) continue;
            if (o && n < 20000) fprintf(o, "%llx %g %g %g step %g\n", (unsigned long long)(ra[i] + k * 4), va, vb, vc, s1);
            n++;
        }
    }
    for (int i = 0; i < nr; i++) {  // int32 (marked 'i': fixed-point positions)
        if (!rs[i] || !snap[0][i] || !snap[1][i]) continue;
        const int32_t *a = (const int32_t *)snap[0][i], *b = (const int32_t *)snap[1][i];
        const int32_t *c = (const int32_t *)(uintptr_t)ra[i];
        for (uint64_t k = 0; k < rs[i] / 4; k++) {
            int32_t va = a[k], vb = b[k], vc = c[k];
            if (va == vb || abs(va) > 50000000 || abs(vc) > 50000000) continue;
            double s1 = (vb - va) / (double)dab, s2 = (vc - vb) / (double)dbc;
            if (fabs(s1) < 2 || fabs(s1 - s2) > fabs(s1) * fm_tol) continue;
            if (o && n < 20000) fprintf(o, "%llx %d %d %d step %g i\n", (unsigned long long)(ra[i] + k * 4), va, vb, vc, s1);
            n++;
        }
    }
    for (int i = 0; i < nr; i++) {  // doubles too (marked 'd')
        if (!rs[i] || !snap[0][i] || !snap[1][i]) continue;
        const double *a = (const double *)snap[0][i], *b = (const double *)snap[1][i];
        const double *c = (const double *)(uintptr_t)ra[i];
        for (uint64_t k = 0; k < rs[i] / 8; k++) {
            double va = a[k], vb = b[k], vc = c[k];
            if (!(va == va) || !(vb == vb) || !(vc == vc) || fabs(va) > 1e5 || fabs(vc) > 1e5) continue;
            double s1 = (vb - va) / dab, s2 = (vc - vb) / dbc;
            if (fabs(s1) < 1e-3 || fabs(s1 - s2) > fabs(s1) * fm_tol) continue;
            if (o && n < 20000) fprintf(o, "%llx %g %g %g step %g d\n", (unsigned long long)(ra[i] + k * 8), va, vb, vc, s1);
            n++;
        }
    }
    if (o) fclose(o);
    LOG("[findmove] %d steady floats -> build/find_move.txt\n", n);
}

// LARECOMP_ADDF=<frame>:<delta>:<file>[:<lo>:<hi>[:<first>:<count>]] (RE): at <frame> add <delta> to the values at
// the addresses listed in <file> (hex address first on each line - build/find_move.txt works; lines ending in ' d'
// are doubles), only those currently in [lo, hi], and of those only entries first..first+count-1 (bisecting);
// all are logged again 30 frames later: Link's real position keeps the offset, mirrors snap back.
static double g_addf_ref;
static void addf_tick(uint64_t frame) {
    static long at = -2;
    static float delta, lo = -1e30f, hi = 1e30f;
    static int first = 0, count = 1 << 30;
    static char path[512];
    static uint64_t addr[4096];
    static char dbl[4096];
    static int n;
    if (at == -2) {
        at = -1;
        const char *e = getenv("LARECOMP_ADDF");
        if (e) {
            char rest[512] = "";
            if (sscanf(e, "%ld:%f:%511[^:]:%511s", &at, &delta, path, rest) < 3) at = -1;
            else if (rest[0]) sscanf(rest, "%f:%f:%d:%d", &lo, &hi, &first, &count);
        }
    }
    if (at < 0) return;
    if ((long)frame == at) {
        FILE *f = fopen(path, "r");
        char line[256];
        int k = 0;
        while (f && n < 4096 && fgets(line, sizeof line, f)) {
            unsigned long long a;
            if (sscanf(line, "%llx", &a) != 1) continue;
            int d = strstr(line, " d\n") != NULL;
            double v = d ? *(volatile double *)(uintptr_t)a : *(volatile float *)(uintptr_t)a;
            if (!(v >= lo && v <= hi)) continue;
            if (k++ < first || k > first + count) continue;
            addr[n] = a; dbl[n] = (char)d; n++;
        }
        if (f) fclose(f);
        // LARECOMP_ADDF_MATCH=1: take the first listed value as the reference and poke EVERY float / double in guest
        // data memory within 0.001 of it instead (finds the real copy even if it doesn't move steadily)
        if (getenv("LARECOMP_ADDF_MATCH") && (n > 0 || strcmp(getenv("LARECOMP_ADDF_MATCH"), "1"))) {
            double ref = dbl[0] ? *(volatile double *)(uintptr_t)addr[0] : *(volatile float *)(uintptr_t)addr[0];
            if (!strcmp(getenv("LARECOMP_ADDF_MATCH"), "mode")) {  // most common current value of the listed copies
                int best = 0;
                for (int i = 0; i < n; i++) {
                    double vi = dbl[i] ? *(volatile double *)(uintptr_t)addr[i] : *(volatile float *)(uintptr_t)addr[i];
                    int c = 0;
                    for (int j = 0; j < n; j++) {
                        double vj = dbl[j] ? *(volatile double *)(uintptr_t)addr[j] : *(volatile float *)(uintptr_t)addr[j];
                        c += fabs(vi - vj) < 1e-4;
                    }
                    if (c > best) { best = c; ref = vi; }
                }
            } else if (strcmp(getenv("LARECOMP_ADDF_MATCH"), "1")) ref = atof(getenv("LARECOMP_ADDF_MATCH"));
            extern int kernel_data_regions(uint64_t *addr, uint64_t *size, int max);
            uint64_t ra[64], rs[64];
            int nr = kernel_data_regions(ra, rs, 64);
            n = 0;
            for (int r = 0; r < nr && n < 4096; r++) {
                for (uint64_t k = 0; k + 4 <= rs[r] && n < 4096; k += 4) {
                    float v = *(volatile float *)(uintptr_t)(ra[r] + k);
                    if (fabs(v - ref) < 1e-3) { addr[n] = ra[r] + k; dbl[n] = 0; n++; }
                }
                for (uint64_t k = 0; k + 8 <= rs[r] && n < 4096; k += 8) {
                    double v = *(volatile double *)(uintptr_t)(ra[r] + k);
                    if (fabs(v - ref) < 1e-3) { addr[n] = ra[r] + k; dbl[n] = 1; n++; }
                }
            }
            LOG("[addf] match %g: %d values\n", ref, n);
            g_addf_ref = ref;
            if (first || count < (1 << 30)) {  // slice of the matches (bisecting)
                int m = 0;
                for (int i = first; i < n && i < first + count; i++) { addr[m] = addr[i]; dbl[m] = dbl[i]; m++; }
                n = m;
            }
        }
        for (int i = 0; i < n; i++) {
            if (dbl[i]) {
                volatile double *p = (volatile double *)(uintptr_t)addr[i];
                LOG("[addf] %llx d %g -> %g\n", (unsigned long long)addr[i], *p, *p + delta);
                *p += delta;
            } else {
                volatile float *p = (volatile float *)(uintptr_t)addr[i];
                LOG("[addf] %llx %g -> %g\n", (unsigned long long)addr[i], *p, *p + delta);
                *p += delta;
            }
        }
    } else if ((long)frame == at + 30) {
        if (getenv("LARECOMP_ADDF_MATCH")) {  // did the unpoked copies follow? count values at old / new position
            extern int kernel_data_regions(uint64_t *addr, uint64_t *size, int max);
            uint64_t ra[64], rs[64];
            int nr = kernel_data_regions(ra, rs, 64), c_old = 0, c_new = 0;
            double ref = g_addf_ref;
            for (int r = 0; r < nr; r++)
                for (uint64_t k = 0; k + 4 <= rs[r]; k += 4) {
                    float v = *(volatile float *)(uintptr_t)(ra[r] + k);
                    c_old += fabs(v - ref) < 1e-3;
                    c_new += fabs(v - (ref + delta)) < 1e-3;
                }
            LOG("[addf+30] floats at old %g: %d, at new %g: %d\n", ref, c_old, ref + delta, c_new);
        }
        for (int i = 0; i < n; i++)
            LOG("[addf+30] %llx %g\n", (unsigned long long)addr[i],
                dbl[i] ? *(volatile double *)(uintptr_t)addr[i] : *(volatile float *)(uintptr_t)addr[i]);
        at = -1;
    }
}

// LARECOMP_PTRSCAN=<frame>:<hex addr>:<hex range> (RE): log every 8-byte value in guest data memory pointing into
// [addr - range, addr] (which objects hold the object that contains addr), and where it is
static void ptrscan_tick(uint64_t frame) {
    static long at = -2;
    static unsigned long long target, range;
    if (at == -2) {
        at = -1;
        const char *e = getenv("LARECOMP_PTRSCAN");
        if (e && sscanf(e, "%ld:%llx:%llx", &at, &target, &range) != 3) at = -1;
    }
    if (at < 0 || (long)frame != at) return;
    extern int kernel_data_regions(uint64_t *addr, uint64_t *size, int max);
    uint64_t ra[64], rs[64];
    int nr = kernel_data_regions(ra, rs, 64), n = 0;
    for (int r = 0; r < nr; r++)
        for (uint64_t k = 0; k + 8 <= rs[r] && n < 2000; k += 8) {
            uint64_t v = *(volatile uint64_t *)(uintptr_t)(ra[r] + k);
            if (v <= target && v >= target - range) {
                LOG("[ptrscan] %llx -> %llx (target-%llx)\n", (unsigned long long)(ra[r] + k),
                    (unsigned long long)v, (unsigned long long)(target - v));
                n++;
            }
        }
    LOG("[ptrscan] %d pointers\n", n);
}

// LARECOMP_MEMDUMP=<frame>:<hex addr>:<hex len>[,...] (RE): hex dump guest memory to the log
static void memdump_tick(uint64_t frame) {
    static char spec[512];
    static int init;
    if (!init) { init = 1; const char *e = getenv("LARECOMP_MEMDUMP"); if (e) strlcpy(spec, e, sizeof spec); }
    for (char *p = spec; *p;) {
        long f; unsigned long long a, l;
        if (sscanf(p, "%ld:%llx:%llx", &f, &a, &l) == 3 && (long)frame == f)
            for (unsigned long long o = 0; o < l; o += 16) {
                const uint32_t *w = (const uint32_t *)(uintptr_t)(a + o);
                LOG("[memdump] %llx: %08x %08x %08x %08x\n", a + o, w[0], w[1], w[2], w[3]);
            }
        p = strchr(p, ',');
        if (!p) break;
        p++;
    }
}

// LARECOMP_PANE_SCAN=<frame>:<hex addr>[:<depth>]: log UI panes reachable from an object (addr 0 = LARECOMP_FNPROF_BT's
// x0, 1 = the dungeon map page) (RE: pane name at +0xB0,
// translate +0x30/+0x34, scale +0x48/+0x4C), following pointers up to <depth> levels (default 2)
static void pane_scan_rec(uint64_t obj, int depth, const char *path, uint64_t *seen, int *nseen) {
    extern int kernel_data_regions(uint64_t *a, uint64_t *s, int max);
    static uint64_t ra[256], rs[256];
    static int nr = -1;
    if (nr < 0) nr = kernel_data_regions(ra, rs, 256);
    for (uint32_t off = 0; off < 0x800; off += 8) {
        uint64_t p = LD64(obj + off);
        bool ok = false;
        for (int i = 0; i < nr && !ok; i++) ok = p >= ra[i] && p + 0x800 > p && p + 0x800 <= ra[i] + rs[i];  // (-1: wraps)
        if (!ok) continue;
        bool dup = false;
        for (int i = 0; i < *nseen && !dup; i++) dup = seen[i] == p;
        if (dup || *nseen >= 4096) continue;
        seen[(*nseen)++] = p;
        const char *nm = (const char *)(uintptr_t)(p + 0xB0);
        int len = 0;
        while (len < 24 && ((nm[len] >= 'A' && nm[len] <= 'Z') || (nm[len] >= 'a' && nm[len] <= 'z') ||
                            (nm[len] >= '0' && nm[len] <= '9') || nm[len] == '_')) len++;
        if (len >= 3 && nm[len] == 0 && nm[1] == '_') {
            const float *f = (const float *)(uintptr_t)p;
            LOG("[pane] %s+%#x -> %llx '%s' t(%.1f, %.1f) s(%.3f, %.3f) size(%.1f, %.1f)\n", path, off, (unsigned long long)p, nm,
                f[0x30 / 4], f[0x34 / 4], f[0x48 / 4], f[0x4C / 4], f[0x50 / 4], f[0x54 / 4]);
        }
        if (depth > 0) {
            char sub[256];
            snprintf(sub, sizeof sub, "%s+%#x", path, off);
            pane_scan_rec(p, depth - 1, sub, seen, nseen);
        }
    }
}

static void pane_scan_tick(uint64_t frame) {
    static long at = -2;
    static uint64_t addr;
    static int depth = 2;
    if (at == -2) {
        at = -1;
        const char *e = getenv("LARECOMP_PANE_SCAN");
        if (e && sscanf(e, "%ld:%llx:%d", &at, (unsigned long long *)&addr, &depth) < 2) at = -1;
    }
    if (at < 0 || (long)frame != at) return;
    extern uint64_t g_bt_first_x0;  // fnprof.c: x0 of the first LARECOMP_FNPROF_BT function hit (addr 0 = use it)
    if (!addr) addr = g_bt_first_x0;
    if (addr == 1) { extern uint64_t chaos_dgnmap_obj(void); addr = chaos_dgnmap_obj(); }  // 1 = the dungeon map page
    LOG("[pane] scanning from %llx\n", (unsigned long long)addr);
    if (!addr) return;
    static uint64_t seen[4096];
    int nseen = 0;
    pane_scan_rec(addr, depth, "obj", seen, &nseen);
    LOG("[pane] scan done (%d objects)\n", nseen);
}

// LARECOMP_ACTOR_SCAN=<frame>:<vtable main offset hex>[:<off>=<hex byte>,...] (test): from that frame (every 60 frames,
// 10 times) log every object whose first word is that vtable (main + offset) with a dump of its first 0x400 bytes in
// 16-byte rows that aren't all zero; pokes write bytes into each once (after the first log). Underwater items research.
static void actor_scan_tick(uint64_t frame) {
    static long at = -2, vt;
    static int left = 10, poked;
    static char pokes[256];
    if (at == -2) {
        at = -1;
        const char *e = getenv("LARECOMP_ACTOR_SCAN");
        if (e && sscanf(e, "%ld:%lx", &at, &vt) == 2) {
            const char *p = strchr(strchr(e, ':') + 1, ':');
            if (p) strlcpy(pokes, p + 1, sizeof pokes);
        } else at = -1;
    }
    if (at < 0 || (long)frame < at || !left || (frame - at) % 60) return;
    left--;
    extern int kernel_data_regions(uint64_t *addr, uint64_t *size, int max);
    uint64_t ra[64], rs[64];
    int nr = kernel_data_regions(ra, rs, 64), n = 0;
    const char *byname = getenv("LARECOMP_ACTOR_NAME");  // (instead: objects whose name at +0x398 starts with this)
    size_t bl = byname ? strlen(byname) : 0;
    for (int r = 0; r < nr; r++)
        for (uint64_t k = 0; k + 0x400 <= rs[r]; k += byname ? 4 : 8) {
            uint64_t o = ra[r] + k;
            static long name_at = -1;  // LARECOMP_ACTOR_NAME_AT=<hex>: where the name sits (default 0x398; -1 anywhere)
            if (name_at == -1) name_at = getenv("LARECOMP_ACTOR_NAME_AT") ? strtol(getenv("LARECOMP_ACTOR_NAME_AT"), NULL, 16) : 0x398;
            if (byname ? memcmp((const void *)(uintptr_t)(o + (name_at >= 0 ? name_at : 0)), byname, bl) != 0
                       : LD64(o) != MAIN_BASE + (uint64_t)vt)
                continue;
            if (byname) LOG("[actorscan] by name: vtable main+%llx\n", (unsigned long long)(LD64(o) - MAIN_BASE));
            n++;
            LOG("[actorscan] f%llu obj %llx\n", (unsigned long long)frame, (unsigned long long)o);
            int back = getenv("LARECOMP_ACTOR_BACK") ? (int)strtol(getenv("LARECOMP_ACTOR_BACK"), NULL, 0) : 0;
            for (int q = -back; q < 0x400; q += 16) {
                const uint8_t *b = (const uint8_t *)(uintptr_t)(o + q);
                int z = 1;
                for (int i = 0; i < 16; i++) z &= !b[i];
                if (z) continue;
                char line[80];
                int m = 0;
                for (int i = 0; i < 16; i++) m += snprintf(line + m, sizeof line - m, "%02x%s", b[i], i % 4 == 3 ? " " : "");
                LOG("[actorscan]   %c%03x %s\n", q < 0 ? '-' : '+', q < 0 ? -q : q, line);
            }
            if (pokes[0] && !poked) {
                for (const char *p = pokes; *p;) {
                    unsigned off, val;
                    if (sscanf(p, "%x=%x", &off, &val) == 2 && off < 0x1200) {
                        *(volatile uint8_t *)(uintptr_t)(o + off) = (uint8_t)val;
                        LOG("[actorscan]   poked +%x = %#x\n", off, val);
                    }
                    while (*p && *p != ',') p++;
                    if (*p) p++;
                }
            }
        }
    if (pokes[0] && n) poked = 1;
    LOG("[actorscan] f%llu %d objects with vtable main+%lx\n", (unsigned long long)frame, n, vt);
}

void give_item_tick(Cpu *c, uint64_t frame) {
    actor_scan_tick(frame);
    { extern void miniboss_tick(Cpu *, uint64_t); miniboss_tick(c, frame); }
    { extern void escape_tick(Cpu *, uint64_t); escape_tick(c, frame); }
    { extern void rescue_tick(uint64_t); rescue_tick(frame); }  // (softlock rescues: runtime/hle/rescue.c)
    pane_scan_tick(frame);
    { extern void doorjump_tick(uint64_t frame); doorjump_tick(frame); }
    { extern void pot_reveal_tick(uint64_t frame); pot_reveal_tick(frame); }  // (after doorjump: Link found)
    { extern void souls_tick(uint64_t frame); souls_tick(frame); }  // (boss / mini-boss souls: runtime/hle/souls.c)
    { extern void hookshot_tour_tick(uint64_t frame); hookshot_tour_tick(frame); }
    { extern void flags_ext_tick(void); flags_ext_tick(); }  // (game thread: flag gets / sets happen here too)
    ptrscan_tick(frame);
    memdump_tick(frame);
    find_move_tick(frame);
    find_floats_tick(frame);
    addf_tick(frame);
    {  // LARECOMP_WATCH_ADDR=<frame>:<hex lo>:<hex hi>: report host functions writing [lo,hi) (RE; one 16K page)
        static long wat = -2;
        static unsigned long long wlo, whi;
        if (wat == -2) {
            wat = -1;
            const char *e = getenv("LARECOMP_WATCH_ADDR");
            if (e && sscanf(e, "%ld:%llx:%llx", &wat, &wlo, &whi) != 3) wat = -1;
        }
        if (wat >= 0 && (long)frame >= wat) {
            extern uint64_t g_watch_lo, g_watch_hi;
            extern void watch_arm(void);
            g_watch_lo = wlo;
            g_watch_hi = whi;
            watch_arm();
            LOG("[watch] armed %llx-%llx\n", wlo, whi);
            wat = -1;
        }
    }
    static long at = -2;  // LARECOMP_GIVE_ITEM=<frame>:<id>:<count>:<index>[:<auto-equip 1>][,...] (test hook)
    static char spec[256];
    if (at == -2) {
        at = -1;
        const char *e = getenv("LARECOMP_GIVE_ITEM");
        if (e) { strlcpy(spec, e, sizeof spec); at = atol(e); }
    }
    if (at >= 0 && (long)frame >= at && game_in_play()) {
        for (char *p = spec; p && *p;) {
            long f; int id, cnt, idx, equip = 0;
            if (sscanf(p, "%ld:%d:%d:%d:%d", &f, &id, &cnt, &idx, &equip) >= 4) {
                uint64_t saved[31];
                memcpy(saved, c->x, sizeof saved);
                c->x[0] = (uint64_t)id; c->x[1] = (uint64_t)cnt; c->x[2] = (uint64_t)(int64_t)idx; c->x[3] = (uint64_t)equip;
                guest_call(c, ADD_ITEM);
                memcpy(c->x, saved, sizeof saved);
                LOG("[give] item %d x%d (index %d)\n", id, cnt, idx);
            }
            p = strchr(p, ',');
            if (p) p++;
        }
        at = -1;
    }
    static long poke_at = -2;  // LARECOMP_POKE=<frame>:<hex addr>:<hex u32 OR mask>[,...] (test hook: set save bits)
    static char poke[256];
    if (poke_at == -2) {
        poke_at = -1;
        const char *e = getenv("LARECOMP_POKE");
        if (e) { strlcpy(poke, e, sizeof poke); poke_at = atol(e); }
    }
    if (poke_at >= 0 && (long)frame >= poke_at && game_in_play()) {
        for (char *p = poke; p && *p;) {
            long f; unsigned long long a; unsigned v;
            if (sscanf(p, "%ld:%llx:%x", &f, &a, &v) == 3) {
                *(volatile uint32_t *)(uintptr_t)a |= v;
                LOG("[poke] %llx |= %#x -> %#x\n", a, v, *(volatile uint32_t *)(uintptr_t)a);
            }
            p = strchr(p, ',');
            if (p) p++;
        }
        poke_at = -1;
    }
    if (g_give_pending && game_in_play()) {
        g_give_pending = 0;
        uint64_t saved[31];
        memcpy(saved, c->x, sizeof saved);
        c->x[0] = (uint64_t)g_give_id; c->x[1] = (uint64_t)g_give_count; c->x[2] = (uint64_t)(int64_t)g_give_index; c->x[3] = 0;
        guest_call(c, ADD_ITEM);
        memcpy(c->x, saved, sizeof saved);
        LOG("[give] item %d x%d (index %d)\n", g_give_id, g_give_count, g_give_index);
    }
}

// Debug: who calls vector::at() out of range? Log the guest return address and frame chain, then stop.
HLE_FUNC("_ZNKSt3__120__vector_base_commonILb1EE20__throw_out_of_rangeEv", rt_vector_oor) {
    LOG("[oor] vector::at out of range: lr %llx\n", (unsigned long long)c->x[30]);
    uint64_t fp = c->x[29];
    for (int i = 0; i < 12 && fp; i++) {
        uint64_t next = *(uint64_t *)(uintptr_t)fp, ret = *(uint64_t *)(uintptr_t)(fp + 8);
        LOG("[oor]   frame %d ret %llx\n", i, (unsigned long long)ret);
        fp = next;
    }
    LOG("[oor] regs x0 %llx x1 %llx x2 %llx x8 %llx x9 %llx x19 %llx x20 %llx x21 %llx\n", (unsigned long long)c->x[0],
        (unsigned long long)c->x[1], (unsigned long long)c->x[2], (unsigned long long)c->x[8], (unsigned long long)c->x[9],
        (unsigned long long)c->x[19], (unsigned long long)c->x[20], (unsigned long long)c->x[21]);
    FATAL("vector::at out of range (see [oor] lines)\n");
}

void rt_log_resname(uint64_t p) {
    static int on = -1;
    if (on < 0) on = getenv("LARECOMP_LOG_RESNAME") != NULL;
    if (!on || !p) return;
    const char *s = (const char *)(uintptr_t)*(uint64_t *)(uintptr_t)p;
    if (s) LOG("[resname] %.120s\n", s);
}
