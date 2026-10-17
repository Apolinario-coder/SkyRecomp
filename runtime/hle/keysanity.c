// Keysanity: dungeon items found anywhere. The game's AddItem (main+0x7e18b0, Items.gsheet row ids) adds small
// keys / Nightmare Key / map / compass / beak to the CURRENT dungeon only (outside one it does nothing). Seeds with
// keysanity give them with a dungeon index (upstream LAS dev convention: 0-7 = Tail Cave..Turtle Rock, 9 = Color
// Dungeon): this hook
// (tools/patches.py ADD_ITEM_SITE: the inner add main+0x8d08e0 that AddItem, chests and drops all call) applies those straight to that dungeon's save record.
// index -1 (vanilla behaviour, the item's own dungeon) is left to the game.
//
// Save record (notes/save_format.md): live save + 0xE0 + 0x10 * rec, dungeon n -> rec 2(n-1), Color Dungeon -> 18;
// +0 small keys held, +1 bits: compass 0, map 1, beak 2, Nightmare Key 3.
#include <stdio.h>

#include "../hle.h"

#define LIVE_SAVE 0x7001CBB220ull
// item ids as AddItem gets them: Items.gsheet `itemID` (not the row number; rows 41+ differ)
enum { IT_COMPASS = 44, IT_MAP = 45, IT_BEAK = 47, IT_SMALL_KEY = 48, IT_NIGHTMARE_KEY = 49 };

// Pot filler refills (tools/rando/pots.py REFILL_INDEX): bombs / magic powder / arrows only once Link has the bomb
// bag / powder / bow (a refill would otherwise unlock them - "bombs must be found first"); before that a blue rupee.
// Inventory bits: live save +0x5568 (notes/save_format.md): bit 2 bombs, bit 3 bow, bit 8 powder.
enum { IT_BOMB = 4, IT_POWDER = 11, IT_ARROW = 60, IT_RUPEE_BLUE = 102, REFILL_INDEX = 80 };

static int refill(Cpu *c, int item, int count) {
    uint32_t inv = *(const uint32_t *)(uintptr_t)(LIVE_SAVE + 0x5568);
    int have = item == IT_BOMB ? inv & (1u << 2) : item == IT_POWDER ? inv & (1u << 8) : inv & (1u << 3);
    if (!have) {
        c->x[0] = IT_RUPEE_BLUE;
        c->x[1] = 1;
    }
    c->x[2] = (uint64_t)(int64_t)-1;  // the game's own add from here on
    LOG("[pots] refill item %d x%d: %s\n", item, count, have ? "owned -> refill" : "not owned -> blue rupee");
    return 0;
}

// x0 item, x1 count, x2 index: returns 1 when handled (AddItem then returns at once)
int rt_add_item(Cpu *c) {
    int item = (int)(int32_t)c->x[0], count = (int)(int32_t)c->x[1], index = (int)(int32_t)c->x[2];
    extern int rt_shop_add_item(Cpu *);  // shopsanity.c: a stolen shopsanity good
    if (rt_shop_add_item(c)) return 1;
    if (index == REFILL_INDEX && (item == IT_BOMB || item == IT_POWDER || item == IT_ARROW)) return refill(c, item, count);
    extern int rt_pet_item(int n);  // pets.c: companions found as items (tools/rando/pets.py PET_INDEX 90)
    if (index >= 90 && index < 93) return rt_pet_item(index - 90);
    if (index < 0 || index > 9 || index == 8) return 0;
    int rec = index == 9 ? 18 : 2 * index;
    uint8_t *r = (uint8_t *)(uintptr_t)(LIVE_SAVE + 0xE0 + 0x10 * rec);
    switch (item) {
    case IT_SMALL_KEY: r[0] = (uint8_t)(r[0] + (count > 0 ? count : 1)); break;
    case IT_COMPASS: r[1] |= 1; break;
    case IT_MAP: r[1] |= 2; break;
    case IT_BEAK: r[1] |= 4; break;
    case IT_NIGHTMARE_KEY: r[1] |= 8; break;
    default: return 0;
    }
    // (the get-item message names the dungeon: seed text, tools/rando/keysanity_text.py)
    LOG("[keysanity] item %d x%d -> dungeon %d (record %d: keys %d bits %#x)\n", item, count, index, rec, r[0], r[1]);
    return 1;
}
