// Shopsanity / Trendsanity (tools/rando/shopsanity.py, trendsanity.py): the two places where the game hands out a
// randomized check's item from code, not from an event flow.
//   Stealing: the shopkeeper's exit check (main+0xa4a9b4) adds the carried good's item (good +0x488) through the
//   inner AddItem (keysanity.c rt_add_item). A shopsanity good carries a dummy item (Items.gsheet "LARShop<n>",
//   itemID 230+); here it sets that check's flags instead (bought/stolen + "give on the way out") and adds nothing.
//   PlayerStart's ExitOutShop then gives the real item with its item-get animation.
//   Crane prizes: AddPrizeByKey (main+0x7e1c70) looks the prize row up by symbol and calls AddPrize(row, count,
//   index) (main+0x1489c0), which knows rows 0-20 only. A Trendsanity prize row (21+) sets its check's flags
//   (won + "give now") for PrizeCommon's flow; a row that replaces a figure still adds that figure (house shelf,
//   the next figure's condition, the BowWow figure's final prize).
// rando/<seed>/shopsanity.tsv: "item <itemID> <done flag> <give flag>" / "prize <row> <done flag> <give flag> <row
// to add instead, 255 = nothing>" (GlobalFlags indices).
#include <stdio.h>
#include <string.h>

#include "../hle.h"
#include "../rando.h"

#define FLAG_PTR (MAIN_BASE + 0x1CC1388)  // the event-flag bitfield (runtime/hle/flags_ext.c)
#define FLAG_CAP (MAIN_BASE + 0x1CC1390)

typedef struct { int kind, id, done, give, add; } Entry;  // kind 0 item, 1 prize
static Entry g_e[64];
static int g_n = -1;

static void load(void) {
    g_n = 0;
    const char *dir = rando_seed_dir();
    if (!dir) return;
    char p[600];
    snprintf(p, sizeof p, "%s/shopsanity.tsv", dir);
    FILE *f = fopen(p, "r");
    if (!f) return;
    char kind[16];
    Entry e = {0};
    while (g_n < 64 && fscanf(f, "%15s %d %d %d", kind, &e.id, &e.done, &e.give) == 4) {
        e.kind = !strcmp(kind, "prize");
        e.add = 255;
        if (e.kind && fscanf(f, "%d", &e.add) != 1) break;
        g_e[g_n++] = e;
    }
    fclose(f);
    LOG("[shopsanity] %d entries from %s\n", g_n, p);
}

static void set_flag(int idx) {
    uint64_t p = LD64(FLAG_PTR);
    if (!p || idx < 0 || (uint32_t)(idx >> 3) >= LD32(FLAG_CAP)) return;
    ST8(p + (idx >> 3), LD8(p + (idx >> 3)) | (1u << (idx & 7)));
}

static const Entry *find(int kind, int id) {
    if (g_n < 0) load();
    for (int i = 0; i < g_n; i++)
        if (g_e[i].kind == kind && g_e[i].id == id) return &g_e[i];
    return NULL;
}

// from rt_add_item (x0 item, x1 count, x2 index): 1 = handled (nothing added)
int rt_shop_add_item(Cpu *c) {
    const Entry *e = find(0, (int)(uint8_t)c->x[0]);
    if (!e || (int)(uint32_t)c->x[0] != e->id) return 0;
    set_flag(e->done);
    set_flag(e->give);
    LOG("[shopsanity] stolen good (item %d): flags %d %d\n", e->id, e->done, e->give);
    return 1;
}

// tools/patches.py PRIZE_ADD_SITE, before AddPrize(x0 row, x1 count, x2 index)
void rt_add_prize(Cpu *c) {
    const Entry *e = find(1, (int)(uint8_t)c->x[0]);
    if (!e) return;
    set_flag(e->done);
    set_flag(e->give);
    LOG("[trendsanity] prize row %d won: flags %d %d, adds row %d\n", e->id, e->done, e->give, e->add);
    c->x[0] = (uint64_t)e->add;
}

RT_ON_RESET(shopsanity_reset) { g_n = -1; }  // (seed switch: reload the seed's table)
