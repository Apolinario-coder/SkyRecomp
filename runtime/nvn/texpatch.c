// Button prompts in the controller's own style, and the Hero Mode HUD icon hidden: edits made to the game's own UI
// textures as they are uploaded (ideas from the GameBanana mods "XBOX UI", "PS4 UI Buttons" and "No Hero Mode
// Indicator", credited in README.md; none of their files are used and no game art ships with the recomp).
//
// The prompt glyphs are 48x48 layout textures (BtnA_00, BtnL_01, ... in the UI archives' __Combined.bntx: BC5
// masks, BC3 / ASTC colour versions) and the pictogram font's sheet (pictogram.bffnt: RGBA8, the inline icons of
// message text). They are recognised by content: the user's own UI archives are read once and each texture the game
// uploads is compared with the known ones (same size, same bytes). A known glyph keeps the game's button shape and
// colours; only its label is redrawn: the old label (the pixels inside the shape that differ from the body colour)
// is painted over with the body colour and the new label (Xbox letters, PlayStation symbols, drawn here with a
// system font / simple geometry) composited in its place. Changed 4x4 blocks are re-encoded to the original format.
//
// Labels follow the controller bindings (runtime/hle/bindings.c; by position unless remapped): each Switch button's
// glyph names the controller button now driving it. Defaults: Switch A (east) = Xbox B / circle, B (south) = Xbox A /
// cross, X (north) = Y / triangle, Y (west) = X / square; L / R = LB / RB, L1 / R1; ZL / ZR = LT / RT, L2 / R2;
// Plus / Minus = Menu / View, Options / Create. A button remapped on a Nintendo-style pad gets Nintendo letters; one
// moved to a stick click / the d-pad gets LS / RS (L3 / R3) or an arrow. Stick glyphs stay. (The keyboard has no
// glyphs of its own: keyboard bindings don't change the prompts.)
#include "texpatch.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../hle.h"
#include "../input_bind.h"
#include "../rando.h"
#include "../settings.h"
#include "astc.h"
#include "tegra.h"

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "../../third_party/imgui/imstb_truetype.h"

enum { K_BC5, K_BC3, K_ASTC4, K_RGBA8, K_BC1 };
enum { R_A = 1, R_B, R_X, R_Y, R_L, R_R, R_ZL, R_ZR, R_PLUS, R_MINUS, R_HERO, R_SHEET, R_BOTTLE, R_BOTTLE_MODEL, R_FAIRY_MODEL,
       R_ITEM_CURSOR, R_TUNIC, R_TUNIC_RED, R_TUNIC_BLUE, R_HEART, R_POWDER_MODEL };
enum { STYLE_NINTENDO, STYLE_XBOX, STYLE_PS };
// labels: R_A..R_MINUS = the button that role is on by default; or these
enum { L_LS = 32, L_RS, L_DUP, L_DDOWN, L_DLEFT, L_DRIGHT, L_OTHER };

typedef struct {
    uint8_t role, kind, srgb;
    uint16_t w, h, levels;
    uint32_t size;
    uint8_t *data;  // the texture's (block-linear) bytes as stored in the game files
    char name[32];
} Ref;

static Ref g_refs[96];
static int g_nrefs;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;

// ------------------------------------------------------------------ game files (SARC / BNTX / BFFNT)
static uint8_t *read_file(const char *p, size_t *len) {
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc(*len ? *len : 1);
    if (fread(d, 1, *len, f) != *len) { free(d); d = NULL; }
    fclose(f);
    return d;
}

static uint8_t *read_all(const char *rel, size_t *len) {  // (the game's own files, never a seed's)
    char p[1200];
    snprintf(p, sizeof p, "%s/%s", g_romfs_dir ? g_romfs_dir : "extracted/romfs", rel);
    return read_file(p, len);
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t rd64(const uint8_t *p) { return rd32(p) | (uint64_t)rd32(p + 4) << 32; }

static int role_of(const char *name) {
    static const struct { const char *prefix; int role; } m[] = {
        {"BtnA_", R_A}, {"BtnB_", R_B}, {"BtnX_", R_X}, {"BtnY_", R_Y}, {"BtnL_", R_L}, {"BtnR_", R_R},
        {"BtnZL_", R_ZL}, {"BtnZR_", R_ZR}, {"BtnPlus_", R_PLUS}, {"BtnMinus_", R_MINUS},
        {"Spicy_00^t", R_HERO},  // Ld_Spicy (StartUp.arc): the HUD's Hero Mode icon (the file select uses Spicy_00^f)
        // the empty bottle's item icon and its 3D model's colour map (ItemBottle.bfres): identified only, never edited
        // (the gold Escape Bottle's draws are tinted: runtime/hle/escape.c)
        {"ItemBottle_00^w", R_BOTTLE}, {"MI_bottle_alb", R_BOTTLE_MODEL},
        // the fairy's colour map (ItemFairy.bfres): boss / mini-boss souls are a fairy drawn evil red (runtime/hle/souls.c)
        {"MI_fairy_alb", R_FAIRY_MODEL},
        // cosmetics: Link's tunics (recoloured on upload: green, and with a colour chosen the red / blue too, so the
        // Color Dungeon's tunics keep it - user 2026-10-11; the red map is also the recolour's mask)
        // and the hearts of Cm_HeartGauge (HUD, file select): Hp_00 = an empty heart (its own draw); each quarter of
        // a full one is one draw of Hp_01 (fill) + Hp_02 (frame, coloured by the layout's material) through the
        // Hp_04 mask; HeartUp = the life-up flash. Their draws are tinted.
        {"MI_body_alb.0", R_TUNIC}, {"MI_body_alb.1", R_TUNIC_RED}, {"MI_body_alb.2", R_TUNIC_BLUE},
        // the Magic Powder sack's colour map (ItemMagicPowder.bfres; after the tunic's: same name prefix): held up as
        // the Bomb Bag, tinted bomb blue then (runtime/hle/escape.c bomb_bag_tint)
        {"MI_body_alb", R_POWDER_MODEL},
        {"Hp_00^w", R_HEART}, {"Hp_01^w", R_HEART}, {"Hp_02^w", R_HEART}, {"HeartUp^w", R_HEART},
        // the Items page's cursor brackets: identified only (hidden while the companion card has the cursor, pets.c)
        {"ItemCursor_", R_ITEM_CURSOR},
    };
    for (unsigned i = 0; i < sizeof m / sizeof *m; i++)
        if (!strncmp(name, m[i].prefix, strlen(m[i].prefix))) return m[i].role;
    return 0;
}

static void add_ref(int role, int kind, int srgb, uint32_t w, uint32_t h, uint32_t levels, const uint8_t *data, uint32_t size,
                    const char *name) {
    for (int i = 0; i < g_nrefs; i++)  // (the archives share copies of the same textures)
        if (g_refs[i].w == w && g_refs[i].h == h && g_refs[i].size == size && !memcmp(g_refs[i].data, data, size)) return;
    if (g_nrefs == (int)(sizeof g_refs / sizeof *g_refs)) return;
    Ref *r = &g_refs[g_nrefs++];
    *r = (Ref){(uint8_t)role, (uint8_t)kind, (uint8_t)srgb, (uint16_t)w, (uint16_t)h, (uint16_t)levels, size, malloc(size), ""};
    memcpy(r->data, data, size);
    strlcpy(r->name, name, sizeof r->name);
}

// Textures of a BNTX (`b` .. `b + len`): the glyph ones (or, sheet = true, its only one: a font sheet).
static void scan_bntx(const uint8_t *b, size_t len, bool sheet) {
    if (len < 0x40 || memcmp(b, "BNTX", 4) || memcmp(b + 0x20, "NX  ", 4)) return;
    uint32_t count = rd32(b + 0x24);
    uint64_t infos = rd64(b + 0x28);
    for (uint32_t i = 0; i < count && infos + 8 * i + 8 <= len; i++) {
        uint64_t brti = rd64(b + infos + 8 * i);
        if (brti + 0x90 > len || memcmp(b + brti, "BRTI", 4)) continue;
        const uint8_t *t = b + brti + 16;
        uint64_t name_at = rd64(t + 80), ptrs = rd64(t + 96);
        char name[64] = "";
        if (name_at + 2 < len) {
            uint16_t n = rd16(b + name_at);
            if (n < sizeof name && name_at + 2 + n <= len) { memcpy(name, b + name_at + 2, n); name[n] = 0; }
        }
        int role = sheet ? R_SHEET : role_of(name);
        if (!role || ptrs + 8 > len) continue;
        uint32_t fmt = rd32(t + 12), w = rd32(t + 20), h = rd32(t + 24), size = rd32(t + 64), levels = rd16(t + 6);
        uint64_t data = rd64(b + ptrs);
        if (data + size > len) continue;
        int kind = (fmt >> 8) == 0x1e ? K_BC5 : (fmt >> 8) == 0x1c ? K_BC3 : (fmt >> 8) == 0x2d ? K_ASTC4 :
                   (fmt >> 8) == 0x0b ? K_RGBA8 : (fmt >> 8) == 0x1a ? K_BC1 : -1;
        if (kind < 0 || (sheet && kind != K_RGBA8)) continue;
        add_ref(role, kind, (fmt & 0xff) == 6, w, h, levels, b + data, size, name);
    }
}

static void scan_sarc(const char *rel) {
    size_t len;
    uint8_t *d = read_all(rel, &len);
    if (!d) return;
    if (len > 0x20 && !memcmp(d, "SARC", 4) && !memcmp(d + 0x14, "SFAT", 4)) {
        uint32_t data_off = rd32(d + 0xC);
        uint16_t n = rd16(d + 0x1A);
        const uint8_t *ent = d + 0x20, *names = d + 0x20 + 16 * n + 8;  // (SFNT header: 8 bytes)
        for (uint16_t i = 0; i < n && ent + 16 * (i + 1) <= d + len; i++) {
            uint32_t attr = rd32(ent + 16 * i + 4), s = rd32(ent + 16 * i + 8), e = rd32(ent + 16 * i + 12);
            const char *nm = (attr >> 24) ? (const char *)names + (attr & 0xffffff) * 4 : "";
            if ((const uint8_t *)nm >= d + len || strcmp(nm, "timg/__Combined.bntx") || data_off + e > len) continue;
            scan_bntx(d + data_off + s, e - s, false);
        }
    }
    free(d);
}

static void load_refs(void) {
    if (!g_is_las) return;
    scan_sarc("region_common/ui/Game.arc");
    scan_sarc("region_common/ui/StartUp.arc");
    scan_sarc("regionUS/common/ui/GameRegionCommon.arc");
    scan_sarc("regionEU/common/ui/GameRegionCommon.arc");
    size_t len;
    uint8_t *f;
    static const char *const models[] = {"region_common/actor/ItemBottle.bfres", "region_common/actor/Playerlink.bfres",
                                         "region_common/actor/ItemFairy.bfres", "region_common/actor/ItemMagicPowder.bfres"};
    for (int m = 0; m < 4; m++)
        if ((f = read_all(models[m], &len))) {  // (their textures: one BNTX inside)
            for (size_t i = 0; i + 8 < len; i += 4)
                if (!memcmp(f + i, "BNTX\0\0\0\0", 8)) { scan_bntx(f + i, len - i, false); break; }
            free(f);
        }
    f = read_all("region_common/ui/pictogram.bffnt", &len);  // message icons: one RGBA8 sheet in a BNTX
    if (f) {
        for (size_t i = 0; i + 8 < len; i += 4)
            if (!memcmp(f + i, "BNTX\0\0\0\0", 8)) { scan_bntx(f + i, len - i, true); break; }
        free(f);
    }
    LOG("[texpatch] %d known textures (button glyphs, pictogram sheet, Hero Mode icon, bottle icon / model, tunic, hearts)\n",
        g_nrefs);
}

int texpatch_bottle_kind(int id) {
    if (id < 0 || id >= g_nrefs) return 0;
    return g_refs[id].role == R_BOTTLE ? 1 : g_refs[id].role == R_BOTTLE_MODEL ? 2 : g_refs[id].role == R_FAIRY_MODEL ? 3
         : g_refs[id].role == R_POWDER_MODEL ? 4 : 0;
}

int texpatch_heart_kind(int id) {
    if (id < 0 || id >= g_nrefs) return 0;
    return g_refs[id].role == R_HEART;
}

static int ref_of(int role) {
    for (int i = 0; i < g_nrefs; i++) if (g_refs[i].role == role) return i;
    return -1;
}

// A seed made with a tunic colour (older seeds: tools/rando/cosmetics.py) ships its own recoloured Playerlink.bfres:
// its tunic map is recognised too, so the Cosmetics tab's colour wins over it. Read again when the seed changes
// (seeds switch in place).
static uint8_t *g_seed_tunic;
static uint32_t g_seed_tunic_size;
static char g_seed_tunic_dir[300] = "\1";
static const uint8_t *seed_tunic(uint32_t *size) {
    const char *dir = rando_seed_dir();
    if (strcmp(dir ? dir : "", g_seed_tunic_dir)) {
        strlcpy(g_seed_tunic_dir, dir ? dir : "", sizeof g_seed_tunic_dir);
        free(g_seed_tunic);
        g_seed_tunic = NULL;
        char p[600];
        size_t len;
        uint8_t *f = NULL;
        if (dir) snprintf(p, sizeof p, "%s/romfs/region_common/actor/Playerlink.bfres", dir), f = read_file(p, &len);
        int n0 = g_nrefs;
        for (size_t i = 0; f && i + 8 < len; i += 4)  // (a copy only differing from the game's adds a ref: taken out)
            if (!memcmp(f + i, "BNTX\0\0\0\0", 8)) { scan_bntx(f + i, len - i, false); break; }
        free(f);
        for (int i = n0; i < g_nrefs; i++) {
            if (g_refs[i].role == R_TUNIC && !g_seed_tunic) { g_seed_tunic = g_refs[i].data; g_seed_tunic_size = g_refs[i].size; }
            else free(g_refs[i].data);
        }
        g_nrefs = n0;
        if (g_seed_tunic) LOG("[texpatch] seed tunic map (%s)\n", dir);
    }
    *size = g_seed_tunic_size;
    return g_seed_tunic;
}

static TexLayout g_tunic_layout;  // (the game's tunic texture: its levels' places in the map's data)
static bool g_tunic_layout_ok;
bool texpatch_is_item_cursor(int id) { return id >= 0 && id < g_nrefs && g_refs[id].role == R_ITEM_CURSOR; }

int texpatch_identify(const NvnTexture *t) {
    pthread_once(&g_once, load_refs);
    if (!t->addr || t->depth > 1) return -1;
    uint64_t total = tegra_total_size(&t->layout);
    for (int i = 0; i < g_nrefs; i++) {
        const Ref *r = &g_refs[i];
        // (only the bottle model's and the tunic's maps have mip levels: compared in full, all levels)
        bool tunic = r->role == R_TUNIC || r->role == R_TUNIC_RED || r->role == R_TUNIC_BLUE;
        if (r->w != t->width || r->h != t->height || (r->role != R_POWDER_MODEL &&
            (t->levels > 1) != (r->role == R_BOTTLE_MODEL || r->role == R_FAIRY_MODEL || tunic))) continue;
        uint64_t n = r->size < total ? r->size : total;
        if (!memcmp((const void *)(uintptr_t)t->addr, r->data, n)) {
            static int nlog;
            if (nlog++ < 64) LOG("[texpatch] %s (%ux%u fmt %#x) uploaded\n", r->name, r->w, r->h, t->format);
            if (tunic) { g_tunic_layout = t->layout; g_tunic_layout_ok = true; }  // (all three: the same layout)
            return i;
        }
    }
    int tu = ref_of(R_TUNIC);
    if (tu >= 0 && t->levels > 1 && t->width == g_refs[tu].w && t->height == g_refs[tu].h) {
        uint32_t n;
        const uint8_t *s = seed_tunic(&n);
        if (s && n == g_refs[tu].size && n <= total && !memcmp((const void *)(uintptr_t)t->addr, s, n)) {
            LOG("[texpatch] seed's tunic map uploaded\n");
            g_tunic_layout = t->layout;
            g_tunic_layout_ok = true;
            return tu;
        }
    }
    return -1;
}

static int prompt_style(void) {
    // LARECOMP_PROMPTS=nintendo|xbox|playstation: test override, until frame LARECOMP_PROMPTS_UNTIL (then the setting)
    static int env = -2;
    static long until;
    if (env == -2) {
        const char *e = getenv("LARECOMP_PROMPTS");
        env = !e ? -1 : !strcmp(e, "xbox") ? PROMPTS_XBOX : !strcmp(e, "playstation") ? PROMPTS_PLAYSTATION :
              !strcmp(e, "nintendo") ? PROMPTS_NINTENDO : PROMPTS_AUTO;
        until = getenv("LARECOMP_PROMPTS_UNTIL") ? atol(getenv("LARECOMP_PROMPTS_UNTIL")) : -1;
    }
    extern uint64_t renderer_present_count(void);
    int p = env >= 0 && (until < 0 || (long)renderer_present_count() < until) ? env : g_settings.prompts;
    if (p == PROMPTS_XBOX) return STYLE_XBOX;
    if (p == PROMPTS_PLAYSTATION) return STYLE_PS;
    if (p == PROMPTS_NINTENDO) return STYLE_NINTENDO;
    extern int input_pad_family(void);  // runtime/hle/input.c: the controller SDL reports
    int f = input_pad_family();
    return f == 1 ? STYLE_XBOX : f == 2 ? STYLE_PS : STYLE_NINTENDO;
}

static bool hide_hero(void) {
    static int env = -2;  // LARECOMP_HIDE_HERO=0/1: test override
    if (env == -2) { const char *e = getenv("LARECOMP_HIDE_HERO"); env = e ? atoi(e) != 0 : -1; }
    return env >= 0 ? env : g_settings.hide_hero_icon;
}

// The label a glyph of `role` gets: whichever button is bound to that Switch input now
static int prompt_label(int role) {
    static const int8_t slot[] = {[R_A] = BIND_A, [R_B] = BIND_B, [R_X] = BIND_X, [R_Y] = BIND_Y, [R_L] = BIND_L,
                                  [R_R] = BIND_R, [R_ZL] = BIND_ZL, [R_ZR] = BIND_ZR, [R_PLUS] = BIND_PLUS,
                                  [R_MINUS] = BIND_MINUS};
    if (role < R_A || role > R_MINUS) return role;
    switch (bind_prompt_host(slot[role])) {
    case 1 /* east */: return R_A;
    case 0 /* south */: return R_B;
    case 3 /* north */: return R_X;
    case 2 /* west */: return R_Y;
    case 9 /* left shoulder */: return R_L;
    case 10 /* right shoulder */: return R_R;
    case PADB_LTRIGGER: return R_ZL;
    case PADB_RTRIGGER: return R_ZR;
    case 6 /* start */: return R_PLUS;
    case 4 /* back */: return R_MINUS;
    case 7: return L_LS;
    case 8: return L_RS;
    case 11: return L_DUP;
    case 12: return L_DDOWN;
    case 13: return L_DLEFT;
    case 14: return L_DRIGHT;
    default: return L_OTHER;
    }
}

static uint32_t labels_hash(void) {
    uint32_t h = 0;
    for (int r = R_A; r <= R_MINUS; r++) h = h * 41 + (uint32_t)(prompt_label(r) - r + 64);
    return h;
}

// (a Nintendo-style glyph is only redrawn when its button was remapped)
static bool restyled(int role, int style) { return style != STYLE_NINTENDO || prompt_label(role) != role; }

static void cosmetics_test(void);

uint32_t texpatch_generation(void) {  // (a rebind re-uploads the glyphs, like a style change; so does a tunic colour)
    cosmetics_test();
    return (1 + (uint32_t)prompt_style() + (hide_hero() ? 4 : 0) + (labels_hash() << 3)) ^ (uint32_t)g_settings.tunic * 0x9E3779B1u;
}

// ------------------------------------------------------------------ block codecs (BC4/BC5, BC1/BC3, ASTC 4x4)
static void bc4_decode(const uint8_t *b, uint8_t out[16]) {
    int e0 = b[0], e1 = b[1], pal[8] = {e0, e1};
    if (e0 > e1) for (int i = 1; i < 7; i++) pal[i + 1] = ((7 - i) * e0 + i * e1 + 3) / 7;
    else { for (int i = 1; i < 5; i++) pal[i + 1] = ((5 - i) * e0 + i * e1 + 2) / 5; pal[6] = 0; pal[7] = 255; }
    uint64_t bits = 0;
    for (int i = 0; i < 6; i++) bits |= (uint64_t)b[2 + i] << (8 * i);
    for (int i = 0; i < 16; i++) out[i] = (uint8_t)pal[(bits >> (3 * i)) & 7];
}

static void bc4_encode(const uint8_t v[16], uint8_t *b) {
    int lo = 255, hi = 0;
    for (int i = 0; i < 16; i++) { if (v[i] < lo) lo = v[i]; if (v[i] > hi) hi = v[i]; }
    uint64_t bits = 0;
    if (hi == lo) { b[0] = b[1] = (uint8_t)hi; }
    else {
        int pal[8] = {hi, lo};
        for (int i = 1; i < 7; i++) pal[i + 1] = ((7 - i) * hi + i * lo + 3) / 7;
        b[0] = (uint8_t)hi; b[1] = (uint8_t)lo;
        for (int i = 0; i < 16; i++) {
            int best = 0, be = 1 << 30;
            for (int k = 0; k < 8; k++) { int e = abs(pal[k] - v[i]); if (e < be) { be = e; best = k; } }
            bits |= (uint64_t)best << (3 * i);
        }
    }
    for (int i = 0; i < 6; i++) b[2 + i] = (uint8_t)(bits >> (8 * i));
}

static void rgb565(uint16_t c, int o[3]) {
    int r = c >> 11, g = (c >> 5) & 63, b = c & 31;
    o[0] = r << 3 | r >> 2; o[1] = g << 2 | g >> 4; o[2] = b << 3 | b >> 2;
}

static void bc1_decode4(const uint8_t *b, uint8_t out[16][4]) {  // (BC2/BC3 colour: always the 4-colour mode)
    int c[4][3];
    rgb565(rd16(b), c[0]); rgb565(rd16(b + 2), c[1]);
    for (int k = 0; k < 3; k++) { c[2][k] = (2 * c[0][k] + c[1][k]) / 3; c[3][k] = (c[0][k] + 2 * c[1][k]) / 3; }
    uint32_t idx = rd32(b + 4);
    for (int i = 0; i < 16; i++) for (int k = 0; k < 3; k++) out[i][k] = (uint8_t)c[(idx >> (2 * i)) & 3][k];
}

static uint16_t to565(const uint8_t *p) { return (uint16_t)((p[0] >> 3) << 11 | (p[1] >> 2) << 5 | p[2] >> 3); }

static void bc1_encode4(const uint8_t px[16][4], uint8_t *b) {
    int bi = 0, bj = 0, bd = -1;  // the two most distant colours of the block as endpoints
    for (int i = 0; i < 16; i++)
        for (int j = i + 1; j < 16; j++) {
            int d = 0;
            for (int k = 0; k < 3; k++) d += (px[i][k] - px[j][k]) * (px[i][k] - px[j][k]);
            if (d > bd) { bd = d; bi = i; bj = j; }
        }
    uint16_t c0 = to565(px[bi]), c1 = to565(px[bj]);
    if (c0 < c1) { uint16_t t = c0; c0 = c1; c1 = t; }
    int c[4][3];
    rgb565(c0, c[0]); rgb565(c1, c[1]);
    for (int k = 0; k < 3; k++) { c[2][k] = (2 * c[0][k] + c[1][k]) / 3; c[3][k] = (c[0][k] + 2 * c[1][k]) / 3; }
    uint32_t idx = 0;
    for (int i = 0; i < 16; i++) {
        int best = 0, be = 1 << 30;
        for (int s = 0; s < 4; s++) {
            int e = 0;
            for (int k = 0; k < 3; k++) e += (c[s][k] - px[i][k]) * (c[s][k] - px[i][k]);
            if (e < be) { be = e; best = s; }
        }
        idx |= (uint32_t)best << (2 * i);
    }
    b[0] = (uint8_t)c0; b[1] = (uint8_t)(c0 >> 8); b[2] = (uint8_t)c1; b[3] = (uint8_t)(c1 >> 8);
    for (int i = 0; i < 4; i++) b[4 + i] = (uint8_t)(idx >> (8 * i));
}

// ASTC 4x4: one partition, colour endpoint mode 12 (LDR RGBA direct) with 8-bit endpoints, a 4x4 grid of 2-bit
// weights (block mode 0x42: 11 + 2 + 4 + 64 endpoint + 32 weight bits). Only blocks we change are re-encoded.
static void put_bits(uint8_t *b, int at, int n, uint32_t v) {
    for (int i = 0; i < n; i++) if (v >> i & 1) b[(at + i) >> 3] |= (uint8_t)(1 << ((at + i) & 7));
}

static void astc_encode4(const uint8_t px[16][4], bool srgb, uint8_t *b) {
    int bi = 0, bj = 0, bd = -1;
    for (int i = 0; i < 16; i++)
        for (int j = i + 1; j < 16; j++) {
            int d = 0;
            for (int k = 0; k < 4; k++) d += (px[i][k] - px[j][k]) * (px[i][k] - px[j][k]);
            if (d > bd) { bd = d; bi = i; bj = j; }
        }
    const uint8_t *e0 = px[bi], *e1 = px[bj];
    if (e1[0] + e1[1] + e1[2] < e0[0] + e0[1] + e0[2]) { const uint8_t *t = e0; e0 = e1; e1 = t; }  // (no blue contraction)
    memset(b, 0, 16);
    put_bits(b, 0, 11, 0x42);
    put_bits(b, 13, 4, 12);
    for (int k = 0; k < 4; k++) { put_bits(b, 17 + 16 * k, 8, e0[k]); put_bits(b, 25 + 16 * k, 8, e1[k]); }
    static const int wq[4] = {0, 21, 43, 64};
    for (int i = 0; i < 16; i++) {
        int best = 0, be = 1 << 30;
        for (int q = 0; q < 4; q++) {
            int e = 0;
            for (int k = 0; k < 4; k++) {
                int c0 = srgb && k < 3 ? (e0[k] << 8 | 0x80) : e0[k] * 257, c1 = srgb && k < 3 ? (e1[k] << 8 | 0x80) : e1[k] * 257;
                int v = ((c0 * (64 - wq[q]) + c1 * wq[q] + 32) >> 6) >> 8;
                e += (v - px[i][k]) * (v - px[i][k]);
            }
            if (e < be) { be = e; best = q; }
        }
        if (best & 1) b[(127 - 2 * i) >> 3] |= (uint8_t)(1 << ((127 - 2 * i) & 7));  // weights: bit-reversed from the top
        if (best & 2) b[(126 - 2 * i) >> 3] |= (uint8_t)(1 << ((126 - 2 * i) & 7));
    }
}

// Level data <-> RGBA8 image (BC5: red, green -> R, G; alpha = green, the glyph's shape)
static void decode_image(const Ref *r, const uint8_t *src, uint32_t row, uint8_t *img) {
    uint32_t W = r->w, H = r->h, bw = (W + 3) / 4, bh = (H + 3) / 4;
    if (r->kind == K_RGBA8) { for (uint32_t y = 0; y < H; y++) memcpy(img + y * W * 4, src + y * row, W * 4); return; }
    if (r->kind == K_ASTC4) {
        uint8_t *blocks = malloc((size_t)bw * bh * 16);
        for (uint32_t y = 0; y < bh; y++) memcpy(blocks + y * bw * 16, src + y * row, bw * 16);
        astc_decode_ldr(blocks, W, H, 4, 4, r->srgb, img);
        free(blocks);
        return;
    }
    for (uint32_t by = 0; by < bh; by++)
        for (uint32_t bx = 0; bx < bw; bx++) {
            const uint8_t *b = src + by * row + bx * 16;
            uint8_t px[16][4] = {{0}}, a[16], c2[16];
            if (r->kind == K_BC5) {
                bc4_decode(b, a); bc4_decode(b + 8, c2);
                for (int i = 0; i < 16; i++) { px[i][0] = a[i]; px[i][1] = c2[i]; px[i][2] = 0; px[i][3] = c2[i]; }
            } else {
                bc4_decode(b, a); bc1_decode4(b + 8, px);
                for (int i = 0; i < 16; i++) px[i][3] = a[i];
            }
            for (int i = 0; i < 16; i++) {
                uint32_t x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                if (x < W && y < H) memcpy(img + (y * W + x) * 4, px[i], 4);
            }
        }
}

static void encode_image(const Ref *r, const uint8_t *img, const uint8_t *dirty, uint8_t *dst, uint32_t row) {
    uint32_t W = r->w, H = r->h, bw = (W + 3) / 4, bh = (H + 3) / 4;
    if (r->kind == K_RGBA8) { for (uint32_t y = 0; y < H; y++) memcpy(dst + y * row, img + y * W * 4, W * 4); return; }
    for (uint32_t by = 0; by < bh; by++)
        for (uint32_t bx = 0; bx < bw; bx++) {
            if (!dirty[by * bw + bx]) continue;
            uint8_t px[16][4], *b = dst + by * row + bx * 16;
            for (int i = 0; i < 16; i++) {
                uint32_t x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                if (x >= W) x = W - 1;
                if (y >= H) y = H - 1;
                memcpy(px[i], img + (y * W + x) * 4, 4);
            }
            if (r->kind == K_BC5) {
                uint8_t c0[16], c1[16];
                for (int i = 0; i < 16; i++) { c0[i] = px[i][0]; c1[i] = px[i][1]; }
                bc4_encode(c0, b); bc4_encode(c1, b + 8);
            } else if (r->kind == K_BC3) {
                uint8_t a[16];
                for (int i = 0; i < 16; i++) a[i] = px[i][3];
                bc4_encode(a, b); bc1_encode4(px, b + 8);
            } else {
                astc_encode4(px, r->srgb, b);
            }
        }
}

// ------------------------------------------------------------------ labels
static stbtt_fontinfo g_font;
static bool g_font_ok;
static uint8_t *g_font_data;

static void load_font(void) {  // a bold system font for the letters (none of the game's fonts are needed)
    static const char *const paths[] = {
        "/System/Library/Fonts/Supplemental/Arial Rounded Bold.ttf", "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
        "/Library/Fonts/Arial Bold.ttf", "C:/Windows/Fonts/arialbd.ttf", "C:/Windows/Fonts/segoeuib.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf",
    };
    for (unsigned i = 0; i < sizeof paths / sizeof *paths && !g_font_ok; i++) {
        FILE *f = fopen(paths[i], "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        g_font_data = malloc((size_t)n);
        if (fread(g_font_data, 1, (size_t)n, f) == (size_t)n && stbtt_InitFont(&g_font, g_font_data, stbtt_GetFontOffsetForIndex(g_font_data, 0)))
            g_font_ok = true;
        else { free(g_font_data); g_font_data = NULL; }
        fclose(f);
    }
    if (!g_font_ok) LOG("[texpatch] no system font for the button letters: Xbox letters stay Nintendo's\n");
}

#define CV 256  // label canvas (drawn large, then fitted into the glyph)

static float sat(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static float seg_dist(float px, float py, float ax, float ay, float bx, float by) {
    float vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay;
    float t = sat((wx * vx + wy * vy) / (vx * vx + vy * vy + 1e-6f));
    float dx = wx - t * vx, dy = wy - t * vy;
    return sqrtf(dx * dx + dy * dy);
}

typedef struct { float ax, ay, bx, by; } Seg;

static void draw_segs(float *cv, const Seg *s, int n, float r) {  // round-capped strokes
    for (int y = 0; y < CV; y++)
        for (int x = 0; x < CV; x++) {
            float d = 1e9f;
            for (int i = 0; i < n; i++) { float e = seg_dist(x + 0.5f, y + 0.5f, s[i].ax, s[i].ay, s[i].bx, s[i].by); if (e < d) d = e; }
            float a = sat(r - d + 0.5f);
            if (a > cv[y * CV + x]) cv[y * CV + x] = a;
        }
}

static void draw_ring(float *cv, float cx, float cy, float rad, float th) {
    for (int y = 0; y < CV; y++)
        for (int x = 0; x < CV; x++) {
            float d = hypotf(x + 0.5f - cx, y + 0.5f - cy);
            float a = sat(th / 2 - fabsf(d - rad) + 0.5f);
            if (a > cv[y * CV + x]) cv[y * CV + x] = a;
        }
}

static void draw_rect(float *cv, float x0, float y0, float x1, float y1, float th) {
    Seg s[4] = {{x0, y0, x1, y0}, {x1, y0, x1, y1}, {x1, y1, x0, y1}, {x0, y1, x0, y0}};
    draw_segs(cv, s, 4, th / 2);
}

static bool draw_text(float *cv, const char *text) {
    if (!g_font_ok) return false;
    float sc = stbtt_ScaleForPixelHeight(&g_font, 200);
    float x = 8;
    for (const char *p = text; *p; p++) {
        int adv, lsb, x0, y0, x1, y1;
        stbtt_GetCodepointHMetrics(&g_font, *p, &adv, &lsb);
        stbtt_GetCodepointBitmapBox(&g_font, *p, sc, sc, &x0, &y0, &x1, &y1);
        int w = x1 - x0, h = y1 - y0, ox = (int)x + x0, oy = 200 + y0;
        if (w > 0 && h > 0) {
            unsigned char *bm = malloc((size_t)w * h);
            stbtt_MakeCodepointBitmap(&g_font, bm, w, h, w, sc, sc, *p);
            for (int yy = 0; yy < h; yy++)
                for (int xx = 0; xx < w; xx++) {
                    int X = ox + xx, Y = oy + yy;
                    if (X >= 0 && X < CV && Y >= 0 && Y < CV) { float a = bm[yy * w + xx] / 255.0f; if (a > cv[Y * CV + X]) cv[Y * CV + X] = a; }
                }
            free(bm);
        }
        x += adv * sc;
        if (p[1]) x += stbtt_GetCodepointKernAdvance(&g_font, p[0], p[1]) * sc;
    }
    return true;
}

// The new label `role` (R_* / L_*, canvas coverage). symbol = true: a square pictogram (fitted by its size, not cap
// height).
static bool draw_label(int role, int style, float *cv, bool *symbol) {
    memset(cv, 0, sizeof(float) * CV * CV);
    *symbol = role == R_PLUS || role == R_MINUS || (style == STYLE_PS && role <= R_Y) || role >= L_DUP;
    if (role >= L_DUP && role <= L_DRIGHT) {  // d-pad: an arrow (drawn pointing up, then turned)
        static const float up[3][4] = {{128, 228, 128, 40}, {128, 34, 46, 116}, {128, 34, 210, 116}};
        Seg s[3];
        for (int i = 0; i < 3; i++) {
            float p[4];
            for (int k = 0; k < 4; k += 2) {
                float x = up[i][k] - 128, y = up[i][k + 1] - 128;
                float rx = role == L_DUP ? x : role == L_DDOWN ? -x : role == L_DLEFT ? y : -y;
                float ry = role == L_DUP ? y : role == L_DDOWN ? -y : role == L_DLEFT ? -x : x;
                p[k] = rx + 128, p[k + 1] = ry + 128;
            }
            s[i] = (Seg){p[0], p[1], p[2], p[3]};
        }
        draw_segs(cv, s, 3, 16);
        return true;
    }
    if (role == L_OTHER) { draw_ring(cv, 128, 128, 40, 80); return true; }  // a paddle / extra button: a dot
    if (role == L_LS || role == L_RS)
        return draw_text(cv, style == STYLE_PS ? (role == L_LS ? "L3" : "R3") : (role == L_LS ? "LS" : "RS"));
    if (style == STYLE_NINTENDO && role == R_PLUS) {
        Seg s[2] = {{128, 40, 128, 216}, {40, 128, 216, 128}};
        draw_segs(cv, s, 2, 18);
        return true;
    }
    if (style == STYLE_NINTENDO && role == R_MINUS) {
        Seg s[1] = {{40, 128, 216, 128}};
        draw_segs(cv, s, 1, 18);
        return true;
    }
    if (role == R_PLUS) {  // Menu / Options: three bars
        Seg s[3] = {{48, 64, 208, 64}, {48, 128, 208, 128}, {48, 192, 208, 192}};
        draw_segs(cv, s, 3, 15);
        return true;
    }
    if (role == R_MINUS) {
        if (style == STYLE_XBOX) {  // View: two overlapping windows
            draw_rect(cv, 92, 40, 220, 140, 22);
            for (int y = 104; y < 228; y++) for (int x = 24; x < 164; x++) cv[y * CV + x] = 0;  // (front window hides the back)
            draw_rect(cv, 36, 116, 152, 216, 22);
        } else {  // Create / Share: three rays
            Seg s[3] = {{128, 40, 128, 140}, {48, 92, 104, 164}, {208, 92, 152, 164}};
            draw_segs(cv, s, 3, 15);
            Seg d[1] = {{128, 206, 128, 206}};
            draw_segs(cv, d, 1, 20);
        }
        return true;
    }
    if (style == STYLE_PS && role <= R_Y) {
        if (role == R_A) draw_ring(cv, 128, 128, 96, 26);  // east: circle
        else if (role == R_B) { Seg s[2] = {{44, 44, 212, 212}, {212, 44, 44, 212}}; draw_segs(cv, s, 2, 14); }  // south: cross
        else if (role == R_X) { Seg s[3] = {{128, 30, 226, 200}, {226, 200, 30, 200}, {30, 200, 128, 30}}; draw_segs(cv, s, 3, 13); }  // north: triangle
        else draw_rect(cv, 44, 44, 212, 212, 26);  // west: square
        return true;
    }
    static const char *const xbox[] = {"", "B", "A", "Y", "X", "LB", "RB", "LT", "RT"};
    static const char *const ps[] = {"", "", "", "", "", "L1", "R1", "L2", "R2"};
    static const char *const nin[] = {"", "A", "B", "X", "Y", "L", "R", "ZL", "ZR"};
    if (role > R_ZR) return false;
    return draw_text(cv, style == STYLE_XBOX ? xbox[role] : style == STYLE_PS ? ps[role] : nin[role]);
}

// Relabel one glyph: img (W x H RGBA8, alpha channel `ach` = the shape), region x0,y0 w,h. flip: the region is
// stored upside down (font sheets).
static void relabel(uint8_t *img, int W, int x0, int y0, int w, int h, int ach, bool flip, int role, int style,
                    uint8_t *dirty, int bw) {
    int n = w * h;
    uint8_t *px = malloc((size_t)n * 4), *S = calloc((size_t)n, 1), *I = calloc((size_t)n, 1), *L = calloc((size_t)n, 1);
    int *stack = malloc(sizeof(int) * (size_t)n * 4);
    float *cv = malloc(sizeof(float) * CV * CV);
    for (int y = 0; y < h; y++) {
        int sy = flip ? h - 1 - y : y;
        memcpy(px + y * w * 4, img + ((y0 + sy) * W + x0) * 4, (size_t)w * 4);
    }
    // shape: alpha > 0.5 with its holes filled (the region outside it is what the border reaches)
    uint8_t *out = calloc((size_t)n, 1);
    int sp = 0;
    for (int i = 0; i < n; i++) {
        int x = i % w, y = i / w;
        if ((x == 0 || y == 0 || x == w - 1 || y == h - 1) && px[i * 4 + ach] <= 127) { out[i] = 1; stack[sp++] = i; }
    }
    while (sp) {
        int i = stack[--sp], x = i % w, y = i / w;
        const int nb[4] = {x > 0 ? i - 1 : -1, x < w - 1 ? i + 1 : -1, y > 0 ? i - w : -1, y < h - 1 ? i + w : -1};
        for (int k = 0; k < 4; k++)
            if (nb[k] >= 0 && !out[nb[k]] && px[nb[k] * 4 + ach] <= 127) { out[nb[k]] = 1; stack[sp++] = nb[k]; }
    }
    int sx0 = w, sy0 = h, sx1 = -1, sy1 = -1, area = 0;
    for (int i = 0; i < n; i++)
        if (!out[i]) {
            S[i] = 1, area++;
            int x = i % w, y = i / w;
            if (x < sx0) sx0 = x;
            if (x > sx1) sx1 = x;
            if (y < sy0) sy0 = y;
            if (y > sy1) sy1 = y;
        }
    if (area < 30) goto done;
    for (int i = 0; i < n; i++) {  // interior: 2 pixels in from the shape's edge
        int x = i % w, y = i / w, ok = S[i];
        for (int dy = -2; dy <= 2 && ok; dy++)
            for (int dx = -2; dx <= 2 && ok; dx++) {
                int xx = x + dx, yy = y + dy;
                ok = xx >= 0 && yy >= 0 && xx < w && yy < h && S[yy * w + xx];
            }
        I[i] = (uint8_t)ok;
    }
    // body colour = the most common colour inside, label colour = the most common one that clearly differs
    {
        static uint16_t hist[1 << 16];
        int best = -1, bc = 0;
        memset(hist, 0, sizeof hist);
        for (int i = 0; i < n; i++)
            if (I[i]) {
                const uint8_t *p = px + i * 4;
                int q = (p[0] >> 4) << 12 | (p[1] >> 4) << 8 | (p[2] >> 4) << 4 | p[3] >> 4;
                if (++hist[q] > bc) { bc = hist[q]; best = q; }
            }
        if (best < 0) goto done;
        float body[4] = {0}, lab[4] = {0};
        int nb = 0;
        for (int i = 0; i < n; i++) {
            const uint8_t *p = px + i * 4;
            if (I[i] && ((p[0] >> 4) << 12 | (p[1] >> 4) << 8 | (p[2] >> 4) << 4 | p[3] >> 4) == best) {
                for (int k = 0; k < 4; k++) body[k] += p[k];
                nb++;
            }
        }
        for (int k = 0; k < 4; k++) body[k] /= nb;
        memset(hist, 0, sizeof hist);
        best = -1, bc = 0;
        int lx0 = w, ly0 = h, lx1 = -1, ly1 = -1;
        for (int i = 0; i < n; i++) {
            const uint8_t *p = px + i * 4;
            float d = 0;
            for (int k = 0; k < 4; k++) d += fabsf(p[k] - body[k]);
            if (!I[i] || d < 64) continue;
            L[i] = 1;
            int x = i % w, y = i / w;
            if (x < lx0) lx0 = x;
            if (x > lx1) lx1 = x;
            if (y < ly0) ly0 = y;
            if (y > ly1) ly1 = y;
            int q = (p[0] >> 4) << 12 | (p[1] >> 4) << 8 | (p[2] >> 4) << 4 | p[3] >> 4;
            if (++hist[q] > bc) { bc = hist[q]; best = q; }
        }
        if (best < 0) goto done;
        int nl = 0;
        for (int i = 0; i < n; i++) {
            const uint8_t *p = px + i * 4;
            if (L[i] && ((p[0] >> 4) << 12 | (p[1] >> 4) << 8 | (p[2] >> 4) << 4 | p[3] >> 4) == best) {
                for (int k = 0; k < 4; k++) lab[k] += p[k];
                nl++;
            }
        }
        for (int k = 0; k < 4; k++) lab[k] /= nl;
        bool symbol;
        if (!draw_label(prompt_label(role), style, cv, &symbol)) goto done;
        // paint the old label (and its anti-aliased fringe) over with the body colour
        for (int i = 0; i < n; i++) {
            int x = i % w, y = i / w, near = 0, in = S[i];
            for (int dy = -2; dy <= 2 && !near; dy++)
                for (int dx = -2; dx <= 2 && !near; dx++) {
                    int xx = x + dx, yy = y + dy;
                    near = xx >= 0 && yy >= 0 && xx < w && yy < h && L[yy * w + xx];
                }
            for (int dy = -1; dy <= 1 && in; dy++)
                for (int dx = -1; dx <= 1 && in; dx++) {
                    int xx = x + dx, yy = y + dy;
                    in = xx >= 0 && yy >= 0 && xx < w && yy < h && S[yy * w + xx];
                }
            if (near && in) for (int k = 0; k < 4; k++) px[i * 4 + k] = (uint8_t)lroundf(body[k]);
        }
        // fit the new label: text at the old label's height (narrower than the shape), symbols by the old label's size
        int cx0 = CV, cy0 = CV, cx1 = -1, cy1 = -1;
        for (int i = 0; i < CV * CV; i++)
            if (cv[i] > 0.02f) {
                int x = i % CV, y = i / CV;
                if (x < cx0) cx0 = x;
                if (x > cx1) cx1 = x;
                if (y < cy0) cy0 = y;
                if (y > cy1) cy1 = y;
            }
        if (cx1 < 0) goto done;
        float lw = (float)(lx1 - lx0 + 1), lh = (float)(ly1 - ly0 + 1), iw = (float)(sx1 - sx0 + 1), ih = (float)(sy1 - sy0 + 1);
        float cw = (float)(cx1 - cx0 + 1), ch = (float)(cy1 - cy0 + 1);
        float tcx = (lx0 + lx1 + 1) / 2.0f, tcy = (ly0 + ly1 + 1) / 2.0f, th;
        if (symbol) {
            float s = fmaxf(lw, lh);
            if (role == R_PLUS || role == R_MINUS) s = lw;  // ("-" has almost no height)
            s = fminf(s, fminf(iw, ih) * 0.62f);
            th = s * ch / fmaxf(cw, ch);
            tcx = (sx0 + sx1 + 1) / 2.0f, tcy = (sy0 + sy1 + 1) / 2.0f;
        } else {
            th = role == R_PLUS || role == R_MINUS ? fminf(lw, ih * 0.6f) : lh;  // (remapped Plus / Minus: by "+"'s width)
            if (th * cw / ch > iw * 0.78f) th = iw * 0.78f * ch / cw;
        }
        float tw = th * cw / ch, k = ch / th;  // canvas pixels per glyph pixel
        float bx0 = tcx - tw / 2, by0 = tcy - th / 2;
        for (int y = (int)floorf(by0); y <= (int)ceilf(by0 + th); y++)
            for (int x = (int)floorf(bx0); x <= (int)ceilf(bx0 + tw); x++) {
                if (x < 0 || y < 0 || x >= w || y >= h || !S[y * w + x]) continue;
                float a = 0;  // 4x4 samples of the canvas over this pixel
                for (int sy = 0; sy < 4; sy++)
                    for (int sx = 0; sx < 4; sx++) {
                        float u = cx0 + (x + (sx + 0.5f) / 4 - bx0) * k, v = cy0 + (y + (sy + 0.5f) / 4 - by0) * k;
                        int iu = (int)u, iv = (int)v;
                        if (iu >= cx0 && iv >= cy0 && iu <= cx1 && iv <= cy1) a += cv[iv * CV + iu];
                    }
                a /= 16;
                if (a <= 0) continue;
                uint8_t *p = px + (y * w + x) * 4;
                for (int c = 0; c < 4; c++) p[c] = (uint8_t)lroundf(p[c] + (lab[c] - p[c]) * a);
            }
        // write back, marking changed blocks
        for (int y = 0; y < h; y++) {
            int sy = flip ? h - 1 - y : y;
            uint8_t *d = img + ((y0 + sy) * W + x0) * 4;
            for (int x = 0; x < w; x++)
                if (memcmp(d + x * 4, px + (y * w + x) * 4, 4)) {
                    memcpy(d + x * 4, px + (y * w + x) * 4, 4);
                    dirty[((y0 + sy) / 4) * bw + (x0 + x) / 4] = 1;
                }
        }
    }
done:
    free(px); free(S); free(I); free(L); free(stack); free(cv); free(out);
}

// ------------------------------------------------------------------ cosmetics (Cosmetics tab): tunic and heart colours
// Tunic: the colours tools/rando/cosmetics.py had as a seed option (same recolour, now live): where the green and
// red tunic maps differ (cloth, cap, collar ring) the green pixels get a hue / saturation / value change (PIL HSV,
// 0-255), keeping the shading. Every mip level is recoloured from the game's own green map, so a seed's own colour
// is replaced too; "Default" uploads whatever the files hold (green, or an older seed's colour).
const char *const g_tunic_colours[N_TUNIC_COLOURS] = {"Default", "Purple", "Orange", "Yellow", "Pink", "Teal", "Sky",
                                                      "Brown", "Black", "White", "Grey", "Lime", "Magenta"};
static const float k_tunic_hsv[N_TUNIC_COLOURS][3] = {  // hue (-1 = keep), saturation factor, value factor
    {0}, {195, 1.0f, 1.1f}, {20, 1.2f, 1.75f}, {40, 1.05f, 2.15f}, {232, 0.75f, 1.5f}, {125, 1.0f, 1.0f},
    {140, 0.8f, 1.45f}, {16, 0.75f, 0.8f}, {-1, 0.15f, 0.45f}, {-1, 0.06f, 2.1f}, {-1, 0.0f, 1.1f}, {70, 1.0f, 1.4f},
    {215, 1.0f, 1.15f}};
// Hearts: the colour a full heart gets (sRGB); empty hearts and the frame keep their shade of it (darker)
const char *const g_heart_colours[N_HEART_COLOURS] = {"Default", "Blue", "Green", "Purple", "Gold", "Orange", "Pink",
                                                      "Teal", "White"};
static const uint8_t k_heart_rgb[N_HEART_COLOURS][3] = {{0}, {40, 110, 255}, {40, 205, 60}, {170, 70, 255},
                                                        {255, 200, 30}, {255, 125, 20}, {255, 105, 185},
                                                        {20, 205, 195}, {240, 240, 240}};

// LARECOMP_COSMETICS=<frame>:<tunic>:<hearts>,...  test hook: set the colours (by name) at those frames
static void cosmetics_test(void) {
    static const char *next;
    static int init;
    if (!init) { init = 1; next = getenv("LARECOMP_COSMETICS"); }
    long at;
    char tu[32], he[32];
    if (!next || !*next || sscanf(next, "%ld:%31[^:]:%31[^,]", &at, tu, he) != 3) return;
    extern uint64_t renderer_present_count(void);
    if ((long)renderer_present_count() < at) return;
    for (int i = 0; i < N_TUNIC_COLOURS; i++) if (!strcmp(tu, g_tunic_colours[i])) g_settings.tunic = i;
    for (int i = 0; i < N_HEART_COLOURS; i++) if (!strcmp(he, g_heart_colours[i])) g_settings.hearts = i;
    LOG("[cosmetics] test: tunic %s, hearts %s\n", g_tunic_colours[g_settings.tunic], g_heart_colours[g_settings.hearts]);
    next = strchr(next, ',');
    if (next) next++;
}

static float srgb_lin(int c) { float v = c / 255.0f; return v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f); }

// Heart draws: the shader's luminance tint (translate.c `ltint`) maps the red's brightness onto the chosen colour,
// scaled so the full heart's red (255, 47, 47: linear luminance 0.319) becomes exactly that colour; the empty heart
// (92, 15, 15) and the frame's darker red keep their brightness relative to it.
void texpatch_heart_tint(int kind, float tint[8]) {
    cosmetics_test();
    int c = g_settings.hearts;
    if (c <= 0 || c >= N_HEART_COLOURS || !kind) return;
    for (int k = 0; k < 3; k++) tint[k] = srgb_lin(k_heart_rgb[c][k]) / 0.319f;
    tint[3] = 1;
    tint[4] = tint[5] = tint[6] = 0;
    tint[7] = 1;
}

static void bc1_decode(const uint8_t *b, uint8_t out[16][4]) {  // (BC1: 3-colour mode when c0 <= c1)
    uint16_t e0 = rd16(b), e1 = rd16(b + 2);
    int c[4][3];
    rgb565(e0, c[0]); rgb565(e1, c[1]);
    for (int k = 0; k < 3; k++) {
        if (e0 > e1) { c[2][k] = (2 * c[0][k] + c[1][k]) / 3; c[3][k] = (c[0][k] + 2 * c[1][k]) / 3; }
        else { c[2][k] = (c[0][k] + c[1][k]) / 2; c[3][k] = 0; }
    }
    uint32_t idx = rd32(b + 4);
    for (int i = 0; i < 16; i++) {
        int s = (idx >> (2 * i)) & 3;
        for (int k = 0; k < 3; k++) out[i][k] = (uint8_t)c[s][k];
        out[i][3] = e0 <= e1 && s == 3 ? 0 : 255;
    }
}

static void bc1_image(const uint8_t *lin, uint32_t w, uint32_t h, uint8_t *rgb) {  // linear BC1 blocks -> RGB
    uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4;
    for (uint32_t by = 0; by < bh; by++)
        for (uint32_t bx = 0; bx < bw; bx++) {
            uint8_t px[16][4];
            bc1_decode(lin + (by * bw + bx) * 8, px);
            for (int i = 0; i < 16; i++) {
                uint32_t x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                if (x < w && y < h) memcpy(rgb + (y * w + x) * 3, px[i], 3);
            }
        }
}

static uint8_t clamp8(float v) { return v < 0 ? 0 : v > 255 ? 255 : (uint8_t)v; }

static void hsv_shift(uint8_t *p, const float *m) {  // PIL's RGB <-> HSV (all 0-255), hue moved from green
    float r = p[0], g = p[1], b = p[2], mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b)), h = 0, s = 0, v = mx;
    if (mx > mn) {
        s = (mx - mn) * 255 / mx;
        float rc = (mx - r) / (mx - mn), gc = (mx - g) / (mx - mn), bc = (mx - b) / (mx - mn);
        h = r == mx ? bc - gc : g == mx ? 2 + rc - bc : 4 + gc - rc;
        h = fmodf(h / 6 + 1, 1) * 255;
    }
    if (m[0] >= 0) h = fmodf(floorf(h) + (m[0] - 100 /* the green tunic's hue */) + 256, 256);
    s = clamp8(s * m[1]); v = clamp8(v * m[2]);
    float hh = h / 255 * 6, f, q, t, pp, ss = s / 255;
    int i = (int)hh % 6;
    f = hh - floorf(hh); pp = v * (1 - ss); q = v * (1 - ss * f); t = v * (1 - ss * (1 - f));
    float o[6][3] = {{v, t, pp}, {q, v, pp}, {pp, v, t}, {pp, q, v}, {t, pp, v}, {v, pp, q}};
    for (int k = 0; k < 3; k++) p[k] = clamp8(o[i][k] + 0.5f);
}

// One mip level of the recoloured tunic (linear BC1 blocks), made on first use and kept for that colour
static uint8_t *g_tunic_lv[16];
static int g_tunic_lv_colour;
static const uint8_t *tunic_level(int colour, uint32_t lv) {
    if (colour != g_tunic_lv_colour) {
        for (int i = 0; i < 16; i++) { free(g_tunic_lv[i]); g_tunic_lv[i] = NULL; }
        g_tunic_lv_colour = colour;
    }
    int gi = ref_of(R_TUNIC), ri = ref_of(R_TUNIC_RED);
    if (lv >= 16 || lv >= g_tunic_layout.levels || gi < 0 || ri < 0 || g_refs[gi].size != g_refs[ri].size) return NULL;
    if (g_tunic_lv[lv]) return g_tunic_lv[lv];
    LevelLayout l = tegra_level(&g_tunic_layout, lv);
    if (l.offset + (uint64_t)l.wb * l.hb * 8 > g_refs[gi].size) return NULL;
    uint32_t W = l.w, H = l.h, n = W * H, nb = l.wb * l.hb;
    uint8_t *green = malloc((size_t)nb * 8), *red = malloc((size_t)nb * 8), *gimg = malloc((size_t)n * 3), *rimg = malloc((size_t)n * 3);
    float *mask = malloc(n * sizeof(float)), *tmp = malloc(n * sizeof(float));
    tegra_deswizzle(g_refs[gi].data + l.offset, green, l.wb, l.hb, 1, 8, l.block_height_log2, l.block_depth_log2);
    tegra_deswizzle(g_refs[ri].data + l.offset, red, l.wb, l.hb, 1, 8, l.block_height_log2, l.block_depth_log2);
    bc1_image(green, W, H, gimg);
    bc1_image(red, W, H, rimg);
    // mask: luma of the difference, (d - 12) * 6, Gaussian blur (sigma 1.2) - as cosmetics.py did
    for (uint32_t i = 0; i < n; i++) {
        int d[3];
        for (int k = 0; k < 3; k++) d[k] = abs(gimg[i * 3 + k] - rimg[i * 3 + k]);
        float lu = (d[0] * 299 + d[1] * 587 + d[2] * 114) / 1000.0f;
        mask[i] = fminf(fmaxf((lu - 12) * 6, 0), 255) / 255;
    }
    float kern[7], ks = 0;
    for (int i = -3; i <= 3; i++) ks += kern[i + 3] = expf(-(float)(i * i) / (2 * 1.2f * 1.2f));
    for (int pass = 0; pass < 2; pass++) {  // (separable: rows into tmp, then columns back)
        const float *src = pass ? tmp : mask;
        float *dst = pass ? mask : tmp;
        for (uint32_t y = 0; y < H; y++)
            for (uint32_t x = 0; x < W; x++) {
                float acc = 0;
                for (int i = -3; i <= 3; i++) {
                    int xx = (int)x + (pass ? 0 : i), yy = (int)y + (pass ? i : 0);
                    xx = xx < 0 ? 0 : xx >= (int)W ? (int)W - 1 : xx;
                    yy = yy < 0 ? 0 : yy >= (int)H ? (int)H - 1 : yy;
                    acc += src[yy * W + xx] * kern[i + 3];
                }
                dst[y * W + x] = acc / ks;
            }
    }
    uint8_t *out = malloc((size_t)nb * 8);
    memcpy(out, green, (size_t)nb * 8);
    for (uint32_t by = 0; by < l.hb; by++)
        for (uint32_t bx = 0; bx < l.wb; bx++) {
            uint8_t px[16][4];
            bool any = false;
            for (int i = 0; i < 16; i++) {
                uint32_t x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                x = x < W ? x : W - 1;
                y = y < H ? y : H - 1;
                uint32_t at = y * W + x;
                float m = mask[at];
                uint8_t c[3] = {gimg[at * 3], gimg[at * 3 + 1], gimg[at * 3 + 2]};
                if (m > 0.002f) {
                    any = true;
                    hsv_shift(c, k_tunic_hsv[colour]);
                    for (int k = 0; k < 3; k++) c[k] = clamp8(gimg[at * 3 + k] + (c[k] - gimg[at * 3 + k]) * m + 0.5f);
                }
                memcpy(px[i], c, 3);
                px[i][3] = 255;
            }
            if (any) bc1_encode4(px, out + (by * l.wb + bx) * 8);
        }
    free(green); free(red); free(gimg); free(rimg); free(mask); free(tmp);
    return g_tunic_lv[lv] = out;
}

static void tunic_apply(uint32_t lv, uint8_t *blocks, uint32_t w, uint32_t h, uint32_t row) {
    int c = g_settings.tunic;
    if (c <= 0 || c >= N_TUNIC_COLOURS || !g_tunic_layout_ok) return;
    const uint8_t *src = tunic_level(c, lv);
    uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4;
    if (!src || row < bw * 8) return;
    for (uint32_t y = 0; y < bh; y++) memcpy(blocks + y * row, src + y * bw * 8, bw * 8);
}

// pictogram.bffnt: glyph index -> role (its CMAP: '+' 0, '-' 1, 'A' 2, 'B' 3, 'L' 6, 'R' 8, 'X' 10, 'Y' 11, 'Z' 12
// = ZL, 'l' 13 / 'r' 15 = L / R (second style), 'z' 17 = ZR); 4 columns of 64x64 cells, rows from the bottom.
static const int8_t k_sheet_roles[18] = {R_PLUS, R_MINUS, R_A, R_B, 0, 0, R_L, 0, R_R, 0, R_X, R_Y, R_ZL, R_L, 0, R_R, 0, R_ZR};

void texpatch_apply(int id, uint32_t lv, uint8_t *blocks, uint32_t w, uint32_t h, uint32_t row) {
    if (id < 0 || id >= g_nrefs) return;
    const Ref *r = &g_refs[id];
    if (r->role == R_TUNIC || r->role == R_TUNIC_RED || r->role == R_TUNIC_BLUE) { tunic_apply(lv, blocks, w, h, row); return; }
    if (lv || r->w != w || r->h != h) return;
    if (r->role == R_BOTTLE || r->role == R_BOTTLE_MODEL || r->role == R_FAIRY_MODEL || r->role == R_POWDER_MODEL ||
        r->role == R_ITEM_CURSOR ||
        r->role == R_HEART) return;
    if (r->role == R_HERO) {  // fully transparent (BC5 blocks of zeros)
        if (hide_hero()) for (uint32_t y = 0; y < (h + 3) / 4; y++) memset(blocks + y * row, 0, (w + 3) / 4 * 16);
        return;
    }
    int style = prompt_style();
    if (style == STYLE_NINTENDO) {  // the game's own look, unless a button was remapped
        bool any = false;
        for (int rl = R_A; rl <= R_MINUS; rl++) any |= restyled(rl, style);
        if (!any) return;
    }
    static pthread_once_t font_once = PTHREAD_ONCE_INIT;
    pthread_once(&font_once, load_font);
    int bw = (int)(w + 3) / 4;
    uint8_t *img = malloc((size_t)w * h * 4), *dirty = calloc((size_t)bw * ((h + 3) / 4), 1);
    decode_image(r, blocks, row, img);
    int ach = r->kind == K_BC5 ? 1 : 3;
    if (r->role == R_SHEET) {
        for (int g = 0; g < (int)sizeof k_sheet_roles; g++) {
            int y0 = (int)h - (g / 4 + 1) * 64;
            if (k_sheet_roles[g] && y0 >= 0 && (g % 4 + 1) * 64 <= (int)w && restyled(k_sheet_roles[g], style))
                relabel(img, (int)w, (g % 4) * 64, y0, 64, 64, ach, true, k_sheet_roles[g], style, dirty, bw);
        }
    } else if (restyled(r->role, style)) {
        relabel(img, (int)w, 0, 0, (int)w, (int)h, ach, false, r->role, style, dirty, bw);
    }
    encode_image(r, img, dirty, blocks, row);
    free(img);
    free(dirty);
}
