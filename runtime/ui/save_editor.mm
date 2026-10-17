// Save editor tab: edits the game's live save buffer (main+0x1CBB220, an exact image of savedataN.bin),
// so changes apply immediately and are written out (with fresh checksums) the next time the game saves.
// Layout: notes/save_format.md.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "imgui.h"
#include "osk.h"
#include "item_names.h"
#include "option_rows.h"

extern "C" void give_item_request(int id, int count, int index);
extern "C" bool pets_owned(int c);             // runtime/hle/pets.c
extern "C" void pets_give(int c, bool on);
extern "C" const char *rando_seed_dir(void);

extern "C" {
#include "../hle.h"
}

#define LIVE_SAVE 0x7001CBB220ull  // main + 0x1CBB220
static uint8_t *S(void) { return (uint8_t *)(uintptr_t)LIVE_SAVE; }
template <typename T> static T &F(uint32_t off) { return *(T *)(S() + off); }

enum : uint32_t {
    H_NAME = 0x20, H_CUR_HP = 0x32, H_MAX_HP = 0x34, H_KEYS = 0x78, H_RUPEES = 0x7C, H_SHELLS = 0x80,
    NAME = 0x5388, RUPEES = 0x5540, HP = 0x5544, SHELLS = 0x5548, PIECES = 0x5550, CONTAINERS = 0x5558,
    TRADE = 0x555A, SONGS = 0x555B, UPGRADES = 0x555C, BOMBS = 0x555E, ARROWS = 0x555F, POWDER = 0x5560,
    ITEMS = 0x5568, KEYITEMS = 0x556C, SENSOR = 0x556E, FAIRIES = 0x5580, LEAVES = 0x5582,
};

static bool loaded(void) { return F<uint32_t>(0) == 0xE && F<uint16_t>(NAME) != 0; }

static int max_hp(void) {  // quarter-hearts, as GetMaxHealth() computes it
    return 12 + 4 * (__builtin_popcount(F<uint16_t>(CONTAINERS) & 0x1FF) + __builtin_popcountll(F<uint64_t>(PIECES)) / 4);
}

static uint64_t low_bits(int n) { return n >= 64 ? ~0ull : ((1ull << n) - 1); }

// Keep the file-select summary in step (the game also rebuilds it when it saves).
static void sync_header(void) {
    memcpy(S() + H_NAME, S() + NAME, 0x12);
    F<uint16_t>(H_CUR_HP) = (uint16_t)F<int16_t>(HP);
    F<uint16_t>(H_MAX_HP) = (uint16_t)max_hp();
    F<uint32_t>(H_KEYS) = F<uint32_t>(KEYITEMS) & 0x1FFFF;
    F<uint32_t>(H_RUPEES) = F<uint32_t>(RUPEES);
    F<uint64_t>(H_SHELLS) = F<uint64_t>(SHELLS);
}

static char g_name[32];

extern "C" void save_editor_on_open(void) {
    if (!loaded()) return;
    const uint16_t *u = (const uint16_t *)(S() + NAME);
    int n = 0;
    for (int i = 0; i < 8 && u[i]; i++) {  // UTF-16 -> UTF-8 (BMP)
        uint16_t c = u[i];
        if (c < 0x80) g_name[n++] = (char)c;
        else if (c < 0x800) { g_name[n++] = (char)(0xC0 | c >> 6); g_name[n++] = (char)(0x80 | (c & 0x3F)); }
        else { g_name[n++] = (char)(0xE0 | c >> 12); g_name[n++] = (char)(0x80 | ((c >> 6) & 0x3F)); g_name[n++] = (char)(0x80 | (c & 0x3F)); }
    }
    g_name[n] = 0;
}

static void write_name(const char *s) {
    uint16_t out[9] = {0};
    int n = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p && n < 8) {
        uint32_t c = *p++;
        if (c >= 0xE0 && p[0] && p[1]) { c = ((c & 0xF) << 12) | ((p[0] & 0x3F) << 6) | (p[1] & 0x3F); p += 2; }
        else if (c >= 0xC0 && p[0]) { c = ((c & 0x1F) << 6) | (p[0] & 0x3F); p += 1; }
        out[n++] = (uint16_t)c;
    }
    memcpy(S() + NAME, out, sizeof out);
}

// ---- rows (runtime/ui/option_rows.h)
static bool bit_get(uint32_t off, int bit, int width) {
    return width == 8 ? (F<uint8_t>(off) >> bit) & 1 : width == 16 ? (F<uint16_t>(off) >> bit) & 1 : (F<uint32_t>(off) >> bit) & 1;
}
static void bit_set(uint32_t off, int bit, int width, bool v) {
    if (width == 8) F<uint8_t>(off) = (uint8_t)((F<uint8_t>(off) & ~(1u << bit)) | ((uint32_t)v << bit));
    else if (width == 16) F<uint16_t>(off) = (uint16_t)((F<uint16_t>(off) & ~(1u << bit)) | ((uint32_t)v << bit));
    else F<uint32_t>(off) = (F<uint32_t>(off) & ~(1u << bit)) | ((uint32_t)v << bit);
}

struct Bit { const char *name; uint32_t off; int bit; int width; const char *desc; };
static void bit_rows(const Bit *b, int n) {
    for (int i = 0; i < n; i++) {
        bool v = bit_get(b[i].off, b[i].bit, b[i].width);
        if (opt_toggle(b[i].name, &v, false, b[i].desc)) bit_set(b[i].off, b[i].bit, b[i].width, v);
    }
}

// Level item: "owned" bit in ITEMS plus an upgrade bit in UPGRADES.
static void level(const char *t, int item_bit, int up_bit, const char *const names[3], const char *desc) {
    int lv = !((F<uint32_t>(ITEMS) >> item_bit) & 1) ? 0 : ((F<uint8_t>(UPGRADES) >> up_bit) & 1) ? 2 : 1;
    if (!opt_choice(t, &lv, names, 3, 0, desc)) return;
    F<uint32_t>(ITEMS) = (F<uint32_t>(ITEMS) & ~(1u << item_bit)) | ((uint32_t)(lv > 0) << item_bit);
    F<uint8_t>(UPGRADES) = (uint8_t)((F<uint8_t>(UPGRADES) & ~(1u << up_bit)) | ((uint32_t)(lv == 2) << up_bit));
}

static void ammo(const char *t, uint32_t count_off, int up_bit, int base_cap, const char *desc) {
    int cap = ((F<uint8_t>(UPGRADES) >> up_bit) & 1) ? base_cap * 2 : base_cap;
    int v = F<uint8_t>(count_off);
    if (v > cap) v = cap;
    char fmt[16];
    snprintf(fmt, sizeof fmt, "%%d / %d", cap);
    if (opt_int(t, &v, 0, cap, 1, cap, fmt, desc)) F<uint8_t>(count_off) = (uint8_t)v;
}

static void max_out(void) {
    F<uint16_t>(CONTAINERS) = 0x1FF;
    F<uint64_t>(PIECES) = 0xFFFFFFFFull;
    F<uint32_t>(RUPEES) = 999;
    F<uint64_t>(SHELLS) = low_bits(50);
    F<uint8_t>(LEAVES) = 0x1F;
    F<uint32_t>(ITEMS) |= 0xFFFF;
    F<uint8_t>(UPGRADES) |= 0x3F;
    F<uint8_t>(BOMBS) = 60; F<uint8_t>(ARROWS) = 60; F<uint8_t>(POWDER) = 40;
    F<uint8_t>(SONGS) = 7;
    F<uint32_t>(KEYITEMS) |= 0x7FFF;
    F<uint8_t>(SENSOR) |= 1;
    F<int16_t>(HP) = (int16_t)max_hp();
}

extern "C" void save_editor_draw(float scale) {
    if (!loaded()) {
        ImGui::TextDisabled("Load a save file first: the editor changes the save that's currently being played.");
        return;
    }
    opt_begin("##save", scale);

    opt_section("Quick");
    if (opt_button("Max out everything", "Every heart, item, upgrade, song, instrument and dungeon key, full ammo, "
                                         "999 rupees, all 50 Secret Seashells and 5 Golden Leaves.\n\n"
                                         "The hearts on the HUD update after the next screen change."))
        max_out();
    if (opt_button("Refill hearts & ammo", "Refills health, and the bombs, arrows and Magic Powder Link owns.")) {
        F<int16_t>(HP) = (int16_t)max_hp();
        if (F<uint32_t>(ITEMS) & (1u << 2)) F<uint8_t>(BOMBS) = (F<uint8_t>(UPGRADES) & 0x10) ? 60 : 30;
        if (F<uint32_t>(ITEMS) & (1u << 3)) F<uint8_t>(ARROWS) = (F<uint8_t>(UPGRADES) & 0x20) ? 60 : 30;
        if (F<uint32_t>(ITEMS) & (1u << 8)) F<uint8_t>(POWDER) = (F<uint8_t>(UPGRADES) & 0x08) ? 40 : 20;
    }

    opt_section("Player");
    if (opt_text("Name", g_name, 9 * 3, NULL, "The save file's name (up to 8 letters).")) write_name(g_name);
    int containers = __builtin_popcount(F<uint16_t>(CONTAINERS) & 0x1FF);
    if (opt_int("Heart Containers", &containers, 0, 9, 1, 0, "%d / 9",
                "Heart Containers (one heart each), from the dungeon bosses.\n\n"
                "The hearts on the HUD update after the next screen change."))
        F<uint16_t>(CONTAINERS) = (uint16_t)low_bits(containers);
    int pieces = __builtin_popcountll(F<uint64_t>(PIECES) & 0xFFFFFFFFull);
    if (opt_int("Pieces of Heart", &pieces, 0, 32, 1, 0, "%d / 32",
                "Pieces of Heart: every 4 make a heart.\n\nThe hearts on the HUD update after the next screen change."))
        F<uint64_t>(PIECES) = low_bits(pieces);
    {
        int mh = max_hp();
        if (F<int16_t>(HP) > mh) F<int16_t>(HP) = (int16_t)mh;
        float hearts = F<int16_t>(HP) / 4.0f;
        if (opt_slider("Health", &hearts, 0.25f, mh / 4.0f, 0.25f, mh / 4.0f, "%g hearts",
                       "Link's current health, in quarter hearts."))
            F<int16_t>(HP) = (int16_t)fmaxf(1, roundf(hearts * 4));
    }
    int rupees = (int)F<uint32_t>(RUPEES);
    if (opt_int("Rupees", &rupees, 0, 999, 1, 0, "%d", "Rupees in Link's wallet (up to 999).\n\nHold left / right to go faster."))
        F<uint32_t>(RUPEES) = (uint32_t)rupees;
    int shells = __builtin_popcountll(F<uint64_t>(SHELLS) & low_bits(50));
    if (opt_int("Secret Seashells", &shells, 0, 50, 1, 0, "%d / 50", "Secret Seashells collected."))
        F<uint64_t>(SHELLS) = low_bits(shells);
    int leaves = __builtin_popcount(F<uint8_t>(LEAVES) & 0x1F);
    if (opt_int("Golden Leaves", &leaves, 0, 5, 1, 0, "%d / 5", "Golden Leaves for Richard."))
        F<uint8_t>(LEAVES) = (uint8_t)low_bits(leaves);
    {
        static const char *const tunics[] = {"Green", "Red", "Blue", "Red + Blue"};
        int t = (F<uint8_t>(UPGRADES) >> 6) & 3;
        if (opt_choice("Tunics", &t, tunics, 4, 0, "The tunics from the Color Dungeon: red doubles Link's attack, blue "
                                                   "halves the damage he takes."))
            F<uint8_t>(UPGRADES) = (uint8_t)((F<uint8_t>(UPGRADES) & 0x3F) | (t << 6));
    }

    opt_section("Equipment");
    static const char *const sword[] = {"None", "Sword", "Level 2"};
    static const char *const shield[] = {"None", "Shield", "Mirror Shield"};
    static const char *const bracelet[] = {"None", "Power Bracelet", "Level 2"};
    level("Sword", 0, 0, sword, "Link's sword. Level 2 is the Noble Sword from the Seashell Mansion.");
    level("Shield", 1, 1, shield, "Link's shield. The Mirror Shield blocks beams and fire.");
    level("Power Bracelet", 10, 2, bracelet, "Lifts pots and rocks. Level 2 lifts the heavy ones.");
    static const Bit items[] = {
        {"Bombs", ITEMS, 2, 32, "The bombs item (bomb bag)."},
        {"Bow", ITEMS, 3, 32, "The bow."},
        {"Hookshot", ITEMS, 4, 32, "The Hookshot."},
        {"Boomerang", ITEMS, 5, 32, "The Boomerang."},
        {"Magic Rod", ITEMS, 6, 32, "The Magic Rod."},
        {"Shovel", ITEMS, 7, 32, "The Shovel."},
        {"Magic Powder", ITEMS, 8, 32, "Magic Powder."},
        {"Roc's Feather", ITEMS, 9, 32, "Roc's Feather: jump."},
        {"Pegasus Boots", ITEMS, 11, 32, "Pegasus Boots: dash."},
        {"Ocarina", ITEMS, 12, 32, "The Ocarina. Songs are under Ocarina songs."},
        {"Flippers", KEYITEMS, 0, 32, "Flippers: swim and dive."},
        {"Secret Medicine", KEYITEMS, 1, 32, "Secret Medicine: revives Link once when his health runs out."},
        {"Sleepy Toadstool", KEYITEMS, 15, 32, "The Sleepy Toadstool for the witch."},
        {"Seashell Sensor", SENSOR, 0, 8, "The Seashell Sensor: chimes near hidden Secret Seashells."},
    };
    bit_rows(items, sizeof items / sizeof items[0]);

    opt_section("Ammo");
    static const Bit ups[] = {
        {"Bomb bag upgrade", UPGRADES, 4, 8, "Carry up to 60 bombs instead of 30."},
        {"Quiver upgrade", UPGRADES, 5, 8, "Carry up to 60 arrows instead of 30."},
        {"Powder upgrade", UPGRADES, 3, 8, "Carry up to 40 Magic Powder instead of 20."},
    };
    bit_rows(ups, 3);
    ammo("Bombs", BOMBS, 4, 30, "Bombs carried.");
    ammo("Arrows", ARROWS, 5, 30, "Arrows carried.");
    ammo("Magic Powder", POWDER, 3, 20, "Magic Powder carried.");

    opt_section("Ocarina songs");
    static const Bit songs[] = {
        {"Ballad of the Wind Fish", SONGS, 0, 8, "Marin's song."},
        {"Manbo's Mambo", SONGS, 1, 8, "Warps to Manbo's Pond or a warp point."},
        {"Frog's Song of Soul", SONGS, 2, 8, "Brings some things back to life."},
    };
    bit_rows(songs, 3);

    opt_section("Bottles");
    static const Bit bottles[] = {
        {"Bottle 1", ITEMS, 13, 32, "An empty bottle."}, {"Fairy in bottle 1", FAIRIES, 0, 8, "A fairy in bottle 1."},
        {"Bottle 2", ITEMS, 14, 32, "An empty bottle."}, {"Fairy in bottle 2", FAIRIES, 1, 8, "A fairy in bottle 2."},
        {"Bottle 3", ITEMS, 15, 32, "An empty bottle."}, {"Fairy in bottle 3", FAIRIES, 2, 8, "A fairy in bottle 3."},
    };
    bit_rows(bottles, 6);

    opt_section("Instruments of the Sirens");
    if (opt_button("All instruments", "Gives all eight instruments.")) F<uint32_t>(KEYITEMS) |= 0x7F80;
    static const Bit inst[] = {
        {"Full Moon Cello", KEYITEMS, 7, 32, "Tail Cave's instrument."},
        {"Conch Horn", KEYITEMS, 8, 32, "Bottle Grotto's instrument."},
        {"Sea Lily's Bell", KEYITEMS, 9, 32, "Key Cavern's instrument."},
        {"Surf Harp", KEYITEMS, 10, 32, "Angler's Tunnel's instrument."},
        {"Wind Marimba", KEYITEMS, 11, 32, "Catfish's Maw's instrument."},
        {"Coral Triangle", KEYITEMS, 12, 32, "Face Shrine's instrument."},
        {"Evening Calm Organ", KEYITEMS, 13, 32, "Eagle's Tower's instrument."},
        {"Thunder Drum", KEYITEMS, 14, 32, "Turtle Rock's instrument."},
    };
    bit_rows(inst, 8);

    opt_section("Dungeon keys");
    static const Bit keys[] = {
        {"Tail Key", KEYITEMS, 2, 32, "Opens Tail Cave."},
        {"Slime Key", KEYITEMS, 3, 32, "Opens Key Cavern."},
        {"Angler Key", KEYITEMS, 4, 32, "Opens Angler's Tunnel."},
        {"Face Key", KEYITEMS, 5, 32, "Opens Face Shrine."},
        {"Bird Key", KEYITEMS, 6, 32, "Opens Eagle's Tower."},
    };
    bit_rows(keys, 5);

    opt_section("Trading quest");
    {
        static const char *const trade[] = {"None", "Yoshi Doll", "Ribbon", "Dog Food", "Bananas", "Stick", "Honeycomb",
                                            "Pineapple", "Hibiscus", "Goat's Letter", "Broom", "Fishing Hook", "Necklace",
                                            "Mermaid's Scale", "Magnifying Lens"};
        int t = F<uint8_t>(TRADE);
        if (t > 14) t = 0;
        if (opt_choice("Trade item", &t, trade, 15, 0, "The trading quest item Link is carrying.")) F<uint8_t>(TRADE) = (uint8_t)t;
    }

    if (rando_seed_dir()) {  // randomizer seeds: companions are items (runtime/hle/pets.c)
        opt_section("Companions");
        static const struct { int c; const char *name; } comps[] = {{1, "BowWow"}, {2, "Marin"}, {4, "Flying Rooster"}, {3, "Ghost"}};
        for (const auto &k : comps) {
            bool on = pets_owned(k.c);
            if (opt_toggle(k.name, &on, false, "In this seed companions are items: owning one lets it follow Link."))
                pets_give(k.c, on);
        }
    }

    opt_section("Give any item");
    {   // the game's own AddItem (same path as an item pickup / Inventory.AddItemByKey event)
        static int item = 0, count = 1, index = -1;
        int n = (int)(sizeof kItemNames / sizeof kItemNames[0]);
        opt_choice("Item", &item, kItemNames, n, -1, "The item to give, through the game's own pickup.");
        opt_int("Count", &count, 1, 99, 1, 1, "%d", "How many to give.");
        opt_int("Index", &index, -1, 99, 1, -1, "%d",
                "Which one, for items with several: Pieces of Heart, Secret Seashells, Golden Leaves etc. each have "
                "an index.", "Any");
        if (opt_button("Give", "Gives the item above.")) give_item_request(item, count, index);
    }
    opt_end();
    sync_header();
}

// ---- debug helpers (LARECOMP_SAVE_DUMP_AT=<frame>: log the live save at that frame)
extern "C" void save_editor_file_read(const char *path, uint64_t addr, int64_t off, int64_t size) {
    (void)path; (void)addr; (void)off; (void)size;
}

extern "C" void cheats_frame(void);      // cheats.mm
extern "C" void difficulty_frame(void);  // difficulty.mm
extern "C" void save_editor_frame(uint64_t frame) {
    cheats_frame();
    difficulty_frame();
    static int watch = -1;  // LARECOMP_WATCH_SAVE=<lo>,<hi> (save offsets): log code touching that range
    if (watch < 0) {
        watch = 0;
        unsigned lo, hi;
        if (ENV("LARECOMP_WATCH_SAVE") && sscanf(ENV("LARECOMP_WATCH_SAVE"), "%x,%x", &lo, &hi) == 2) {
            extern uint64_t g_watch_lo, g_watch_hi;
            g_watch_lo = LIVE_SAVE + lo;
            g_watch_hi = LIVE_SAVE + hi;
            extern void watch_arm(void);
            watch_arm();
        }
    }
    static long test_at = -2;  // LARECOMP_SAVE_MAX_AT=<frame>: press "Max out everything" (test hook)
    if (test_at == -2) test_at = getenv("LARECOMP_SAVE_MAX_AT") ? atol(getenv("LARECOMP_SAVE_MAX_AT")) : -1;
    if (test_at >= 0 && (long)frame == test_at && loaded()) {
        F<uint16_t>(CONTAINERS) = 0x1FF; F<uint64_t>(PIECES) = 0xFFFFFFFFull; F<uint32_t>(RUPEES) = 999;
        F<uint64_t>(SHELLS) = low_bits(50); F<uint8_t>(LEAVES) = 0x1F; F<uint32_t>(ITEMS) |= 0xFFFF; F<uint8_t>(UPGRADES) |= 0x3F;
        F<uint8_t>(BOMBS) = 60; F<uint8_t>(ARROWS) = 60; F<uint8_t>(POWDER) = 40; F<uint8_t>(SONGS) = 7;
        F<uint32_t>(KEYITEMS) |= 0x7FFF; F<uint8_t>(SENSOR) |= 1; F<int16_t>(HP) = (int16_t)max_hp();
        sync_header();
        LOG("[save] test: maxed out\n");
    }
    static long poke_done;  // LARECOMP_POKE_SAVE=off=val,...: write live-save bytes (hex offsets) once after load
    if (!poke_done && loaded() && ENV("LARECOMP_POKE_SAVE") && frame > 2600) {
        poke_done = 1;
        for (const char *p = ENV("LARECOMP_POKE_SAVE"); *p;) {
            unsigned off = 0, val = 0;
            if (sscanf(p, "%x=%x", &off, &val) == 2 && off < 0x5EE0) *(uint8_t *)(uintptr_t)(LIVE_SAVE + off) = (uint8_t)val;
            while (*p && *p != ',') p++;
            if (*p) p++;
        }
        LOG("[save] poked %s\n", ENV("LARECOMP_POKE_SAVE"));
    }
    if (loaded()) {  // LARECOMP_LOG_FLAGS: log event-flag bit changes; LARECOMP_SET_FLAGS=i,j,..: force those bits on
        static uint8_t prev[256];
        static bool have_prev;
        uint8_t *fl = (uint8_t *)(uintptr_t)(LIVE_SAVE + 0x5DC0);
        if (const char *sf = ENV("LARECOMP_SET_FLAGS")) {
            for (const char *p = sf; *p;) {
                int i = atoi(p);
                if (i >= 0 && i < 2048 && !(fl[i >> 3] & (1 << (i & 7)))) {
                    fl[i >> 3] |= (uint8_t)(1 << (i & 7));
                    LOG("[flags] forced %d on\n", i);
                }
                while (*p && *p != ',') p++;
                if (*p) p++;
            }
        }
        if (ENV("LARECOMP_LOG_FLAGS")) {
            if (have_prev)
                for (int i = 0; i < 2048; i++)
                    if ((fl[i >> 3] ^ prev[i >> 3]) & (1 << (i & 7)))
                        LOG("[flags] frame %lu: flag %d -> %d\n", (unsigned long)frame, i, (fl[i >> 3] >> (i & 7)) & 1);
            memcpy(prev, fl, 256);
            have_prev = true;
        }
    }
    if (ENV("LARECOMP_FIND_FLAGS") && frame == 3000) {  // locate the event-flag bitfield (ptr, byte capacity)
        for (uint64_t a = 0x7001C00000ull; a < 0x7001D00000ull; a += 8) {
            uint64_t v = *(uint64_t *)(uintptr_t)a;
            if (v >= LIVE_SAVE && v < LIVE_SAVE + 0x5EE0) {
                uint32_t cap = *(uint32_t *)(uintptr_t)(a + 8);
                if (cap > 16 && cap < 0x1000) LOG("[flags] ptr at %llx -> save+%#llx, next u32 %u\n", (unsigned long long)a,
                                                  (unsigned long long)(v - LIVE_SAVE), cap);
            }
        }
    }
    static long at = -2;
    if (at == -2) at = getenv("LARECOMP_SAVE_DUMP_AT") ? atol(getenv("LARECOMP_SAVE_DUMP_AT")) : -1;
    if (at < 0 || (long)frame != at) return;
    const uint8_t *L = S();
    LOG("[save] live: version %u name %c%c%c%c rupees %u health %d items %08x keys %08x\n", *(const uint32_t *)L, L[0x5388],
        L[0x538a], L[0x538c], L[0x538e], *(const uint32_t *)(L + RUPEES), *(const int16_t *)(L + HP),
        *(const uint32_t *)(L + ITEMS), *(const uint32_t *)(L + KEYITEMS));
}
