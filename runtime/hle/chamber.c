// Generated dungeons: load a layout into the game's Chamber Dungeon engine (Dampe's "PanelAdventure") and enter it,
// the way the PanelAdventure event action (main+0x7ee040) does, without going through Dampe.
//
// Chamber manager: global pointer main+0x1cb9fb8 (0x1920-byte object, ctor main+0xcdf1c0). Its layout lives at
// mgr+8: 64 u32 panel ids (MapPieceList mId, row-major 8x8, 0xffffffff = empty); derived values (room count
// +0x700, entrance panel id/cell +0x1578/+0x157c) are cached behind flag bits at layout+0x740. main+0xce34b0(mgr) prepares the run, mgr+0x18fc = 1 marks
// an adventure, main+0xcdbd80(layout) returns the entrance panel's start locator, then a level jump to
// "PanelAdventure" with that locator starts it.
//
// Layout file (tools/rando/chamber_gen.py --out): 64 panel ids (-1 = empty) then the entrance cell index.
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../hle.h"

#define CHAMBER_MGR (MAIN_BASE + 0x1cb9fb8)
#define CHAMBER_CTOR (MAIN_BASE + 0xcdf1c0)
#define CHAMBER_PREPARE (MAIN_BASE + 0xce34b0)
#define CHAMBER_START_LOCATOR (MAIN_BASE + 0xcdbd80)
#define GUEST_NEW (MAIN_BASE + 0x1431170)
#define PANEL_CATALOGUE (MAIN_BASE + 0x1cb9e50)  // MapPieceList table object
#define PANEL_LOOKUP (MAIN_BASE + 0xcd0970)      // (out cell, catalogue, id): out = {sheet, ref, row ptr}

extern int game_in_play(void);
extern void warp_request(const char *level, const char *locator);

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_ids[64];
static int g_entrance = -1;
static volatile int g_pending;
static char g_exit_level[64] = "Field", g_exit_loc[64] = "Field_14D";  // where leaving a generated run goes
static int g_run_active;
// Per-room overrides from the layout file ("door <cell> <N> <W> <E> <S>", "flag <cell> <value>"): applied to the
// MapPieceList catalogue rows in memory (row+0x20 -> int[4] door types in N,W,E,S order; row+0x84 mFlag) when the
// layout loads, restored when the run ends.
typedef struct { int cell, kind; int32_t v[4]; } Override;  // kind 0 = doors, 1 = flag
static Override g_ovr[128];
static int g_novr;
typedef struct { uint64_t addr; int32_t old; } Saved;
static Saved g_saved[512];
static int g_nsaved;  // a generated run is in progress (jumps out of PanelAdventure go to the exit)

RT_ON_RESET(chamber_reset) {  // (reboot: no run in progress; the catalogue rows live in the wiped guest memory)
    g_entrance = -1;
    g_pending = 0;
    g_run_active = 0;
    g_novr = g_nsaved = 0;
    strlcpy(g_exit_level, "Field", sizeof g_exit_level);
    strlcpy(g_exit_loc, "Field_14D", sizeof g_exit_loc);
}

// Load a layout file; returns 0 on success.
int chamber_load_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { LOG("[chamber] no layout %s\n", path); return -1; }
    uint32_t ids[64];
    int n = 0, ent = -1;
    long v;
    while (n < 64 && fscanf(f, "%ld", &v) == 1) ids[n++] = v < 0 ? 0xffffffffu : (uint32_t)v;
    if (fscanf(f, "%d", &ent) != 1) ent = -1;
    char el[64], ec[64];  // optional: "exit <level> <locator>" (the seed's own Tail Cave exit: entrance rando)
    if (fscanf(f, " exit %63s %63s", el, ec) == 2) { strlcpy(g_exit_level, el, sizeof g_exit_level); strlcpy(g_exit_loc, ec, sizeof g_exit_loc); }
    int novr = 0;
    char kw[16];
    while (novr < 128 && fscanf(f, " %15s", kw) == 1) {
        Override o = {0};
        if (!strcmp(kw, "door") && fscanf(f, "%d %d %d %d %d", &o.cell, &o.v[0], &o.v[1], &o.v[2], &o.v[3]) == 5) o.kind = 0;
        else if (!strcmp(kw, "flag") && fscanf(f, "%d %d", &o.cell, &o.v[0]) == 2) o.kind = 1;
        else break;
        if (o.cell >= 0 && o.cell < 64) g_ovr[novr++] = o;
    }
    g_novr = novr;
    fclose(f);
    if (n != 64 || ent < 0 || ent >= 64 || ids[ent] == 0xffffffffu) { LOG("[chamber] bad layout %s (%d ids, entrance %d)\n", path, n, ent); return -1; }
    pthread_mutex_lock(&g_lock);
    memcpy(g_ids, ids, sizeof ids);
    g_entrance = ent;
    g_pending = 1;
    pthread_mutex_unlock(&g_lock);
    LOG("[chamber] queued layout %s (entrance cell %d, panel %u)\n", path, ent, ids[ent]);
    return 0;
}

static uint64_t call3(Cpu *c, uint64_t fn, uint64_t a0, uint64_t a1, uint64_t a2) {
    c->x[0] = a0;
    c->x[1] = a1;
    c->x[2] = a2;
    guest_call(c, fn);
    return c->x[0];
}
static uint64_t call(Cpu *c, uint64_t fn, uint64_t a0) { return call3(c, fn, a0, 0, 0); }

// Load g_ids into the chamber manager and prepare a run; returns the start locator (guest char *), 0 on failure.
// Clobbers guest registers (callers save/restore).
static uint64_t chamber_setup(Cpu *c) {
    uint64_t mgr = LD64(CHAMBER_MGR);
    if (!mgr) {
        mgr = call(c, GUEST_NEW, 0x1920);
        call(c, CHAMBER_CTOR, mgr);
        ST64(CHAMBER_MGR, mgr);
        LOG("[chamber] created manager %llx\n", (unsigned long long)mgr);
    }
    call(c, CHAMBER_PREPARE, mgr);  // (before writing the layout: it resets the manager's state)
    pthread_mutex_lock(&g_lock);
    uint64_t layout = mgr + 8;
    uint64_t cat = LD64(PANEL_CATALOGUE);
    for (int i = 0; i < 64; i++) {
        *(uint32_t *)(uintptr_t)(layout + 4 * i) = g_ids[i];
        // the cells the engine actually reads: layout+0x100 + i*0x18 = {sheet, shared ref, catalogue row}
        uint64_t cell = layout + 0x100 + (uint64_t)i * 0x18;
        if (g_ids[i] != 0xffffffffu && cat) call3(c, PANEL_LOOKUP, cell, cat, g_ids[i]);
        else memset((void *)(uintptr_t)cell, 0, 0x18);  // (drops a reference: a small leak, never freed early)
    }
    // per-room door/flag overrides on the catalogue rows (restored by chamber_restore_rows when the run ends)
    for (int k = 0; k < g_novr; k++) {
        uint64_t row = LD64(layout + 0x100 + (uint64_t)g_ovr[k].cell * 0x18 + 0x10);
        if (!row) continue;
        int n = g_ovr[k].kind == 0 ? 4 : 1;
        uint64_t a = g_ovr[k].kind == 0 ? LD64(row + 0x20) : row + 0x84;
        for (int j = 0; j < n && g_nsaved < 512; j++) {
            g_saved[g_nsaved++] = (Saved){a + 4 * j, *(int32_t *)(uintptr_t)(a + 4 * j)};
            *(int32_t *)(uintptr_t)(a + 4 * j) = g_ovr[k].v[j];
        }
        LOG("[chamber] cell %d override %s:%s %d %d %d %d\n", g_ovr[k].cell, g_ovr[k].kind ? "flag" : "doors",
            n == 4 ? "" : " (1 value)", g_ovr[k].v[0], g_ovr[k].v[1], g_ovr[k].v[2], g_ovr[k].v[3]);
    }
    // layout+0x740: "derived value cached" bits (bit0 room count at +0x700, bit1 entrance at +0x1578); clear them
    // so the engine recomputes from the new ids
    *(uint8_t *)(uintptr_t)(layout + 0x740) = 0;
    g_pending = 0;
    pthread_mutex_unlock(&g_lock);
    // mode bytes + challenge index as a Dampe challenge sets them (seen with challenge 0 loaded): the loader reads
    // the challenge's MapPieceTheme row (time limit, hearts, reward) through +0x15e4; -1 there crashes it
    int m0 = 3, m1 = 3;  // LARECOMP_CHAMBER_MODE=a,b overrides (experiments)
    if (getenv("LARECOMP_CHAMBER_MODE")) sscanf(getenv("LARECOMP_CHAMBER_MODE"), "%d,%d", &m0, &m1);
    *(uint8_t *)(uintptr_t)(mgr + 0x15e0) = (uint8_t)m0;
    *(uint8_t *)(uintptr_t)(mgr + 0x15e1) = (uint8_t)m1;
    *(uint32_t *)(uintptr_t)(mgr + 0x15e4) = 0;
    *(uint8_t *)(uintptr_t)(mgr + 0x18fc) = 1;
    uint64_t loc = call(c, CHAMBER_START_LOCATOR, layout);
    LOG("[chamber] layout ready: entrance id %d cell %d, start locator '%s'\n", *(int32_t *)(uintptr_t)(layout + 0x1578),
        *(int32_t *)(uintptr_t)(layout + 0x157c), loc ? (const char *)(uintptr_t)loc : "");
    return loc;
}

static void chamber_restore_rows(void) {
    while (g_nsaved > 0) { g_nsaved--; *(int32_t *)(uintptr_t)g_saved[g_nsaved].addr = g_saved[g_nsaved].old; }
}

static void set_event_flag(int i) { *(uint8_t *)(uintptr_t)(0x7001CBB220ull + 0x5DC0 + (i >> 3)) |= (uint8_t)(1 << (i & 7)); }

// Chamber-engine prototype: OFF unless a test asks for it (LARECOMP_CHAMBER_TAILCAVE=<layout file>). Generated
// dungeons are being rebuilt as real dungeon levels (notes/PLAN.md); this stays only as reverse-engineering notes.
static const char *layout_path(void) { return getenv("LARECOMP_CHAMBER_TAILCAVE"); }

static void request_set(uint64_t req, const char *level, const char *loc) {
    size_t ll = strlen(level), lc = strlen(loc);
    if (ll > 0x3f || lc > 0x3f) return;
    memcpy((void *)(uintptr_t)(req + 0xa8), level, ll + 1);
    ST64(req + 0xe8, ll);
    memcpy((void *)(uintptr_t)(req + 0xf0), loc, lc + 1);
    ST64(req + 0x130, lc);
}

// Patched into the level-jump request processor's prologue (main+0x81ba80, tools/patches.py): every level change
// passes here with x0 = request (level name +0xa8 / length +0xe8, locator +0xf0 / length +0x130). A jump into
// Tail Cave - from whichever door leads there, so shuffled entrances work - becomes the generated dungeon; the
// jump out of a generated run goes to Tail Cave's own exit (from the layout file) instead of Dampe's shack.
void rt_level_request(Cpu *c) {
    uint64_t req = c->x[0];
    const char *lv = (const char *)(uintptr_t)(req + 0xa8);
    if (ENV("LARECOMP_LOG_LEVELREQ")) LOG("[levelreq] %s / %s\n", lv, (const char *)(uintptr_t)(req + 0xf0));
    if (!g_run_active && layout_path() && !strcmp(lv, "Lv01TailCave") && chamber_load_file(layout_path()) == 0) {
        g_pending = 0;
        uint64_t saved[31], sp = c->sp, pc = c->pc;
        memcpy(saved, c->x, sizeof saved);
        uint64_t loc = chamber_setup(c);
        memcpy(c->x, saved, sizeof saved);
        c->sp = sp;
        c->pc = pc;
        if (loc && *(const char *)(uintptr_t)loc) {
            char l[64];
            strlcpy(l, (const char *)(uintptr_t)loc, sizeof l);
            request_set(req, "PanelAdventure", l);
            for (int i = 470; i <= 472; i++) set_event_flag(i);  // AdvTutorialFirst/Knockdown/Timeover: no Dampe intros
            g_run_active = 1;
            LOG("[chamber] Tail Cave -> generated dungeon (%s)\n", l);
        }
        return;
    }
    if (g_run_active && strcmp(lv, "PanelAdventure") != 0) {
        LOG("[chamber] leaving generated dungeon (%s) -> %s / %s\n", lv, g_exit_level, g_exit_loc);
        request_set(req, g_exit_level, g_exit_loc);
        g_run_active = 0;
        chamber_restore_rows();
    }
}

// Patched over the staircase exit prompt's dialog call (tools/patches.py ADV_EXIT_DIALOG_SITE). In a generated run:
// no dialog, the choice is "leave" (what the dialog's callback would store); the level hook then sends Link to the
// exit. Returns 1 when handled, 0 to show the prompt as usual (Dampe's own chamber dungeons).
int rt_adv_exit_choice(void) {
    if (!g_run_active) return 0;
    uint64_t mgr = LD64(CHAMBER_MGR);
    if (!mgr) return 0;
    *(int32_t *)(uintptr_t)(mgr + 0x18f8) = 0;
    LOG("[chamber] staircase: leaving directly\n");
    return 1;
}

// Game main thread, once per frame (from the controller read).
void chamber_game_tick(Cpu *c, uint64_t frame) {
    static long test_at = -2;  // LARECOMP_CHAMBER_AT=<frame>:<layout file> (test hook)
    if (test_at == -2) {
        test_at = -1;
        const char *e = getenv("LARECOMP_CHAMBER_AT");
        if (e) test_at = atol(e);
    }
    if (test_at >= 0 && (long)frame >= test_at && game_in_play()) {
        test_at = -1;
        const char *e = strchr(getenv("LARECOMP_CHAMBER_AT"), ':');
        if (e) chamber_load_file(e + 1);
    }
    if (!g_pending) return;
    if (!game_in_play()) return;  // keep it queued until a save is loaded
    uint64_t saved[31], sp = c->sp, pc = c->pc;
    memcpy(saved, c->x, sizeof saved);
    uint64_t loc = chamber_setup(c);
    memcpy(c->x, saved, sizeof saved);
    c->sp = sp;
    c->pc = pc;
    const char *ls = loc ? (const char *)(uintptr_t)loc : "";
    if (ls[0]) { g_run_active = 1; warp_request("PanelAdventure", ls); }
}
