// Digsanity: every diggable tile of the overworld is a check, the item comes up out of the hole, and dug holes stay.
//
// The Field is 16 x 16 screens of 15 x 12 units; each screen's .leb grid has 10 x 8 tiles of 1.5 units, and the
// shovel snaps its hole to a tile centre (f_7000ddafc0). Tile ids: g = ((row - 1) * 16 + column) * 80 + tile. Tile g is
// dug once event flag DIG_FLAG_BASE + g is set, its item picked up once GOT_FLAG_BASE + g is (the flag bitfield is
// extended and kept in the save's sidecar: runtime/hle/flags_ext.c).
//
// rando/<seed>/digs.tsv (tools/rando/digs.py) lists every dig tile: screen, tile, ground height, and for a check its
// item (name, model, flow entry in SinkingSword.bfevfl, seashell, scale, turn) and what grows on the tile.
//
// Dig: the patch after the hole's spawn in the shovel's dig commit (tools/patches.py DIG_HOLE_SITE) calls
// rt_dig_hole: the first dig of a tile sets its flag. Items: a check's item is a SinkingSword item actor (type 0x194,
// what LAS uses for standing items, pots and bushes) spawned on the dug tile through the actor factory
// (f_70008f5ea0); touching it runs its flow entry - LAS's own item-get: Link holds it up, the game's message box,
// traps, progressive items. Its string parameters are type 5: indexes into the game's string pool (main+0x1cab2d0,
// a vector of {std::string, refs}; the game never fills it - we install our own storage). An item actor that dies
// with Link next to it was picked up; any other death (the Field unloading) leaves it to be spawned again.
// Holes: the game fills in its oldest holes beyond 80 (f_7000d22d10); with digsanity the limit is HOLE_CAP
// (tools/patches.py HOLE_CAP_SITES). ObjHolesDug actors (type 0x1cd) belong to the Field's loaded scene and are gone
// once Link leaves the Field. Every few frames the screens around Link are checked: a dug tile without a live hole
// gets one spawned the way the shovel does (f_7000d228d0), a dug check whose item is still there gets its item back.
// Tall grass and bushes would grow back over a dug tile on reload: the factory skips them there (rt_digs_skip_plant,
// called from runtime/hle/souls.c's create-function hook). Actors are tracked by a weak reference to their shared_ptr
// control block (libc++: shared count at +8 is -1 once the actor is gone; the block stays while we hold the weak count).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../hle.h"
#include "../rando.h"

#define PLAYER_STATUS_PTR (MAIN_BASE + 0x1CC1368)  // PlayerStatus*: current level name at +0
#define GAME_MGR (MAIN_BASE + 0x1C97188)           // +0x4a8: the current scene (weak), scene +0x228 -> +0x1b8 room sp
#define SCENE_LOCK (MAIN_BASE + 0x1b730)           // (out shared_ptr, scene handle): what the dig commit calls
#define SPAWN_HOLE (MAIN_BASE + 0xd228d0)          // (out, transform, zone, &room, w4, s0): ObjHolesDug
#define HOLE_LIST_INIT (MAIN_BASE + 0xd22860)      // first-use init of the game's hole list (the shovel calls it first)
#define CREATE_ACTOR (MAIN_BASE + 0x8f5ea0)        // (out shared_ptr, &room, spawn info)
#define STR_POOL (MAIN_BASE + 0x1cab2d0)           // std::vector<{std::string, u64 refs}>: type-5 parameters
#define STR_POOL_INIT (MAIN_BASE + 0x1d510)        // its first-use init (clears the vector)
#define SP_RELEASE (MAIN_BASE + 0xbae080)          // shared_ptr release (x0 = control block)
#define WEAK_RELEASE (MAIN_BASE + 0x14311f0)       // std::__shared_weak_count::__release_weak
#define DIG_FLAG_BASE 4096
#define NTILES (256 * 80)
#define GOT_FLAG_BASE (DIG_FLAG_BASE + NTILES)
#define HOLE_CAP 600  // holes alive at once (all within the Field; the 3 x 3 screens around Link hold at most 720)
#define POOL_CAP 1024
#define T_SINKING_SWORD 0x194
#define T_LAWN 0xb5   // ObjLawn: tall grass
#define T_GRASS 0xb6  // ObjGrass: bush
#define T_HOLE 0x1cd   // ObjHolesDug

bool flags_ext_get(int idx);
void flags_ext_set(int idx);
uint64_t doorjump_player(uint64_t frame);

typedef struct {
    bool dig, plant, shell;  // plant: grass / a bush grows there (kept away once dug)
    float y, scale, turn;
    const char *name, *model, *mname, *entry;  // item (check) or NULL
} Tile;
static Tile *g_tiles;             // NTILES
static int g_on = -1;
static uint64_t g_alive[NTILES];  // control block of the tile's hole (weak ref held), 0 = none known
static uint64_t g_item[NTILES];   // ... of the tile's item actor
static uint32_t g_zone = 1;       // zone id the shovel passes (1 on the Field; refreshed by every real dig)
static int g_queue[64], g_nq;     // dug checks whose item is to be spawned
static FILE *g_test_out;          // (dig test: LARECOMP_DIG_TEST)
static uint8_t g_spawns[NTILES];  // holes reopened per tile that didn't stay (3: give up)
static uint64_t g_spawn_at[NTILES];  // ... frame of the last reopen (a hole that lasted was unloaded, not failed)
static uint64_t g_quiet[NTILES];  // reopened hole: until this frame nothing else may appear on the tile (its dig drop)
static uint64_t g_frame;
static int g_field_was;           // (the tick: on the Field / which scene, last frame)
static uint64_t g_scene_was;
static uint64_t g_scratch;        // guest memory for the spawn arguments
static uint64_t g_pool;           // our string pool storage (guest memory), 0 = not installed this boot
static char *g_strtab[POOL_CAP];  // its strings (host copies, for lookups)
static int g_nstr;

// Reboot (another seed / vanilla, in place): its own dig table and nothing dug; actors and the pool were guest memory.
RT_ON_RESET(digs_reset) {
    if (g_tiles)
        for (int g = 0; g < NTILES; g++)
            free((void *)g_tiles[g].name), free((void *)g_tiles[g].model), free((void *)g_tiles[g].mname),
                free((void *)g_tiles[g].entry);
    free(g_tiles);
    g_tiles = NULL;
    g_on = -1;
    memset(g_alive, 0, sizeof g_alive);
    memset(g_item, 0, sizeof g_item);
    memset(g_spawns, 0, sizeof g_spawns);
    memset(g_spawn_at, 0, sizeof g_spawn_at);
    memset(g_quiet, 0, sizeof g_quiet);
    g_zone = 1, g_nq = 0, g_frame = 0;
    g_field_was = 0, g_scene_was = 0, g_scratch = 0, g_pool = 0;
    for (int i = 0; i < g_nstr; i++) free(g_strtab[i]);
    g_nstr = 0;
}

static bool enabled(void) {
    if (g_on >= 0) return g_on;
    g_on = 0;
    const char *dir = rando_seed_dir();
    if (!dir && ENV("LARECOMP_DIG_TEST")) { g_tiles = calloc(NTILES, sizeof(Tile)); return g_on = 1; }  // (no items)
    if (!dir) return false;
    char path[512];
    snprintf(path, sizeof path, "%s/digs.tsv", dir);
    FILE *f = fopen(path, "r");
    if (!f) return false;
    g_tiles = calloc(NTILES, sizeof(Tile));
    char line[1024];
    int n = 0, items = 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        char *col[11], *save = NULL;
        int nc = 0;
        for (char *t = strtok_r(line, "\t\n", &save); t && nc < 11; t = strtok_r(NULL, "\t\n", &save)) col[nc++] = t;
        if (nc < 11) continue;
        int row = atoi(col[0]), c = col[0][2] - 'A', tile = atoi(col[1]);
        if (row < 1 || row > 16 || c < 0 || c > 15 || tile < 0 || tile >= 80) continue;
        Tile *t = &g_tiles[((row - 1) * 16 + c) * 80 + tile];
        t->dig = true, t->y = (float)atof(col[2]);
        t->plant = !strcmp(col[10], "grass") || !strcmp(col[10], "bush");
        if (strcmp(col[4], "-")) {
            t->name = strdup(col[3]), t->model = strdup(col[4]), t->mname = strdup(col[5]), t->entry = strdup(col[6]);
            t->shell = col[7][0] == '1', t->scale = (float)atof(col[8]), t->turn = (float)atof(col[9]);
            items++;
        }
        n++;
    }
    fclose(f);
    g_on = n > 0;
    LOG("[digs] %d dig tiles, %d with items (%s)\n", n, items, path);
    return g_on;
}

uint64_t rt_hole_cap(void) { return enabled() ? HOLE_CAP : 0x51; }

static bool on_field(void) {
    uint64_t ps = LD64(PLAYER_STATUS_PTR);
    return ps && !strcmp((const char *)(uintptr_t)ps, "Field");
}

static int tile_at(float x, float z) {
    int col = (int)floorf(x / 15.0f), row0 = (int)floorf(z / 12.0f);
    if (col < 0 || col > 15 || row0 < 0 || row0 > 15) return -1;
    int tx = (int)floorf((x - col * 15.0f) / 1.5f), tz = (int)floorf((z - row0 * 12.0f) / 1.5f);
    if (tx < 0 || tx > 9 || tz < 0 || tz > 7) return -1;
    return (row0 * 16 + col) * 80 + tz * 10 + tx;
}

static void tile_xz(int g, float *x, float *z) {
    *x = (g / 80 % 16) * 15.0f + (g % 80 % 10) * 1.5f + 0.75f;
    *z = (g / 80 / 16) * 12.0f + (g % 80 / 10) * 1.5f + 0.75f;
}

// the factory asks for every actor's create function (runtime/hle/souls.c): no grass / bush on a dug Field tile
// ... and no dig drop out of a reopened hole: a reopened ObjHolesDug rolls the shovel's drop (rupees, hearts) like a
// fresh dig, whatever it is passed - anything but the hole and our item appearing on its tile right after is dropped
bool rt_digs_skip_plant(uint32_t type, uint64_t info) {
    if (!enabled() || !on_field()) return false;
    const float *pos = (const float *)(uintptr_t)(info + 0x180);  // (spawn info +0x180: position)
    int g = tile_at(pos[0], pos[2]);
    if (g < 0 || !g_tiles[g].dig) return false;
    if (type == T_LAWN || type == T_GRASS) return flags_ext_get(DIG_FLAG_BASE + g);
    if (type != T_SINKING_SWORD && type != T_HOLE && g_frame < g_quiet[g]) {
        LOG("[digs] no drop out of the reopened hole on %02d%c tile %d (actor type %#x)\n", g / 80 / 16 + 1,
            'A' + g / 80 % 16, g % 80, type);
        return true;
    }
    return false;
}

static void hold_weak(uint64_t *slot, uint64_t ctrl) {
    if (!ctrl || *slot == ctrl) return;
    __atomic_fetch_add((int64_t *)(uintptr_t)(ctrl + 0x10), 1, __ATOMIC_ACQ_REL);
    *slot = ctrl;  // (callers drop an older ref first)
}

static bool alive(uint64_t ctrl) { return ctrl && (int64_t)LD64(ctrl + 8) >= 0; }

static void drop(Cpu *c, uint64_t *slot) {
    if (!*slot) return;
    c->x[0] = *slot;
    *slot = 0;
    guest_call(c, WEAK_RELEASE);
}

// shovel dig commit, right after the hole's spawn: [sp+0x10] the hole (actor, control block), x21 its transform
// (+0 position, +0x30 ground point on the tile centre), x20 +0xb0 the zone id
void rt_dig_hole(Cpu *c) {
    if (!enabled() || !on_field()) return;
    const float *t = (const float *)(uintptr_t)c->x[21];
    int g = tile_at(t[12], t[14]);
    if (g < 0) return;
    if (g_test_out) { fprintf(g_test_out, "DUG %d\n", g); fflush(g_test_out); }
    g_zone = LD32(c->x[20] + 0xb0);
    uint64_t ctrl = LD64(c->sp + 0x18);
    if (g_alive[g] && g_alive[g] != ctrl) {  // a dead hole's ref (replaced)
        uint64_t saved[31], sp = c->sp;
        memcpy(saved, c->x, sizeof saved);
        drop(c, &g_alive[g]);
        memcpy(c->x, saved, sizeof saved);
        c->sp = sp;
    }
    hold_weak(&g_alive[g], ctrl);
    if (flags_ext_get(DIG_FLAG_BASE + g)) return;
    flags_ext_set(DIG_FLAG_BASE + g);
    LOG("[digs] dug %02d%c tile %d (flag %d)%s%s\n", g / 80 / 16 + 1, 'A' + g / 80 % 16, g % 80, DIG_FLAG_BASE + g,
        g_tiles[g].entry ? ": " : "", g_tiles[g].entry ? g_tiles[g].name : "");
    if (g_tiles[g].entry && g_nq < 64) g_queue[g_nq++] = g;  // (spawned on the next tick, outside the shovel's code)
}

// a string's index in the pool (ours: installed on first use, the game's init run first - it would clear it)
static int pool_index(Cpu *c, const char *str) {
    for (int i = 0; i < g_nstr; i++)
        if (!strcmp(g_strtab[i], str)) return i;
    if (!g_pool) {
        guest_call(c, STR_POOL_INIT);
        if (LD64(STR_POOL) != LD64(STR_POOL + 8)) LOG("[digs] warning: the string pool was in use\n");
        g_pool = (uint64_t)(uintptr_t)hle_alloc(32 * POOL_CAP);
        ST64(STR_POOL, g_pool), ST64(STR_POOL + 8, g_pool), ST64(STR_POOL + 16, g_pool + 32 * POOL_CAP);
        for (int i = 0; i < g_nstr; i++) free(g_strtab[i]);
        g_nstr = 0;
    }
    uint64_t e = LD64(STR_POOL + 8);
    if (g_nstr == POOL_CAP || e >= g_pool + 32 * POOL_CAP) {
        static int nl;
        if (nl++ < 5) LOG("[digs] pool full: ours %d, vector %llx..%llx (%lld entries), our storage %llx\n", g_nstr,
                          (unsigned long long)LD64(STR_POOL), (unsigned long long)e, (long long)((e - LD64(STR_POOL)) / 32),
                          (unsigned long long)g_pool);
        return -1;
    }
    size_t n = strlen(str);
    char *buf = hle_alloc(n + 16);
    memcpy(buf, str, n + 1);
    ST64(e, ((n + 16) & ~15ull) | 1);  // libc++ long string: capacity | 1, size, data
    ST64(e + 8, n);
    ST64(e + 16, (uint64_t)(uintptr_t)buf);
    ST64(e + 24, 1ull << 40);  // refs: never released
    ST64(STR_POOL + 8, e + 32);
    g_strtab[g_nstr++] = strdup(str);  // (the lookup: each string once - it was never counted, every spawn added
    return (int)((e - g_pool) / 32);      //  its six again and the pool was full after ~170 item spawns)
}

// a loaded screen's room (shared_ptr): what a hole or item on that screen is spawned into - the shovel uses the
// current one (scene +0x228 = Link's screen's holder, +0x1b8 its room), but a screen's actors die with its own room,
// so one spawned into Link's room on another screen would go when Link's screen unloads. The holders sit in a pool of
// 0x260-byte entries (+0x50 the entry itself, +0x240 1 while loaded, +0x1a8 -> +0x74 the screen's file, "Field_11A.").
// false when the scene isn't up or the screen isn't loaded
static bool screen_room(Cpu *c, uint64_t pair, int screen, uint64_t *sctrl) {
    uint64_t mgr = LD64(GAME_MGR);
    *sctrl = 0;
    if (!mgr) return false;
    c->x[0] = pair + 0x10;  // (scene shared_ptr, released by the caller)
    c->x[1] = LD64(mgr + 0x4a8);
    guest_call(c, SCENE_LOCK);
    uint64_t scene = LD64(pair + 0x10), holder = scene ? LD64(scene + 0x228) : 0;
    *sctrl = LD64(pair + 0x18);
    if (!holder) return false;
    char want[16];
    snprintf(want, sizeof want, "Field_%02d%c", screen / 16 + 1, 'A' + screen % 16);
    for (int k = 0; k < 64; k++) {  // (the current holder first, then outwards)
        uint64_t e = holder + (int64_t)((k & 1) ? -(k + 1) / 2 : k / 2) * 0x260;
        if (e < HEAP_BASE || e + 0x260 > HEAP_BASE + HEAP_MAX || LD64(e + 0x50) != e || LD32(e + 0x240) != 1) continue;
        uint64_t info = LD64(e + 0x1a8), room = LD64(e + 0x1b8);
        if (!room || info < HEAP_BASE || info >= HEAP_BASE + HEAP_MAX) continue;
        if (strncmp((const char *)(uintptr_t)(info + 0x74), want, 9)) continue;  // ("Field_11A.leb")
        ST64(pair, room);  // room (ptr, control block): borrowed, as the shovel does
        ST64(pair + 8, LD64(e + 0x1c0));
        return true;
    }
    return false;
}

static uint64_t scratch(void) {
    if (!g_scratch) g_scratch = (uint64_t)(uintptr_t)hle_alloc(0x400);
    memset((void *)(uintptr_t)g_scratch, 0, 0x400);
    return g_scratch;
}

// false: the tile's screen isn't loaded
static bool spawn_hole(Cpu *c, int g) {
    uint64_t s = scratch(), sctrl;
    guest_call(c, HOLE_LIST_INIT);
    bool loaded = screen_room(c, s + 0x10, g / 80, &sctrl);
    if (loaded) {
        float x, z, y = g_tiles[g].y;
        tile_xz(g, &x, &z);
        float *tr = (float *)(uintptr_t)(s + 0x40);  // the shovel's transform: position, rotation, scale, ground, normal
        tr[0] = x, tr[1] = y + 0.01f, tr[2] = z;
        tr[7] = 1.0f;
        tr[8] = tr[9] = tr[10] = 1.0f;
        tr[12] = x, tr[13] = y, tr[14] = z;
        tr[17] = 1.0f;
        c->x[0] = s;
        c->x[1] = s + 0x40;
        c->x[2] = g_zone;
        c->x[3] = s + 0x10;
        c->x[4] = 0;  // (1: the hole shows filled in)
        c->v[0] = 0;  // s0: the shovel passes 1.0 (normal y); the hole then may drop a rupee - 0 drops nothing (checked)
        guest_call(c, SPAWN_HOLE);
        uint64_t hctrl = LD64(s + 8);
        if (LD64(s) && hctrl) {
            hold_weak(&g_alive[g], hctrl);
            c->x[0] = hctrl;
            guest_call(c, SP_RELEASE);
        }
    }
    if (sctrl) { c->x[0] = sctrl; guest_call(c, SP_RELEASE); }
    return loaded;
}

// the tile's item as a SinkingSword item actor, the way a level spawns one (spawn info: +0 key, +8 type, +0xc zone,
// +0x10 eight {u32 value, u8 type} parameters, +0x180 position / rotation (quaternion) / scale; the rest zero).
// false: the tile's screen isn't loaded
static bool spawn_item(Cpu *c, int g) {
    const Tile *t = &g_tiles[g];
    const char *strs[8] = {t->model, t->mname, t->entry, "LARDigItem", t->shell ? "true" : "false", "", "", ""};
    int idx[8];
    for (int k = 0; k < 8; k++)
        if ((idx[k] = pool_index(c, strs[k])) < 0) { LOG("[digs] string pool full\n"); return true; }
    uint64_t s = scratch(), info = s + 0x40, sctrl;
    bool loaded = screen_room(c, s + 0x10, g / 80, &sctrl);
    if (loaded) {
        ST64(info, 0x7c00000000000000ull | (uint64_t)g);  // (actor key: a tag of ours + the tile)
        ST16(info + 8, T_SINKING_SWORD);
        ST32(info + 0xc, g_zone);
        for (int k = 0; k < 8; k++) { ST32(info + 0x10 + 8 * k, (uint32_t)idx[k]); ST8(info + 0x14 + 8 * k, 5); }
        float x, z, *tp = (float *)(uintptr_t)(info + 0x180), a = t->turn * (float)M_PI / 360.0f;
        tile_xz(g, &x, &z);
        tp[0] = x, tp[1] = t->y, tp[2] = z;
        tp[5] = sinf(a), tp[7] = cosf(a);
        tp[8] = tp[9] = tp[10] = t->scale > 0 ? t->scale : 1.0f;
        c->x[0] = s;
        c->x[1] = s + 0x10;
        c->x[2] = info;
        guest_call(c, CREATE_ACTOR);
        uint64_t ictrl = LD64(s + 8);
        if (LD64(s) && ictrl) {
            hold_weak(&g_item[g], ictrl);
            c->x[0] = ictrl;
            guest_call(c, SP_RELEASE);
        }
        LOG("[digs] item %s on %02d%c tile %d (%s)\n", t->name, g / 80 / 16 + 1, 'A' + g / 80 % 16, g % 80,
            LD64(s) ? "up" : "failed");
    }
    if (sctrl) { c->x[0] = sctrl; guest_call(c, SP_RELEASE); }
    return loaded;
}

static bool hole_alive(int g) { return alive(g_alive[g]); }

// LARECOMP_DIG_TEST=<frame>:<list>[:<first>:<count>] (test): from <frame> on (shovel on Y, sword on B), digs every
// tile of <list> (tools/rando/digs.py --test-list: tile id, C = cut grass / a bush first, nearest Field locator, then
// the walkable neighbours Link can dig it from as x,z,direction). The test warps to the locator when it changes, puts
// Link on a stand tile, turns him toward the tile, cuts (B) when needed and digs (Y); no hole -> the next stand.
// Every hole dug is logged ("DUG <tile>", runtime/hle/digs.c rt_dig_hole) to build/digtest[_<SHOT_ID>].txt, then
// "OK <tile>" / "MISS <tile>" per list entry: the DUG lines are the tiles proven diggable.
static void dig_test(uint64_t frame) {
    enum { MAXS = 4 };
    typedef struct { int g, ns; char cut; char warp[40]; float x[MAXS], z[MAXS]; char dir[MAXS]; } Entry;
    static int state = -1, n, cur, stand, first, count, prev_screen = -1;
    static Entry *list;
    static long at;
    static uint64_t t0, warp_at;
    static char warped[40];
    if (state == -1) {
        state = -2;
        const char *e = ENV("LARECOMP_DIG_TEST");
        char path[256];
        first = 0, count = 1 << 30;
        if (!e || sscanf(e, "%ld:%255[^:]:%d:%d", &at, path, &first, &count) < 2) return;
        FILE *f = fopen(path, "r");
        if (!f) return;
        char line[512];
        list = calloc(8192, sizeof *list);
        for (int i = 0; fgets(line, sizeof line, f) && n < 8192; i++) {
            if (i < first || i >= first + count) continue;
            Entry *en = &list[n];
            char cut[4], *tok, *save = NULL;
            if (sscanf(line, "%d %3s %39s", &en->g, cut, en->warp) != 3) continue;
            en->cut = cut[0] == 'C';
            strtok_r(line, " \n", &save), strtok_r(NULL, " \n", &save), strtok_r(NULL, " \n", &save);
            while ((tok = strtok_r(NULL, " \n", &save)) && en->ns < MAXS) {
                char d;
                if (sscanf(tok, "%f,%f,%c", &en->x[en->ns], &en->z[en->ns], &d) == 3) en->dir[en->ns++] = d;
            }
            n++;
        }
        fclose(f);
        char op[256];
        snprintf(op, sizeof op, "build/digtest%s%s.txt", ENV("SHOT_ID") ? "_" : "", ENV("SHOT_ID") ? ENV("SHOT_ID") : "");
        g_test_out = fopen(op, "w");
        state = 0;
        LOG("[digtest] %d tiles -> %s\n", n, op);
    }
    if (state < 0 || (long)frame < at) return;
    {   // (the shovel on Y: item slot 7 at player status +0xa2)
        uint64_t ps = LD64(PLAYER_STATUS_PTR);
        if (ps) ST8(ps + 0xa2, 7);
    }
    while (cur < n && (!list[cur].ns || flags_ext_get(DIG_FLAG_BASE + list[cur].g))) {  // nowhere to stand / dug
        fprintf(g_test_out, "%s %d\n", list[cur].ns ? "OK" : "NOSTAND", list[cur].g);
        cur++, stand = 0;
    }
    if (cur >= n) {
        LOG("[digtest] done\n");
        if (g_test_out) fclose(g_test_out);
        g_test_out = NULL;
        if (!ENV("LARECOMP_DIG_TEST_STAY")) _exit(0);  // (STAY: the game goes on - a test that walks the holes after)
        state = -2;
        return;
    }
    const Entry *en = &list[cur];
    if (strcmp(warped, en->warp)) {  // far away: warp to the nearest locator first
        extern void warp_request(const char *level, const char *locator);
        warp_request("Field", en->warp);
        strlcpy(warped, en->warp, sizeof warped);
        warp_at = frame;
        prev_screen = -1;
        state = 0;
        return;
    }
    if (frame < warp_at + 200) return;
    if (!on_field()) {  // walked into a door / cave on the way: skip this tile, warp back
        if (frame > warp_at + 500) {
            fprintf(g_test_out, "LEFT %d\n", en->g);
            cur++, stand = 0, warped[0] = 0, state = 0;
        }
        return;
    }
    if (frame == warp_at + 200) { void doorjump_player_rescan(void); doorjump_player_rescan(); }
    uint64_t pc = doorjump_player(frame);
    if (!pc) return;
    float *pos = (float *)(uintptr_t)(pc + 0x80), *pos2 = (float *)(uintptr_t)(pc + 0xb0);
    extern int32_t g_test_lx, g_test_ly;
    extern uint64_t g_test_stick_until, g_test_buttons, g_test_buttons_until;
    if (state == 0) { t0 = frame; state = 1; }
    long t = (long)(frame - t0), settle = en->g / 80 != prev_screen ? 90 : 12, dig = settle + (en->cut ? 40 : 6);
    if (t <= settle) {  // hold Link on the stand tile while the area streams in, then turn him
        if (t == 2) g_test_buttons = 1, g_test_buttons_until = frame + 4;  // (a message box left open: closed)
        pos[0] = pos2[0] = en->x[stand];
        pos[2] = pos2[2] = en->z[stand];
        if (t == settle) {
            char d = en->dir[stand];
            g_test_lx = d == 'W' ? -32767 : d == 'E' ? 32767 : 0;
            g_test_ly = d == 'N' ? 32767 : d == 'S' ? -32767 : 0;
            g_test_stick_until = frame + 2;
        }
    } else if (en->cut && t == settle + 6) {  // cut the grass / bush (sword)
        g_test_buttons = 2;
        g_test_buttons_until = frame + 6;
    } else if (t == dig) {
        g_test_buttons = 8;  // Y: shovel
        g_test_buttons_until = frame + 6;
    } else if (t == dig + 24 || t == dig + 34) {  // A: close an item's message box (the next dig waits behind it)
        g_test_buttons = 1;
        g_test_buttons_until = frame + 4;
    } else if (t >= dig + 40) {
        prev_screen = en->g / 80;
        state = 0;
        if (flags_ext_get(DIG_FLAG_BASE + en->g)) return;  // (the loop above reports it)
        if (++stand < en->ns && stand < 2) return;  // (two stands: a third rarely helps)
        fprintf(g_test_out, "MISS %d\n", en->g);
        fflush(g_test_out);
        cur++, stand = 0;
    }
}

// game main thread, once per frame (runtime/hle/input.c)
void digs_game_tick(Cpu *c, uint64_t frame) {
    if (!enabled()) return;
    extern int game_in_play(void);
    if (!game_in_play()) return;
    dig_test(frame);
    g_frame = frame;
    uint64_t saved[31], sp = c->sp, pc = c->pc;
    __uint128_t v0 = c->v[0];
    memcpy(saved, c->x, sizeof saved);
    int field = on_field();
    uint64_t mgr = LD64(GAME_MGR), scene = mgr ? LD64(mgr + 0x4a8) : 0;
    if (field != g_field_was || scene != g_scene_was) {  // left / (re)loaded the Field: drop the refs of actors gone
        for (int g = 0; g < NTILES; g++) {
            if (g_alive[g] && !alive(g_alive[g])) drop(c, &g_alive[g]);
            if (g_item[g] && !alive(g_item[g])) drop(c, &g_item[g]);
        }
        memset(g_spawns, 0, sizeof g_spawns);
        memset(g_spawn_at, 0, sizeof g_spawn_at);
        g_field_was = field, g_scene_was = scene;
    }
    uint64_t pc0 = field ? doorjump_player(frame) : 0;
    const float *p = pc0 ? (const float *)(uintptr_t)(pc0 + 0x80) : NULL;
    for (int i = 0; i < g_nq; i++) if (field) spawn_item(c, g_queue[i]);
    g_nq = 0;
    if (p && frame % 4 == 0)  // an item gone with Link next to it was picked up (its flow ends in Destroy)
        for (int g = 0; g < NTILES; g++) {
            if (!g_item[g] || alive(g_item[g])) continue;
            float x, z;
            tile_xz(g, &x, &z);
            bool got = fabsf(p[0] - x) < 3.0f && fabsf(p[2] - z) < 3.0f;
            if (got) flags_ext_set(GOT_FLAG_BASE + g);
            LOG("[digs] item on %02d%c tile %d %s\n", g / 80 / 16 + 1, 'A' + g / 80 % 16, g % 80, got ? "picked up" : "gone");
            drop(c, &g_item[g]);
        }
    if (p && frame % 8 == 0) {
        int here = tile_at(p[0], p[2]), holes = 0, items = 0;
        if (here >= 0) {
            int row0 = here / 80 / 16, col = here / 80 % 16;
            static const int8_t around[9][2] = {{0, 0}, {0, -1}, {0, 1}, {-1, 0}, {1, 0}, {-1, -1}, {-1, 1}, {1, -1}, {1, 1}};
            for (int n = 0; n < 9; n++) {  // (Link's screen first: its holes are the ones on screen)
                int r = row0 + around[n][0], k = col + around[n][1];
                if (r < 0 || r > 15 || k < 0 || k > 15) continue;
                for (int t = 0; t < 80 && holes + items < 12; t++) {
                    int g = (r * 16 + k) * 80 + t;
                    if (!g_tiles[g].dig || !flags_ext_get(DIG_FLAG_BASE + g)) continue;
                    bool open = hole_alive(g);
                    if (!open && g_spawn_at[g] && frame - g_spawn_at[g] > 300) g_spawns[g] = 0;  // (it lasted)
                    if (g_spawns[g] < 3 && !open) {
                        drop(c, &g_alive[g]);
                        g_quiet[g] = frame + 240;
                        if (!spawn_hole(c, g)) break;  // (the screen isn't loaded: its holes come back once it is)
                        g_spawns[g]++;
                        g_spawn_at[g] = frame;
                        holes++;
                    }
                    if (g_tiles[g].entry && !g_item[g] && !flags_ext_get(GOT_FLAG_BASE + g) && spawn_item(c, g))
                        items++;
                }
            }
        }
        if (holes || items)
            LOG("[digs] f%llu: %d holes, %d items back around %02d%c\n", (unsigned long long)frame, holes, items,
                here / 80 / 16 + 1, 'A' + here / 80 % 16);
    }
    memcpy(c->x, saved, sizeof saved);
    c->v[0] = v0;
    c->sp = sp;
    c->pc = pc;
}
