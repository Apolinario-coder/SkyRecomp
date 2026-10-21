// Button prompts in the controller's own style, and the Hero Mode HUD icon hidden: edits made to the game's own UI
// textures as they are uploaded (ideas from the GameBanana mods "XBOX UI", "PS4 UI Buttons" and "No Hero Mode
// Indicator", credited in README.md; none of their files are used and no game art ships with the recomp).
//
// The prompt glyphs are 48x48 layout textures (BtnA_00, BtnL_01, ... in the UI archives' __Combined.bntx: BC5
// masks, BC3 / ASTC colour versions) and the pictogram font's sheet (pictogram.bffnt: RGBA8, the inline icons of
// message text). They are recognised by content: the user's own UI archives are read once and each texture the game
// uploads is compared with the known ones (same size, same bytes). A known glyph is replaced whole: its region is
// cleared and a new icon (runtime/nvn/prompt_icons.c: Xbox, PlayStation, Nintendo and keyboard designs, no font
// files) drawn where the old one's shape was. Changed 4x4 blocks are re-encoded to the original format.
//
// Labels follow the controller bindings (runtime/hle/bindings.c; by position unless remapped): each Switch button's
// glyph names the controller button now driving it. Defaults: Switch A (east) = Xbox B / circle, B (south) = Xbox A /
// cross, X (north) = Y / triangle, Y (west) = X / square; L / R = LB / RB, L1 / R1; ZL / ZR = LT / RT, L2 / R2;
// Plus / Minus = Menu / View, Options / Create. A button remapped on a Nintendo-style pad gets Nintendo letters; one
// moved to a stick click / the d-pad gets LS / RS (L3 / R3) or a d-pad. Stick glyphs stay the game's on a controller
// (they animate the stick's tilt); the keyboard's show the keys bound to that stick. Keyboard prompts name the bound
// key (or mouse button).
#include "texpatch.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <SDL3/SDL.h>

#include "../hle.h"
#include "../input_bind.h"
#include "../platform.h"
#include "../rando.h"
#include "../settings.h"
#include "astc.h"
#include "prompt_icons.h"
#include "tegra.h"

enum { K_BC5, K_BC3, K_ASTC4, K_RGBA8, K_BC1, K_RG8 };
// roles: the prompt glyphs' R_A..R_MINUS / L_* (prompt_icons.h), then these
enum { R_HERO = R_MINUS + 1, R_SHEET, R_SHEET_1, R_SHEET_3, R_SHEET_4, R_BOTTLE, R_BOTTLE_MODEL, R_FAIRY_MODEL,
       R_ITEM_CURSOR, R_TUNIC, R_TUNIC_RED, R_TUNIC_BLUE, R_HEART, R_POWDER_MODEL, R_BASICINFO_BG };

typedef struct {
    uint8_t role, kind, srgb;
    uint16_t w, h, levels;
    uint32_t size;
    uint8_t *data;  // the texture's (block-linear) bytes as stored in the game files
    char name[32];
} Ref;

static Ref g_refs[512];
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
static uint16_t rd16_be(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t rd32_be(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3]; }

static int role_of(const char *name) {
    static const struct { const char *prefix; int role; } m[] = {
        {"BtnA_", R_A}, {"BtnB_", R_B}, {"BtnX_", R_X}, {"BtnY_", R_Y}, {"BtnL_", R_L}, {"BtnR_", R_R},
        {"BtnZL_", R_ZL}, {"BtnZR_", R_ZR}, {"BtnPlus_", R_PLUS}, {"BtnMinus_", R_MINUS},
        // Skyward Sword HD button names
        {"tr_aBtn_", R_A}, {"tr_bBtn_", R_B}, {"tr_xBtn_", R_X}, {"tr_yBtn_", R_Y},
        {"tr_lBtn_", R_L}, {"tr_rBtn_", R_R}, {"tr_rBtnSmall_", R_R},
        {"tr_zlBtn_", R_ZL}, {"tr_zrBtn_", R_ZR}, {"tr_zrBtnSmall_", R_ZR},
        {"tr_plusBtn_", R_PLUS}, {"tr_minusBtn_", R_MINUS}, {"tr_minusBtnSmall_", R_MINUS},
        {"tr_crossBtnBasicD_", L_DDOWN}, {"tr_crossBtnBasicL_", L_DLEFT},
        {"tr_crossBtnBasicR_", L_DRIGHT}, {"tr_crossBtnBasicT_", L_DUP},
        {"tr_crossBtnBasic_", L_DPAD},
        {"tr_lStick_", L_LS}, {"tr_rStick_", L_RS},
        {"th_zBtn_", R_ZL}, {"th_nunchakuBtn_", L_LS}, {"tr_nunchakuBtn_", L_LS}, {"tr_cBtn_", R_L},
        {"tr_basicInfoWinLT_", R_BASICINFO_BG},
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

static uint8_t *romfs_read_file(const char *path, size_t *out_len) {
    extern bool romfs_find_file(const char *rel, int *out_fd, uint64_t *out_off, uint64_t *out_size);
    int fd;
    uint64_t off, sz;
    if (!romfs_find_file(path, &fd, &off, &sz)) return NULL;
    uint8_t *d = malloc(sz ? sz : 1);
    if (!d) return NULL;
    if (pread(fd, d, sz, off) != (ssize_t)sz) { free(d); return NULL; }
    if (out_len) *out_len = (size_t)sz;
    return d;
}

static uint8_t *decode_tpl(const uint8_t *tpl, size_t len, uint32_t *out_w, uint32_t *out_h) {
    if (len < 0x28 || tpl[0] != 0x00 || tpl[1] != 0x20 || tpl[2] != 0xaf || tpl[3] != 0x30) return NULL;
    uint32_t tex_tab = rd32_be(tpl + 8);
    if (tex_tab + 4 > len) return NULL;
    uint32_t im_hdr = rd32_be(tpl + tex_tab);
    if (im_hdr + 12 > len) return NULL;
    uint16_t h = rd16_be(tpl + im_hdr), w = rd16_be(tpl + im_hdr + 2);
    uint32_t fmt = rd32_be(tpl + im_hdr + 4), d_off = rd32_be(tpl + im_hdr + 8);
    if (d_off >= len || !w || !h) return NULL;
    const uint8_t *raw = tpl + d_off;
    size_t raw_len = len - d_off;
    uint8_t *px = malloc((size_t)w * h * 4);
    if (!px) return NULL;
    memset(px, 0, (size_t)w * h * 4);
    if (fmt == 6) {  // RGBA8 (4x4 tiles of 32 bytes AR + 32 bytes GB)
        size_t src_idx = 0;
        for (uint32_t by = 0; by < h; by += 4) {
            for (uint32_t bx = 0; bx < w; bx += 4) {
                if (src_idx + 64 > raw_len) break;
                const uint8_t *ar = raw + src_idx, *gb = raw + src_idx + 32;
                src_idx += 64;
                int k = 0;
                for (uint32_t y = 0; y < 4; y++) {
                    for (uint32_t x = 0; x < 4; x++, k++) {
                        if (bx + x < w && by + y < h) {
                            uint8_t *p = px + ((by + y) * w + bx + x) * 4;
                            p[0] = ar[k * 2 + 1];  // R
                            p[1] = gb[k * 2];      // G
                            p[2] = gb[k * 2 + 1];  // B
                            p[3] = ar[k * 2];      // A
                        }
                    }
                }
            }
        }
    } else if (fmt == 3) {  // IA8
        size_t src_idx = 0;
        for (uint32_t by = 0; by < h; by += 4) {
            for (uint32_t bx = 0; bx < w; bx += 4) {
                if (src_idx + 32 > raw_len) break;
                for (uint32_t y = 0; y < 4; y++) {
                    for (uint32_t x = 0; x < 4; x++) {
                        if (bx + x < w && by + y < h) {
                            uint8_t a = raw[src_idx], i = raw[src_idx + 1];
                            uint8_t *p = px + ((by + y) * w + bx + x) * 4;
                            p[0] = i; p[1] = i; p[2] = i; p[3] = a;
                        }
                        src_idx += 2;
                    }
                }
            }
        }
    } else if (fmt == 5) {  // RGB5A3
        size_t src_idx = 0;
        for (uint32_t by = 0; by < h; by += 4) {
            for (uint32_t bx = 0; bx < w; bx += 4) {
                if (src_idx + 32 > raw_len) break;
                for (uint32_t y = 0; y < 4; y++) {
                    for (uint32_t x = 0; x < 4; x++) {
                        uint16_t v = (uint16_t)(raw[src_idx] << 8 | raw[src_idx + 1]);
                        src_idx += 2;
                        if (bx + x < w && by + y < h) {
                            uint8_t *p = px + ((by + y) * w + bx + x) * 4;
                            if (v & 0x8000) {
                                p[0] = ((v >> 10) & 0x1f) * 255 / 31;
                                p[1] = ((v >> 5) & 0x1f) * 255 / 31;
                                p[2] = (v & 0x1f) * 255 / 31;
                                p[3] = 255;
                            } else {
                                p[3] = ((v >> 12) & 7) * 255 / 7;
                                p[0] = ((v >> 8) & 0xf) * 255 / 15;
                                p[1] = ((v >> 4) & 0xf) * 255 / 15;
                                p[2] = (v & 0xf) * 255 / 15;
                            }
                        }
                    }
                }
            }
        }
    } else {
        free(px);
        return NULL;
    }
    *out_w = w;
    *out_h = h;
    return px;
}

static void scan_u8_arc(const uint8_t *b, size_t len) {
    if (len < 0x20 || b[0] != 0x55 || b[1] != 0xaa || b[2] != 0x38 || b[3] != 0x2d) return;
    uint32_t root_node = rd32_be(b + 4);
    if (root_node + 12 > len) return;
    uint32_t num_nodes = rd32_be(b + root_node + 8);
    uint32_t string_pool = root_node + num_nodes * 12;
    if (string_pool > len) return;
    for (uint32_t i = 1; i < num_nodes; i++) {
        const uint8_t *node = b + root_node + i * 12;
        if ((size_t)(root_node + i * 12 + 12) > len || node[0]) continue;  // directory
        uint32_t name_off = (uint32_t)(node[1] << 16 | node[2] << 8 | node[3]);
        if (string_pool + name_off >= len) continue;
        const char *name = (const char *)b + string_pool + name_off;
        int role = role_of(name);
        if (!role) continue;
        uint32_t data_off = rd32_be(node + 4), sz = rd32_be(node + 8);
        if (data_off + sz > len) continue;
        uint32_t w = 0, h = 0;
        uint8_t *rgba = decode_tpl(b + data_off, sz, &w, &h);
        if (rgba) {
            add_ref(role, K_RGBA8, 0, w, h, 1, rgba, w * h * 4, name);
            uint8_t *rg8 = malloc((size_t)w * h * 2);
            for (uint32_t p = 0; p < w * h; p++) {
                rg8[p * 2] = rgba[p * 4];          // Intensity
                rg8[p * 2 + 1] = rgba[p * 4 + 3];  // Alpha
            }
            add_ref(role, K_RG8, 0, w, h, 1, rg8, w * h * 2, name);
            free(rg8);
            free(rgba);
        }
    }
}

static void load_refs(void) {
    if (!g_is_las) {
        static const char *const arcs[] = {
            "Layout/DoButton.arc", "Layout/FileSelect.arc", "Layout/Shop.arc", "Layout/System2D.arc",
            "Layout/Map2D.arc", "Layout/MenuOptions.arc", "Layout/MenuPause.arc", "Layout/MenuHelp.arc",
            "Layout/SeekerStone.arc", "Layout/SoftwareKeyboard.arc", "Layout/Title2D.arc",
            "Layout/Main2D.arc", "Layout/CursorStick.arc", "Layout/CommonArrow.arc"
        };
        for (unsigned i = 0; i < sizeof arcs / sizeof *arcs; i++) {
            size_t len = 0;
            uint8_t *d = romfs_read_file(arcs[i], &len);
            if (d) {
                scan_u8_arc(d, len);
                free(d);
            }
        }
        // Load message box / dialogue font icons from Font/picture_00.brfnt
        size_t flen = 0;
        uint8_t *brfnt = romfs_read_file("Font/picture_00.brfnt", &flen);
        if (brfnt && flen > 0x50) {
            uint32_t sheet_off = 0x30 + rd32_be(brfnt + 0x30 + 28);
            uint32_t sheet_size = 131072;
            for (int s = 0; s < 5; s++) {
                if (sheet_off + (s + 1) * sheet_size <= flen) {
                    const uint8_t *raw = brfnt + sheet_off + s * sheet_size;
                    uint8_t *lin_rg8 = malloc(256 * 256 * 2);
                    uint8_t *lin_rgba = malloc(256 * 256 * 4);
                    size_t p = 0;
                    for (uint32_t by = 0; by < 256; by += 4) {
                        for (uint32_t bx = 0; bx < 256; bx += 4) {
                            for (uint32_t y = 0; y < 4; y++) {
                                for (uint32_t x = 0; x < 4; x++) {
                                    uint8_t a = raw[p], i = raw[p + 1];
                                    p += 2;
                                    uint32_t idx = (by + y) * 256 + bx + x;
                                    lin_rg8[idx * 2] = i;
                                    lin_rg8[idx * 2 + 1] = a;
                                    lin_rgba[idx * 4] = i;
                                    lin_rgba[idx * 4 + 1] = i;
                                    lin_rgba[idx * 4 + 2] = i;
                                    lin_rgba[idx * 4 + 3] = a;
                                }
                            }
                        }
                    }
                    int role = (s == 1) ? R_SHEET_1 : (s == 3) ? R_SHEET_3 : (s == 4) ? R_SHEET_4 : 0;
                    if (role) {
                        char nm[32];
                        snprintf(nm, sizeof nm, "picture_00_%d.brfnt", s);
                        add_ref(role, K_RG8, 0, 256, 256, 1, lin_rg8, 256 * 256 * 2, nm);
                        add_ref(role, K_RGBA8, 0, 256, 256, 1, lin_rgba, 256 * 256 * 4, nm);
                    }
                    free(lin_rg8);
                    free(lin_rgba);
                }
            }
            free(brfnt);
        }
        LOG("[texpatch] Skyward Sword HD: %d known button prompt textures\n", g_nrefs);
        return;
    }
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
    if (!g_is_las && t->width <= 256 && t->height <= 256 && t->layout.fmt) {
        uint32_t bpb = t->layout.fmt->bpb;
        if (bpb == 4 || bpb == 2) {
            uint8_t lin[256 * 256 * 4];
            LevelLayout l = tegra_level(&t->layout, 0);
            bool linear = (t->flags & 0x10) != 0;
            if (linear) {
                uint32_t row = l.wb * bpb;
                for (uint32_t y = 0; y < l.hb; y++)
                    memcpy(lin + (uint64_t)y * row, (const uint8_t *)(uintptr_t)t->addr + (uint64_t)y * t->stride, row);
            } else {
                tegra_deswizzle((const uint8_t *)(uintptr_t)t->addr, lin, l.wb, l.hb, 1, bpb, l.block_height_log2, l.block_depth_log2);
            }
            for (int i = 0; i < g_nrefs; i++) {
                const Ref *r = &g_refs[i];
                if (r->w == t->width && r->h == t->height) {
                    if (r->kind == K_RGBA8 && bpb == 4) {
                        if (!memcmp(lin, r->data, (size_t)t->width * t->height * 4)) {
                            static int nlog;
                            if (nlog++ < 64) LOG("[texpatch] %s (%ux%u role %d) identified\n", r->name, r->w, r->h, r->role);
                            return i;
                        }
                    } else if (r->kind == K_RG8 && bpb == 2) {
                        if (!memcmp(lin, r->data, (size_t)t->width * t->height * 2)) {
                            static int nlog;
                            if (nlog++ < 64) LOG("[texpatch] %s (%ux%u role %d) identified\n", r->name, r->w, r->h, r->role);
                            return i;
                        }
                    }
                }
            }
        }
    }
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
    // LARECOMP_PROMPTS=nintendo|xbox|playstation|keyboard: test override, until frame LARECOMP_PROMPTS_UNTIL (then the setting)
    static int env = -2;
    static long until;
    if (env == -2) {
        const char *e = getenv("LARECOMP_PROMPTS");
        env = !e ? -1 : !strcmp(e, "xbox") ? PROMPTS_XBOX : !strcmp(e, "playstation") ? PROMPTS_PLAYSTATION :
              !strcmp(e, "keyboard") ? PROMPTS_KEYBOARD :
              !strcmp(e, "nintendo") ? PROMPTS_NINTENDO : PROMPTS_AUTO;
        until = getenv("LARECOMP_PROMPTS_UNTIL") ? atol(getenv("LARECOMP_PROMPTS_UNTIL")) : -1;
    }
    extern uint64_t renderer_present_count(void);
    int p = env >= 0 && (until < 0 || (long)renderer_present_count() < until) ? env : g_settings.prompts;
    if (p == PROMPTS_XBOX) return STYLE_XBOX;
    if (p == PROMPTS_PLAYSTATION) return STYLE_PS;
    if (p == PROMPTS_KEYBOARD) return STYLE_KEYBOARD;
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

static int role_to_slot(int role) {
    switch (role) {
    case R_A: return BIND_A;
    case R_B: return BIND_B;
    case R_X: return BIND_X;
    case R_Y: return BIND_Y;
    case R_L: return BIND_L;
    case R_R: return BIND_R;
    case R_ZL: return BIND_ZL;
    case R_ZR: return BIND_ZR;
    case R_PLUS: return BIND_PLUS;
    case R_MINUS: return BIND_MINUS;
    case L_LS: return BIND_LSTICK;
    case L_RS: return BIND_RSTICK;
    case L_DUP: return BIND_UP;
    case L_DDOWN: return BIND_DOWN;
    case L_DLEFT: return BIND_LEFT;
    case L_DRIGHT: return BIND_RIGHT;
    default: return -1;
    }
}

static uint32_t labels_hash(void) {
    uint32_t h = 0;
    for (int r = R_A; r <= R_MINUS; r++) h = h * 41 + (uint32_t)(prompt_label(r) - r + 64);
    for (int i = 0; i < BIND_KEY_COUNT; i++) h = h * 31 + (uint32_t)g_bind_key[i];
    return h;
}

// (a Nintendo-style glyph is only redrawn when its button was remapped; a stick's only for the keyboard)
static bool restyled(int role, int style) {
    if (role == L_LS || role == L_RS) return style == STYLE_KEYBOARD;
    return style != STYLE_NINTENDO || prompt_label(role) != role;
}

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
    if (r->kind == K_RG8) {
        for (uint32_t y = 0; y < H; y++) {
            const uint8_t *s = src + y * row;
            uint8_t *d = img + y * W * 4;
            for (uint32_t x = 0; x < W; x++) {
                uint8_t i = s[x * 2], a = s[x * 2 + 1];
                d[x * 4] = i; d[x * 4 + 1] = i; d[x * 4 + 2] = i; d[x * 4 + 3] = a;
            }
        }
        return;
    }
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
    if (r->kind == K_RG8) {
        for (uint32_t y = 0; y < H; y++) {
            const uint8_t *s = img + y * W * 4;
            uint8_t *d = dst + y * row;
            for (uint32_t x = 0; x < W; x++) {
                uint8_t i = (uint8_t)((s[x * 4] * 299 + s[x * 4 + 1] * 587 + s[x * 4 + 2] * 114) / 1000);
                d[x * 2] = i;
                d[x * 2 + 1] = s[x * 4 + 3];
            }
        }
        return;
    }
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

// ------------------------------------------------------------------ icons
// Redraw one glyph: img (W x H RGBA8, alpha in channel `ach`), region x0, y0 w x h (flip: stored upside down, font
// sheets). The new icon fills the old one's shape (its alpha's bounds), and the rest of the region is cleared.
// mono: an intensity-only texture (RG8, BC5).
static void redraw(uint8_t *img, int W, int x0, int y0, int w, int h, int ach, bool flip, bool mono, int role, int style,
                   uint8_t *dirty, int bw) {
    int bx0 = w, by0 = h, bx1 = -1, by1 = -1, area = 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int sy = flip ? h - 1 - y : y;
            if (img[((y0 + sy) * W + x0 + x) * 4 + ach] <= 127) continue;
            area++;
            if (x < bx0) bx0 = x;
            if (x > bx1) bx1 = x;
            if (y < by0) by0 = y;
            if (y > by1) by1 = y;
        }
    if (area < 25) return;
    // keyboard: the keys bound to the glyph's input (a stick, the whole d-pad: up, left, down, right)
    static const int8_t four[3][4] = {{BIND_LS_UP, BIND_LS_LEFT, BIND_LS_DOWN, BIND_LS_RIGHT},
                                      {BIND_RS_UP, BIND_RS_LEFT, BIND_RS_DOWN, BIND_RS_RIGHT},
                                      {BIND_UP, BIND_LEFT, BIND_DOWN, BIND_RIGHT}};
    int keys[4] = {0}, k = role == L_LS ? 0 : role == L_RS ? 1 : role == L_DPAD ? 2 : -1;
    for (int i = 0; i < 4 && k >= 0; i++) keys[i] = g_bind_key[four[k][i]];
    if (k < 0 && role_to_slot(role) >= 0) keys[0] = g_bind_key[role_to_slot(role)];
    float *cv = calloc((size_t)w * h * 4, sizeof(float));
    prompt_icon(cv, w, h, (float)bx0, (float)by0, (float)(bx1 - bx0 + 1), (float)(by1 - by0 + 1), style,
                style == STYLE_KEYBOARD ? role : prompt_label(role), keys, mono);
    for (int y = 0; y < h; y++) {
        int sy = flip ? h - 1 - y : y;
        uint8_t *d = img + ((y0 + sy) * W + x0) * 4;
        for (int x = 0; x < w; x++) {
            const float *c = cv + ((size_t)y * w + x) * 4;
            uint8_t o[4] = {0}, a = (uint8_t)lroundf(c[3] * 255);
            for (int i = 0; i < 3 && a; i++) o[i] = (uint8_t)fminf(255, lroundf(c[i] / c[3] * 255));
            if (ach == 1) o[0] = (uint8_t)((o[0] * 299 + o[1] * 587 + o[2] * 114) / 1000), o[1] = a, o[2] = 0;  // (BC5)
            o[3] = a;
            if (memcmp(d + x * 4, o, 4)) {
                memcpy(d + x * 4, o, 4);
                dirty[((y0 + sy) / 4) * bw + (x0 + x) / 4] = 1;
            }
        }
    }
    free(cv);
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
    if (r->role == R_BASICINFO_BG) {
        int bw = (int)(w + 3) / 4;
        uint8_t *img = malloc((size_t)w * h * 4), *dirty = malloc((size_t)bw * ((h + 3) / 4));
        memset(img, 0, (size_t)w * h * 4);
        memset(dirty, 1, (size_t)bw * ((h + 3) / 4));
        encode_image(r, img, dirty, blocks, row);
        free(img);
        free(dirty);
        return;
    }
    int style = prompt_style();
    if (style == STYLE_NINTENDO) {  // the game's own look, unless a button was remapped
        bool any = false;
        for (int rl = R_A; rl <= R_MINUS; rl++) any |= restyled(rl, style);
        if (!any) return;
    }
    LOG("[texpatch] apply: %s (%dx%d role %d kind %d) style %d\n", r->name, w, h, r->role, r->kind, style);
    int bw = (int)(w + 3) / 4;
    uint8_t *img = malloc((size_t)w * h * 4), *dirty = calloc((size_t)bw * ((h + 3) / 4), 1);
    decode_image(r, blocks, row, img);
    int ach = r->kind == K_BC5 ? 1 : 3;
    bool mono = r->kind == K_BC5 || r->kind == K_RG8;
    if (r->role == R_SHEET_1) {
        if (restyled(R_B, style)) redraw(img, (int)w, 160, 0, 80, 80, ach, false, mono, R_B, style, dirty, bw);
        if (restyled(R_A, style)) redraw(img, (int)w, 0, 80, 80, 80, ach, false, mono, R_A, style, dirty, bw);
    } else if (r->role == R_SHEET_3) {
        if (restyled(R_L, style)) redraw(img, (int)w, 0, 0, 80, 80, ach, false, mono, R_L, style, dirty, bw);
        if (restyled(R_R, style)) redraw(img, (int)w, 80, 0, 80, 80, ach, false, mono, R_R, style, dirty, bw);
        if (restyled(R_ZL, style)) redraw(img, (int)w, 160, 0, 80, 80, ach, false, mono, R_ZL, style, dirty, bw);
        if (restyled(R_ZR, style)) redraw(img, (int)w, 0, 80, 80, 80, ach, false, mono, R_ZR, style, dirty, bw);
        if (restyled(R_X, style)) redraw(img, (int)w, 80, 80, 80, 80, ach, false, mono, R_X, style, dirty, bw);
        if (restyled(R_Y, style)) redraw(img, (int)w, 160, 80, 80, 80, ach, false, mono, R_Y, style, dirty, bw);
        if (restyled(R_PLUS, style)) redraw(img, (int)w, 0, 160, 80, 80, ach, false, mono, R_PLUS, style, dirty, bw);
        if (restyled(R_MINUS, style)) redraw(img, (int)w, 80, 160, 80, 80, ach, false, mono, R_MINUS, style, dirty, bw);
    } else if (r->role == R_SHEET_4) {
        if (restyled(L_LS, style)) redraw(img, (int)w, 0, 80, 80, 80, ach, false, mono, L_LS, style, dirty, bw);
        if (restyled(L_RS, style)) redraw(img, (int)w, 80, 80, 80, 80, ach, false, mono, L_RS, style, dirty, bw);
    } else if (r->role == R_SHEET) {
        for (int g = 0; g < (int)sizeof k_sheet_roles; g++) {
            int y0 = (int)h - (g / 4 + 1) * 64;
            if (k_sheet_roles[g] && y0 >= 0 && (g % 4 + 1) * 64 <= (int)w && restyled(k_sheet_roles[g], style))
                redraw(img, (int)w, (g % 4) * 64, y0, 64, 64, ach, true, mono, k_sheet_roles[g], style, dirty, bw);
        }
    } else if (restyled(r->role, style)) {
        redraw(img, (int)w, 0, 0, (int)w, (int)h, ach, false, mono, r->role, style, dirty, bw);
    }
    encode_image(r, img, dirty, blocks, row);
    free(img);
    free(dirty);
}
