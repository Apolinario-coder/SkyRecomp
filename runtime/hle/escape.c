// Escape Bottle: every randomizer seed made with it (settings.json "escape-bottle": tools/rando/las_headless.py drops
// one Bottle from the item pool) starts with the game's THIRD bottle (inventory bit 15, item slot 0xf) as the Escape
// Bottle: Tarin hands it over with the starting items, in the game's own get-item message
// (tools/rando/escape_bottle.py). Equipped on X / Y and used: inside a dungeon Link warps to that dungeon's entrance
// (the entrance room stays at its own cell in shuffled layouts: tools/rando/dungeon_gen.py ENTRANCES), anywhere else
// to Mabe Village, walking out of Marin & Tarin's front door (Field_11C; user 2026-10-11 - was just inside it).
//
// Why the third bottle: it is a real inventory item, so it gets an item-page slot, X / Y assignment and the game's own
// "may Link use an item now?" rules for free: the use is caught in the game's predicate for that slot
// (tools/patches.py BOTTLE3_USE_SITE), which only runs while Link is free to act - no cutscene, dialog, menu, level
// or room transition. A bottle that never swings never catches anything, so it stays empty. Not usable in boss arenas
// (rect per arena below: the fight would be left half done) or Dampe's chambers (chamber.c keeps run state): there it
// does nothing (no swing either: a fairy caught in it would turn it into a fairy bottle).
// Nothing in the item logic knows about it (convenience only). Drawn GOLD (escape_bottle_tint below) on the Items page,
// the HUD button and in Tarin's hands (no name tag: the gold says it - user 2026-10-11).
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../hle.h"
#include "../rando.h"

#define PLAYER_STATUS_PTR (MAIN_BASE + 0x1CC1368)  // PlayerStatus*: level name at +0, inventory bits at +0xa8
extern uint64_t renderer_present_count(void);
extern int game_in_play(void);
extern int warp_busy(void);
extern void warp_request(const char *level, const char *locator);
extern bool doorjump_player_xz(uint64_t frame, float *x, float *z);

// Each dungeon's entrance spawn (TagPlayerStart in its entrance room; runtime/ui/warp_data.h "Entrance")
static const char *const kEntrance[][2] = {
    {"Lv01TailCave", "Lv01TailCave_08D"},       {"Lv02BottleGrotto", "Lv02BottleGrotto_08C"},
    {"Lv03KeyCavern", "Lv03KeyCavern_08B"},     {"Lv04AnglersTunnel", "Lv04AnglersTunnel_08D"},
    {"Lv05CatfishsMaw", "Lv05CatfishsMaw_08H"}, {"Lv06FaceShrine", "Lv06FaceShrine_08D"},
    {"Lv07EagleTower", "Lv07EagleTower_08B_b"}, {"Lv08TurtleRock", "Lv08TurtleRock_08D_b"},
    {"Lv09WindFishsEgg", "Lv09WindFishsEgg_08D"}, {"Lv10ClothesDungeon", "Lv10ClothesDungeon_08C"},
};
// Boss arenas (rooms holding a boss actor, types 0x62-0x6f; they never move in shuffled layouts): x / z rect from the
// room's grid origin (15 x 12 per cell; the side-view arenas 04AL / 05HL sit right of the grid, 14 tiles wide)
static const struct { const char *level; float x0, z0, x1, z1; } kBossArena[] = {
    {"Lv01TailCave", 90, 36, 105, 48},       {"Lv02BottleGrotto", 90, 36, 105, 48},
    {"Lv03KeyCavern", 90, 72, 105, 84},      {"Lv04AnglersTunnel", 135, -6, 160, 6},
    {"Lv05CatfishsMaw", 45, 12, 60, 24},     {"Lv06FaceShrine", 60, 36, 75, 48},
    {"Lv07EagleTower", 162, -6, 187, 6},     {"Lv08TurtleRock", 45, 12, 60, 24},
    {"Lv09WindFishsEgg", 75, 0, 90, 12},     {"Lv10ClothesDungeon", 15, 36, 30, 48},
};
static const char *const kHome[2] = {"Field", "Field_11C"};  // (out of Marin & Tarin's front door)
static const char *const kTarinHouse = "MarinTarinHouse";  // (where Tarin holds the bottle up)

// LARECOMP_ESCAPE=1 / 0 forces it on / off (tests: also in vanilla)
static int on = -1;  // (per seed: read again after an in-place reboot)
static bool active(void) {
    if (on >= 0) return on;
    const char *e = getenv("LARECOMP_ESCAPE");
    if (e) return on = atoi(e) != 0;
    on = 0;
    const char *dir = rando_seed_dir();
    if (!dir) return false;
    char path[512], buf[16384];
    snprintf(path, sizeof path, "%s/settings.json", dir);
    FILE *f = fopen(path, "r");
    if (!f) return false;
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    on = strstr(buf, "\"escape-bottle\": true") != NULL;  // (older seeds: three normal bottles)
    LOG("[escape] Escape Bottle %s\n", on ? "on" : "off");
    return on;
}

static const char *cur_level(void) {
    uint64_t ps = LD64(PLAYER_STATUS_PTR);
    return ps ? (const char *)(uintptr_t)ps : "";
}

static int in_boss_arena(const char *lv, uint64_t frame) {
    for (size_t i = 0; i < sizeof kBossArena / sizeof kBossArena[0]; i++) {
        if (strcmp(lv, kBossArena[i].level)) continue;
        float x, z;
        if (!doorjump_player_xz(frame, &x, &z)) return 0;  // (not found yet: allow)
        return x >= kBossArena[i].x0 && x < kBossArena[i].x1 && z >= kBossArena[i].z0 && z < kBossArena[i].z1;
    }
    return 0;
}

// Game thread, inside the player's update: the third bottle was just used on X / Y. 1 = handled (no bottle swing).
int rt_bottle3_use(void) {
    if (!active()) return 0;
    static uint64_t last;
    uint64_t frame = renderer_present_count();
    if (frame - last < 30 || warp_busy()) return 1;  // (the predicate can run more than once per press)
    last = frame;
    const char *lv = cur_level();
    if (!strncmp(lv, "Panel", 5) || in_boss_arena(lv, frame)) {
        LOG("[escape] refused in %s\n", lv);
        return 1;
    }
    const char *const *to = kHome;
    for (size_t i = 0; i < sizeof kEntrance / sizeof kEntrance[0]; i++)
        if (!strcmp(lv, kEntrance[i][0])) to = kEntrance[i];
    LOG("[escape] used in %s -> %s / %s\n", lv, to[0], to[1]);
    warp_request(to[0], to[1]);
    return 1;
}

void escape_tick(Cpu *c, uint64_t frame) {
    (void)c;
    // LARECOMP_SET8=<frame>:<hex addr>:<hex byte>[,...] (test: set save bytes, e.g. live save +0x5562 = the item slot on
    // Y, +0x5563 on X - the game's equip predicates read +0xa2 with Y, +0xa3 with X)
    static long at = -2;
    static char spec[256];
    if (at == -2) {
        at = -1;
        const char *e = getenv("LARECOMP_SET8");
        if (e) { strlcpy(spec, e, sizeof spec); at = atol(e); }
    }
    if (at >= 0 && (long)frame >= at && game_in_play()) {
        for (char *p = spec; p && *p;) {
            long f; unsigned long long a; unsigned v;
            if (sscanf(p, "%ld:%llx:%x", &f, &a, &v) == 3) {
                *(volatile uint8_t *)(uintptr_t)a = (uint8_t)v;
                LOG("[set8] %llx = %#x\n", a, v);
            }
            p = strchr(p, ',');
            if (p) p++;
        }
        at = -1;
    }
    if (!active() || !game_in_play()) return;
    // keep Link's position cached in boss levels (doorjump.c finds it by a heap scan after each level load)
    const char *lv = cur_level();
    for (size_t i = 0; i < sizeof kBossArena / sizeof kBossArena[0]; i++)
        if (!strcmp(lv, kBossArena[i].level)) {
            float x, z;
            doorjump_player_xz(frame, &x, &z);
            break;
        }
}

RT_ON_RESET(escape_reset) { on = -1; }  // (seed switch: the next seed may differ)

// Gold Escape Bottle: the renderer calls this for draws sampling the bottle's item icon (kind 1) or its 3D model's
// colour map (kind 2; runtime/nvn/texpatch.c identifies both by content). All three bottles share those textures, so
// the Escape Bottle's own draws are picked out: UI icons by the pane matrix in vertex UBO 0 (floats 16-23: 2x4,
// x scale at 16, translation 19 / 23 in the 1280x720 layout, origin at the centre, y up) - its Items page slot
// (last of the bottom row, 436, -67) and the HUD button holding item slot 0xf (Y at 476, 296; X at 571, 310; Y / X =
// PlayerStatus +0xa2 / +0xa3); the model only while Tarin hands it over (Marin & Tarin's house, not owned yet - the
// get sequence runs before AddItem). Tint (translate.c): rgb = mix(rgb, t0.rgb * lum^t1.w + t1.rgb * lum, t0.w).
// The Bomb Bag (tools/rando/bomb_bag_text.py): found bombs are held up as the Magic Powder sack while the flow keeps
// GlobalFlags 431 (unused0431) set; the sack is drawn bomb blue then. LARECOMP_BOMB_BAG_TINT=1: always (test).
#define BOMB_BAG_FLAG 431
static void bomb_bag_tint(float tint[8]) {
    const uint8_t *fl = (const uint8_t *)(uintptr_t)(MAIN_BASE + 0x1CBB220 + 0x5DC0);  // (live save: GlobalFlags)
    if (!((fl[BOMB_BAG_FLAG >> 3] >> (BOMB_BAG_FLAG & 7)) & 1) && !getenv("LARECOMP_BOMB_BAG_TINT")) return;
    // ltint (translate.c): colour.rgb, mix, glow.rgb, luminance power - the bombs' deep navy, lit
    static const float navy[8] = {0.10f, 0.24f, 1.30f, 1.0f, 0.0f, 0.01f, 0.06f, 1.45f};
    memcpy(tint, navy, sizeof navy);
}

void escape_bottle_tint(int kind, const float *ui, uint32_t ui_size, float tint[8]) {
    if (kind == 4) { bomb_bag_tint(tint); return; }  // (the Magic Powder sack's map)
    if (kind == 3) {  // (the fairy's model map: boss / mini-boss souls, runtime/hle/souls.c)
        extern void souls_fairy_tint(const float *m, uint32_t m_size, float tint[8]);
        souls_fairy_tint(ui, ui_size, tint);
        return;
    }
    if (!active()) return;
    uint64_t ps = LD64(PLAYER_STATUS_PTR);
    if (!ps) return;
    bool owned = LD8(ps + 0xa9) & 0x80;
    if (kind == 2) {
        if (owned || strcmp(cur_level(), kTarinHouse)) return;
        extern void pot_gold_tint(float tint[8]);  // (the gold of unchecked pots / bushes: lit 3D)
        pot_gold_tint(tint);
        return;
    }
    if (!owned || !ui || ui_size < 24 * 4) return;
    float x = ui[19], y = ui[23];
    bool on = fabsf(x - 436) < 24 && fabsf(y + 67) < 24;
    if (LD8(ps + 0xa2) == 0xf) on |= fabsf(x - 476) < 24 && fabsf(y - 296) < 24;
    if (LD8(ps + 0xa3) == 0xf) on |= fabsf(x - 571) < 24 && fabsf(y - 310) < 24;
    if (!on) return;
    static const float gold[8] = {1.30f, 0.86f, 0.16f, 1.0f, 0.12f, 0.05f, 0.0f, 1.25f};  // (UI: unlit, 0-1; deep gold)
    memcpy(tint, gold, sizeof gold);
}
