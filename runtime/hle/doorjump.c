// Generated dungeons: doorways that don't line up across a room edge (Face Shrine 05A's one-tile opening next to
// Catfish's Maw 07E's two-tile one, half a tile apart). Walls are part of the room models, so Link stops in his
// doorway against the neighbour's wall. When he walks into such a doorway, he is moved just past the room edge in
// front of the partner doorway; the game sees him in the next room and plays its normal scroll.
//
// rando/<seed>/door_jumps.tsv (tools/rando/dungeon_gen.py): level, cell (e.g. 08C), side (N/S/W/E), doorway centre
// on that side and the partner's centre on the facing side, in half tiles (x 0.75 units) from the cell's corner.
// Optional (Chaos dungeon room shuffle): target cell and target side - Link comes out of that room's doorway on that
// side (any room of the level); without them, the neighbour across the edge, facing side.
//
// Link's position: the "PlayerCollision" component (vtable main+0x1c501f0, name inline at +0x18), vec3 at +0x80
// (a second copy at +0xb0); writing it moves Link, every other copy follows (found 2026-10-08 with
// LARECOMP_ADDF_MATCH bisection, notes/PLAN.md). It's re-found by a heap scan after each level load.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../hle.h"
#include "../rando.h"

#define PLAYER_STATUS_PTR (MAIN_BASE + 0x1CC1368)  // PlayerStatus*: current level name at +0
#define PLAYER_COLLISION_VT (MAIN_BASE + 0x1c501f0)

typedef struct { char level[40]; int col, row; char side; int own, partner, still; int tcol, trow; char tside; float depth;
                 int pair; float height; } Jump;  // height: floor where Link lands (Chaos; <-90 = keep his)  // pair: Chaos doorway pair (both directions share it), -1 = a neighbour jump
// Chaos dungeon map letters (runtime/ui/chaos_overlay.mm): a pair's letter shows once Link has walked through either
// end - remembered in event flag CHAOS_FLAG_BASE + pair (extended flags, runtime/hle/flags_ext.c: saved per slot)
#define CHAOS_FLAG_BASE 3800
typedef struct { char level[40]; int col[2], row[2]; char side[2]; int slot[2]; bool two; } ChaosPair;  // two: both ways
// (Madness: rows are one-way, each unmatched row is its own pair; 3800-4095 = at most 296 pairs per seed)
static ChaosPair g_pairs[296];
static int g_npairs;
static Jump *g_jumps;
static int g_njumps = -1;

static void load(void) {
    g_njumps = 0;
    const char *dir = rando_seed_dir();
    if (!dir) return;
    char path[512];
    snprintf(path, sizeof path, "%s/door_jumps.tsv", dir);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[256];
    int cap = 0;
    while (fgets(line, sizeof line, f)) {
        Jump j;
        char cell[8], side[4], tcell[8], tside[4];
        j.depth = 0.9f;  // how far past the target edge Link lands (Chaos: further when the target has its own door)
        j.height = -100.0f;
        int n = sscanf(line, "%39s %7s %3s %d %d %7s %3s %f %f", j.level, cell, side, &j.own, &j.partner, tcell, tside,
                       &j.depth, &j.height);
        if (n != 5 && n != 7 && n != 8 && n != 9) continue;
        j.row = atoi(cell);  // "08C" -> row 8, column C
        j.col = cell[2] - 'A';
        j.side = side[0];
        j.pair = -1;
        if (n >= 7) {
            j.trow = atoi(tcell), j.tcol = tcell[2] - 'A', j.tside = tside[0];
            // its pair: the reverse row's, if seen already
            for (int k = 0; k < g_npairs && j.pair < 0; k++) {
                const ChaosPair *q = &g_pairs[k];
                if (!strcmp(q->level, j.level) && q->col[1] == j.col && q->row[1] == j.row && q->side[1] == j.side &&
                    q->slot[1] == j.own && q->col[0] == j.tcol && q->row[0] == j.trow && q->side[0] == j.tside)
                    j.pair = k, g_pairs[k].two = true;
            }
            bool self = j.trow == j.row && j.tcol == j.col && j.tside == j.side && j.partner == j.own;
            if (j.pair < 0 && g_npairs < 296 && !self) {  // (a doorway leading back into itself is a wall: no letter)
                ChaosPair *q = &g_pairs[g_npairs];
                strlcpy(q->level, j.level, sizeof q->level);
                q->col[0] = j.col, q->row[0] = j.row, q->side[0] = j.side, q->slot[0] = j.own;
                q->col[1] = j.tcol, q->row[1] = j.trow, q->side[1] = j.tside, q->slot[1] = j.partner;
                q->two = false;
                j.pair = g_npairs++;
            }
        } else {  // the neighbour across the edge, its facing side
            j.trow = j.row + (j.side == 'N' ? -1 : j.side == 'S' ? 1 : 0);
            j.tcol = j.col + (j.side == 'W' ? -1 : j.side == 'E' ? 1 : 0);
            j.tside = j.side == 'N' ? 'S' : j.side == 'S' ? 'N' : j.side == 'W' ? 'E' : 'W';
        }
        j.still = 0;
        if (g_njumps == cap) g_jumps = realloc(g_jumps, sizeof(Jump) * (cap = cap ? cap * 2 : 64));
        g_jumps[g_njumps++] = j;
    }
    fclose(f);
    LOG("[doorjump] %d mismatched doorways from %s\n", g_njumps, path);
}

static uint64_t g_player;          // find_player: the PlayerCollision component found last
static uint64_t g_player_scan;     // frame of the last heap scan
static char g_player_level[64];    // level it was found in
static int g_player_rescan;        // (a warp within the same level: the old player can't be told apart either)
extern uint64_t g_stick_freeze_until;  // (below)
static uint64_t g_hold_until;      // place_tick: Link held at a far jump's landing spot until this frame

// Reboot: the next seed's door_jumps.tsv, nothing found in the old heap.
RT_ON_RESET(doorjump_reset) {
    free(g_jumps);
    g_jumps = NULL;
    g_njumps = -1;
    g_npairs = 0;
    g_player = g_player_scan = 0;
    g_player_level[0] = 0;
    g_player_rescan = 0;
    g_hold_until = 0;
    g_stick_freeze_until = 0;
}

static int is_player_collision(uint64_t p) {
    return LD64(p) == PLAYER_COLLISION_VT && LD64(p + 8) == p &&
           !memcmp((const void *)(uintptr_t)(p + 0x18), "PlayerCollision", 16);
}

// The component lives on the heap (16-byte aligned); scan for it after each level change (the previous level's
// player can stay in freed memory looking valid) and when the cached pointer goes stale. Several candidates:
// log them all (LARECOMP_DOORJUMP_LOG) and take the last one found (newest allocation is the live one - checked).
static uint64_t find_player(uint64_t frame) {
    uint64_t ps = LD64(PLAYER_STATUS_PTR);
    const char *lv = ps ? (const char *)(uintptr_t)ps : "";
    if (strncmp(g_player_level, lv, sizeof g_player_level - 1) || g_player_rescan) {  // new level: rescan once settled
        g_player_rescan = 0;
        strlcpy(g_player_level, lv, sizeof g_player_level);
        g_player = 0;
        g_player_scan = frame;
        return 0;
    }
    if (g_player && is_player_collision(g_player)) return g_player;
    g_player = 0;
    if (frame - g_player_scan < 30) return 0;
    g_player_scan = frame;
    extern int kernel_data_regions(uint64_t *addr, uint64_t *size, int max);
    uint64_t ra[64], rs[64];
    int nr = kernel_data_regions(ra, rs, 64), n = 0;
    for (int r = 0; r < nr; r++)
        for (uint64_t k = 0; k + 0x100 <= rs[r]; k += 16)
            if (LD64(ra[r] + k) == PLAYER_COLLISION_VT && is_player_collision(ra[r] + k)) {
                const float *p = (const float *)(uintptr_t)(ra[r] + k + 0x80);
                if (ENV("LARECOMP_DOORJUMP_LOG"))
                    LOG("[doorjump] candidate %llx (%.2f, %.2f, %.2f) parent %llx\n", (unsigned long long)(ra[r] + k),
                        p[0], p[1], p[2], (unsigned long long)LD64(ra[r] + k + 0x10));
                g_player = ra[r] + k;
                n++;
            }
    LOG("[doorjump] %s: PlayerCollision %s %llx (%d candidates)\n", lv, g_player ? "at" : "not found",
        (unsigned long long)g_player, n);
    return g_player;
}

uint64_t doorjump_player(uint64_t frame) { return find_player(frame); }  // (test hooks elsewhere: hookshot.c; digsanity: digs.c)

// LARECOMP_DOORJUMP_TEST=<frame>:<cell>:<side> (test): from <frame> on, once the level with jumps has been loaded
// for 60 frames (warp into the cell first, LARECOMP_WARP_AT), put Link 1.2 inside that doorway; the test holds the
// stick outward (tools/doorjump_test.sh, LARECOMP_INPUT); 300 frames later log the cell he is in
uint64_t g_stick_freeze_until;
static int g_jumps_fired;  // (test result: a jump that fired and then he ended elsewhere = pushed back by an enemy)  // runtime/hle/input.c releases the stick until this frame
int32_t g_test_lx, g_test_ly;  // stick held by the test (runtime/hle/input.c)
uint64_t g_test_stick_until;

static void test_tick(uint64_t frame, uint64_t pc, const char *lv) {
    static long at = -2, placed = -1;
    static char cell[8], side;
    static int er = -1, ec = -1;  // expected cell (the tested jump's target)
    static float px0, pz0;        // where Link was placed
    static int tries;
    static uint64_t in_level_since;
    static int was_in;
    if (at == -2) {
        at = -1;
        const char *e = ENV("LARECOMP_DOORJUMP_TEST");
        char sd[4];
        if (e && sscanf(e, "%ld:%7[^:]:%3s", &at, cell, sd) == 3) side = sd[0]; else at = -1;
    }
    if (at < 0 || !pc) return;
    // wait until the warp into the dungeon has finished: in a level with jumps, settled for 60 frames
    int in = 0;
    for (int i = 0; i < g_njumps && !in; i++) in = !strcmp(g_jumps[i].level, lv);
    if (in && !was_in) in_level_since = frame;
    was_in = in;
    float *pos = (float *)(uintptr_t)(pc + 0x80), *pos2 = (float *)(uintptr_t)(pc + 0xb0);
    int row = atoi(cell), col = cell[2] - 'A';
    // place once Link has arrived: the dungeon's fade-in / walk-in runs late under load and would overwrite the
    // placement - wait until he has moved (the walk-in) and then stood still 60 frames (or 15 s at most)
    static float lx_, lz_;
    static int moved_, still_;
    if (in) {
        float px_ = pos[0], pz_ = pos[2];
        if (fabsf(px_ - lx_) > 0.01f || fabsf(pz_ - lz_) > 0.01f) { if (frame - in_level_since > 5) moved_ = 1; still_ = 0; }
        else still_++;
        lx_ = px_, lz_ = pz_;
    }
    bool arrived = in && frame - in_level_since >= 120 && ((moved_ && still_ >= 60) || frame - in_level_since >= 900);
    if (placed < 0 && (long)frame >= at && arrived) {
        for (int i = 0; i < g_njumps; i++) {
            const Jump *j = &g_jumps[i];
            if (j->row != row || j->col != col || j->side != side || strcmp(j->level, lv)) continue;  // (this level's)
            float x0 = 15.0f * col, z0 = 12.0f * (row - 1), x = 0, z = 0;
            switch (side) {
            case 'W': x = x0 + 1.2f; z = z0 + j->own * 0.75f; break;
            case 'E': x = x0 + 15.0f - 1.2f; z = z0 + j->own * 0.75f; break;
            case 'N': z = z0 + 1.2f; x = x0 + j->own * 0.75f; break;
            default: z = z0 + 12.0f - 1.2f; x = x0 + j->own * 0.75f; break;
            }
            pos[0] = pos2[0] = x;
            pos[2] = pos2[2] = z;
            // on this doorway's floor: the pair's way back lands here and knows its height (raised doorways, Chaos)
            for (int k = 0; k < g_njumps; k++) {
                const Jump *b = &g_jumps[k];
                if (b->trow == row && b->tcol == col && b->tside == side && !strcmp(b->level, lv) && b->height > 0.3f)
                    { pos[1] = pos2[1] = b->height + 0.05f; break; }
            }
            px0 = x, pz0 = z;
            er = j->trow, ec = j->tcol;
            g_test_lx = side == 'W' ? -32767 : side == 'E' ? 32767 : 0;  // walk out
            g_test_ly = side == 'N' ? 32767 : side == 'S' ? -32767 : 0;
            g_test_stick_until = frame + 180;  // cut to 40 frames after the jump (doorjump_tick)
            LOG("[doorjump-test] f%llu placed at (%.2f, %.2f) in %s %c\n", (unsigned long long)frame, x, z, cell, side);
            break;
        }
        placed = (long)frame;
    } else if (placed >= 0 && (long)frame < placed + 120 && !g_jumps_fired && tries < 3 &&
               (fabsf(pos[0] - px0) > 4.0f || fabsf(pos[2] - pz0) > 4.0f)) {
        // the dungeon's arrival (fade-in / walk-in, late under load) put Link back at the entrance: place again
        LOG("[doorjump-test] f%llu placement overwritten (Link at %.2f, %.2f): again\n", (unsigned long long)frame, pos[0], pos[2]);
        placed = -1;
        tries++;
    } else if (placed >= 0 && (long)frame == placed + 300) {
        float x = pos[0], z = pos[2];
        int c = (int)floorf(x / 15.0f), r = (int)floorf(z / 12.0f) + 1;
        LOG("[doorjump-test] %s %c -> now in %02d%c (%.2f, %.2f), expected %02d%c: %s (%d jumps fired)\n", cell, side, r,
            'A' + c, x, z, er, 'A' + ec, (r == er && c == ec) ? "PASS" : g_jumps_fired ? "FAIL-after-jump" : "FAIL",
            g_jumps_fired);
        at = -1;
    }
}

uint64_t g_test_buttons, g_test_buttons_until;
int32_t g_stick_lx, g_stick_ly;  // left stick as the player holds it (runtime/hle/input.c)

// LARECOMP_PLACE=<frame>:<x>:<z>[:<script>] (test): put Link at x, z (any level) once the level has been loaded 60
// frames, then run <script> relative to that moment: steps "<delay>:<L|R|U|D|A|B|X|Y>:<frames>" separated by '/', e.g.
// "10:L:8/40:A:6/90:A:6/130:L:30" = face west, lift, throw, walk west (pot tests)
static long trace_until = -1;  // LARECOMP_DOORJUMP_TRACE: Link's position for 90 frames after each jump / placement

static void place_tick(uint64_t frame) {
    static long at = -2, done, placed_at = -1;
    static char script[256];
    static float px, pz;
    static uint64_t since;
    static char level[64];
    if (at == -2) {
        at = -1;
        const char *e = getenv("LARECOMP_PLACE");
        if (e && sscanf(e, "%ld:%f:%f:%255s", &at, &px, &pz, script) < 3) at = -1;
    }
    if (placed_at >= 0 && script[0]) {  // action script
        long t = (long)frame - placed_at;
        char buf[256];
        strlcpy(buf, script, sizeof buf);
        for (char *st = strtok(buf, "/"); st; st = strtok(NULL, "/")) {
            long d, n;
            char k;
            if (sscanf(st, "%ld:%c:%ld", &d, &k, &n) != 3 || t != d) continue;
            extern int32_t g_test_lx, g_test_ly;
            extern uint64_t g_test_stick_until;
            if (strchr("LRUD", k)) {
                g_test_lx = k == 'L' ? -32767 : k == 'R' ? 32767 : 0;
                g_test_ly = k == 'U' ? 32767 : k == 'D' ? -32767 : 0;
                g_test_stick_until = frame + n;
            } else {
                g_test_buttons = k == 'A' ? 1 : k == 'X' ? 4 : k == 'Y' ? 8 : 2;  // bit 0 A, 1 B, 2 X, 3 Y
                g_test_buttons_until = frame + n;
            }
            LOG("[place] f%llu script %c for %ld\n", (unsigned long long)frame, k, n);
        }
    }
    if (at < 0 || done || (long)frame < at) return;
    uint64_t ps = LD64(PLAYER_STATUS_PTR);
    const char *lv = ps ? (const char *)(uintptr_t)ps : "";
    if (strncmp(level, lv, sizeof level - 1)) { strlcpy(level, lv, sizeof level); since = frame; return; }
    if (frame - since < 60) return;
    uint64_t pc = find_player(frame);
    if (!pc) return;
    float *pos = (float *)(uintptr_t)(pc + 0x80), *pos2 = (float *)(uintptr_t)(pc + 0xb0);
    pos[0] = pos2[0] = px;
    pos[2] = pos2[2] = pz;
    LOG("[place] f%llu Link -> (%.2f, %.2f) in %s\n", (unsigned long long)frame, px, pz, lv);
    done = 1;
    placed_at = (long)frame;
    if (ENV("LARECOMP_DOORJUMP_TRACE")) trace_until = (long)frame + 240;
}

void doorjump_tick(uint64_t frame) {
    place_tick(frame);
    if (g_njumps < 0) load();
    {   // LARECOMP_LOG_LINK=<every N frames>: Link's position (movement tests)
        static int every = -1;
        if (every < 0) { const char *e = getenv("LARECOMP_LOG_LINK"); every = e ? atoi(e) : 0; }
        uint64_t pc0 = every > 0 && frame % (uint64_t)every == 0 ? find_player(frame) : 0;
        if (pc0) {
            float *p0 = (float *)(uintptr_t)(pc0 + 0x80);
            LOG("[link] f%llu (%.2f, %.2f, %.2f)\n", (unsigned long long)frame, p0[0], p0[1], p0[2]);
        }
    }
    if ((long)frame <= trace_until && !g_njumps) {  // (trace without a jump table: vanilla tests)
        uint64_t pc0 = find_player(frame);
        if (pc0) {
            float *p0 = (float *)(uintptr_t)(pc0 + 0x80);
            LOG("[doorjump-trace] f%llu (%.2f, %.2f, %.2f)\n", (unsigned long long)frame, p0[0], p0[1], p0[2]);
        }
    }
    if (!g_njumps) return;
    if (ENV("LARECOMP_DOORJUMP_TEST")) {
        uint64_t ps0 = LD64(PLAYER_STATUS_PTR);
        if (ps0) test_tick(frame, find_player(frame), (const char *)(uintptr_t)ps0);
    }
    uint64_t ps = LD64(PLAYER_STATUS_PTR);
    const char *lv = ps ? (const char *)(uintptr_t)ps : "";
    int any = 0;
    for (int i = 0; i < g_njumps && !any; i++) any = !strcmp(g_jumps[i].level, lv);
    if (!any) return;
    uint64_t pc = find_player(frame);
    if (!pc) return;
    float *pos = (float *)(uintptr_t)(pc + 0x80), *pos2 = (float *)(uintptr_t)(pc + 0xb0);
    float x = pos[0], z = pos[2];
    // after a jump to a room that isn't next door, the game hasn't loaded that room's floor yet (it streams the
    // rooms around Link) and he'd fall through: hold him at the landing spot for a moment
    static float hold_x, hold_y, hold_z;
    if (frame < g_hold_until) {
        pos[0] = pos2[0] = hold_x, pos[1] = pos2[1] = hold_y, pos[2] = pos2[2] = hold_z;
        x = hold_x, z = hold_z;
    }
    if ((long)frame <= trace_until) {
        LOG("[doorjump-trace] f%llu (%.2f, %.2f, %.2f)\n", (unsigned long long)frame, x, pos[1], z);
        if (ENV("LARECOMP_DOORJUMP_DUMP")) {  // every float of the PlayerCollision object that looks like x or z
            const float *o = (const float *)(uintptr_t)pc;
            for (int k = 0; k < 0x400 / 4; k++)
                if ((fabsf(o[k] - x) < 0.6f || fabsf(o[k] - z) < 0.6f) && fabsf(o[k]) > 1.0f)
                    LOG("[doorjump-dump]   +%#x = %.3f\n", k * 4, o[k]);
        }
    }
    static float px, pz;  // last frame (only moving INTO a doorway jumps)
    float dx = x - px, dz = z - pz;
    px = x, pz = z;
    int col = (int)floorf(x / 15.0f), row = (int)floorf(z / 12.0f) + 1;
    float lx = x - 15.0f * col, lz = z - 12.0f * (row - 1);
    for (int i = 0; i < g_njumps; i++) {
        Jump *j = &g_jumps[i];
        if (j->col != col || j->row != row || strcmp(j->level, lv)) continue;
        float depth, lateral, out;  // distance to the edge, position along it, movement towards it
        switch (j->side) {
        case 'W': depth = lx; lateral = lz; out = -dx; break;
        case 'E': depth = 15.0f - lx; lateral = lz; out = dx; break;
        case 'N': depth = lz; lateral = lx; out = -dz; break;
        default: depth = 12.0f - lz; lateral = lx; out = dz; break;
        }
        if (ENV("LARECOMP_DOORJUMP_LOG") && depth < 1.5f)
            LOG("[doorjump] f%llu %c depth %.3f lateral %.3f out %.4f still %d y %.2f\n", (unsigned long long)frame, j->side, depth, lateral, out, j->still, pos[1]);
        // (pressed against the wall he bounces between two positions each frame, ~0.3 from the edge)
        if (depth > 0.7f || fabsf(lateral - j->own * 0.75f) > 1.2f) { j->still = 0; continue; }  // per doorway: a cell's other doorways mustn't reset it
        if (depth > 0.55f) continue;
        if (!j->still && out <= 0.01f) continue;  // armed only by walking in (not by landing here)
        // ...with the stick held out through the doorway (not knocked into it by an enemy: DevChaos1 Catfish 03D W)
        if (!j->still) {
            int32_t along = j->side == 'W' ? -g_stick_lx : j->side == 'E' ? g_stick_lx : j->side == 'N' ? g_stick_ly : -g_stick_ly;
            if (along < 8000) continue;
        }
        // in the doorway's last half tile: release the stick for a moment (moving him while he walks gets undone by
        // that frame's movement, and where the neighbour has no wall he'd walk on into its void), jump at rest
        g_stick_freeze_until = frame + 3;
        ++j->still;
        // jump only after ~0.4 s at rest: while Link is in the game's push-against-a-wall state (it outlasts the
        // push) position writes are undone (Angler's 03G -> 02G: 12 frames failed, 20 passed). LARECOMP_DJ_REST: frames
        static int rest = -1;
        if (rest < 0) rest = getenv("LARECOMP_DJ_REST") ? atoi(getenv("LARECOMP_DJ_REST")) : 24;
        // ...unless he's dropping: where the neighbour has no wall at this doorway there's nothing under him past the
        // edge - jump at once, before he falls (Tail Cave 05F south in a generated layout: fell, then respawned)
        bool falling = pos[1] < -0.3f;
        if (!falling && (j->still < rest || (fabsf(out) > 0.02f && j->still < rest + 8))) continue;
        j->still = 0;
        float nx = x, nz = z, across = j->depth;  // just past the edge, in front of the partner doorway
        // just inside the target room's doorway on its side (for a neighbour: just past the shared edge)
        float tx0 = 15.0f * j->tcol, tz0 = 12.0f * (j->trow - 1);
        switch (j->tside) {
        case 'E': nx = tx0 + 15.0f - across; nz = tz0 + j->partner * 0.75f; break;
        case 'W': nx = tx0 + across; nz = tz0 + j->partner * 0.75f; break;
        case 'S': nz = tz0 + 12.0f - across; nx = tx0 + j->partner * 0.75f; break;
        default: nz = tz0 + across; nx = tx0 + j->partner * 0.75f; break;
        }
        LOG("[doorjump] %s %02d%c %c: (%.2f, %.2f) -> %02d%c %c (%.2f, %.2f)\n", lv, row, 'A' + col, j->side, x, z,
            j->trow, 'A' + j->tcol, j->tside, nx, nz);
        pos[0] = pos2[0] = nx;
        pos[2] = pos2[2] = nz;
        if (j->height > -90.0f) pos[1] = pos2[1] = j->height + 0.05f;  // onto the landing floor (raised rooms: 1.5)
        else if (falling) pos[1] = pos2[1] = 0.0f;  // back on the floor (doorways are at floor height 0)
        px = nx, pz = nz;
        if (abs(j->tcol - col) + abs(j->trow - row) != 1) { hold_x = nx, hold_y = pos[1], hold_z = nz; g_hold_until = frame + 30; }  // (y set above)
        g_jumps_fired++;
        if (j->pair >= 0) { extern void flags_ext_set(int idx); flags_ext_set(CHAOS_FLAG_BASE + j->pair); }
        if (ENV("LARECOMP_DOORJUMP_TRACE")) trace_until = (long)frame + 90;
        if (g_test_stick_until > frame + 40) g_test_stick_until = frame + 40;  // (test: walk on a little, then stop)
        break;
    }
}

// Chaos doorway pairs of a level (letters on the dungeon map), index = the pair's number in the seed (its flag)
int doorjump_chaos_pairs(const char *level, int *ids, int max) {
    if (g_njumps < 0) load();
    int n = 0;
    for (int k = 0; k < g_npairs && n < max; k++)
        if (!strcmp(g_pairs[k].level, level)) ids[n++] = k;
    return n;
}
void doorjump_chaos_pair(int id, int *col, int *row, char *side, int *slot) {
    for (int e = 0; e < 2; e++) { col[e] = g_pairs[id].col[e]; row[e] = g_pairs[id].row[e]; side[e] = g_pairs[id].side[e]; slot[e] = g_pairs[id].slot[e]; }
}
bool doorjump_chaos_twoway(int id) { return g_pairs[id].two; }  // false: Madness, end 0 leads to end 1 only
bool doorjump_chaos_seen(int id) { extern bool flags_ext_get(int idx); return flags_ext_get(CHAOS_FLAG_BASE + id); }

void doorjump_player_rescan(void) { g_player_rescan = 1; }
// Link's position (x, z) in the current level, for other features (runtime/hle/escape.c); false until found
bool doorjump_player_xz(uint64_t frame, float *x, float *z) {
    uint64_t pc = find_player(frame);
    if (!pc) return false;
    *x = *(float *)(uintptr_t)(pc + 0x80), *z = *(float *)(uintptr_t)(pc + 0x88);
    return true;
}
