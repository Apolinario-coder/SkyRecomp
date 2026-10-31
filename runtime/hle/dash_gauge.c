// The Loftwing's dash gauge with more than three wings (Gameplay tab "Dash charges" above 3; runtime/ui/difficulty.mm).
// The HUD gauge (Layout/DoButton.arc: birdGauge_00.brlyt, dLytBirdGauge_c) has three wing icons the game animates (a
// dash shrinks one to 0.8 and darkens it; a recharge brings it back) and never more: its code binds one animation per
// wing group. With more charges, the layout is patched as the RomFS is opened (runtime/services/fs.c), from the player's
// own file: copies of the third wing are added (with a lit copy of its material), the wings re-centred 50 apart and the
// base widened. The game doesn't animate the copies, so each frame, where the HUD reads the charges
// (tools/patches.py DASH_GAUGE_SITE), the runtime shows a copy beyond the charges left as used: shrunk and faded.
// The copies' panes are found as they are made (GAUGE_PICTURE_SITES). Takes effect when the game starts.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#include "../hle.h"
#include "../paths.h"

uint32_t rt_dash_max(void);  // runtime/ui/difficulty.mm

#define SPACING 50.0f
#define MAX_WINGS 7

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static void put32(uint8_t *p, uint32_t v) { p[0] = v >> 24, p[1] = v >> 16, p[2] = v >> 8, p[3] = v; }
static void put16(uint8_t *p, uint16_t v) { p[0] = v >> 8, p[1] = v; }
static void putf(uint8_t *p, float f) { uint32_t v; memcpy(&v, &f, 4); put32(p, v); }

typedef struct { uint8_t *p; size_t n, cap; } Buf;
static void add(Buf *b, const void *d, size_t n) {
    if (b->n + n > b->cap) b->p = realloc(b->p, b->cap = (b->n + n) * 2);
    memcpy(b->p + b->n, d, n);
    b->n += n;
}

static bool is_pane(const uint8_t *s, const char *magic, const char *name) {
    return !memcmp(s, magic, 4) && !strncmp((const char *)s + 0xC, name, 16);
}

// birdGauge_00.brlyt (big-endian, as in the RomFS) with `extra` more wings; NULL if it isn't the layout we know.
// Sections: header 0x10 (size at 8, count at 0xE); pane name at +0xC, translate at +0x24, size at +0x44; a picture's
// material index at +0x5C; mat1: count at +8, offsets (from the section) at +0xC, entries name[20] then tevColor[3]
// as 4 x s16 (tevColor1 at +0x1C: 140 used, 255 lit).
uint8_t *dash_gauge_brlyt(const uint8_t *in, size_t n, int extra, size_t *out_n) {
    if (n < 0x10 || memcmp(in, "RLYT", 4) || be32(in + 8) != n) return NULL;
    uint32_t nsec = be16(in + 0xE), hs = be16(in + 0xC);
    const uint8_t *sec[256];
    uint32_t len[256];
    if (nsec > 256) return NULL;
    for (uint32_t i = 0, o = hs; i < nsec; o += len[i++]) {
        if (o + 8 > n || (len[i] = be32(in + o + 4)) < 8 || o + len[i] > n) return NULL;
        sec[i] = in + o;
    }
    int imat = -1, iw = -1, iend = -1, mat_wing = -1;
    for (uint32_t i = 0; i < nsec; i++) {
        if (!memcmp(sec[i], "mat1", 4)) imat = (int)i;
        if (is_pane(sec[i], "pan1", "N_wing_02")) iw = (int)i;
        if (is_pane(sec[i], "pic1", "P_wing_02")) mat_wing = be16(sec[i] + 0x5C);
    }
    if (imat < 0 || iw < 0 || mat_wing < 0 || iw + 1 >= (int)nsec || memcmp(sec[iw + 1], "pas1", 4)) return NULL;
    for (int i = iw + 1, depth = 0; i < (int)nsec && iend < 0; i++) {
        depth += !memcmp(sec[i], "pas1", 4) - !memcmp(sec[i], "pae1", 4);
        if (!depth) iend = i;
    }
    uint32_t nmat = be16(sec[imat] + 8);
    if (iend < 0 || mat_wing >= (int)nmat) return NULL;
    int wings = 3 + extra;
    Buf b = {0};
    add(&b, in, hs);
    for (int i = 0; i < (int)nsec; i++) {
        const uint8_t *s = sec[i];
        if (i == imat) {  // + a lit copy of the third wing's material, last
            const uint8_t *m = s + be32(s + 0xC + 4 * mat_wing);
            uint32_t mlen = (mat_wing + 1 < (int)nmat ? be32(s + 0xC + 4 * (mat_wing + 1)) : len[i]) - (uint32_t)(m - s);
            size_t at = b.n;
            add(&b, s, 0xC);
            for (uint32_t k = 0; k < nmat; k++) {
                uint8_t o[4];
                put32(o, be32(s + 0xC + 4 * k) + 4);
                add(&b, o, 4);
            }
            uint8_t o[4];
            put32(o, len[i] + 4);
            add(&b, o, 4);
            add(&b, s + 0xC + 4 * nmat, len[i] - 0xC - 4 * nmat);
            size_t e = b.n;
            add(&b, m, mlen);
            memset(b.p + e, 0, 20);
            memcpy(b.p + e, "P_wing_lit", 10);
            for (int c = 0; c < 4; c++) put16(b.p + e + 0x1C + 2 * c, 255);
            put32(b.p + at + 4, (uint32_t)(b.n - at));
            put16(b.p + at + 8, (uint16_t)(nmat + 1));
            continue;
        }
        size_t at = b.n;
        add(&b, s, len[i]);
        for (int w = 0; w < 3; w++) {  // the game's wings, re-centred
            char name[16];
            snprintf(name, sizeof name, "N_wing_%02d", w);
            if (is_pane(s, "pan1", name)) putf(b.p + at + 0x24, (w - (wings - 1) / 2.0f) * SPACING);
        }
        if (is_pane(s, "pic1", "P_base_00") || is_pane(s, "pic1", "P_baseS_00"))
            putf(b.p + at + 0x44, 256.0f + extra * SPACING);
        if (i != iend) continue;
        for (int w = 3; w < wings; w++)  // the copies, after the third wing
            for (int j = iw; j <= iend; j++) {
                size_t c = b.n;
                add(&b, sec[j], len[j]);
                if (memcmp(sec[j], "pan1", 4) && memcmp(sec[j], "pic1", 4)) continue;
                char *nm = (char *)b.p + c + 0xC;
                size_t l = strnlen(nm, 16);
                if (l >= 2 && !strcmp(nm + l - 2, "02")) nm[l - 1] = (char)('0' + w);
                if (j == iw) putf(b.p + c + 0x24, (w - (wings - 1) / 2.0f) * SPACING);
                if (!memcmp(sec[j], "pic1", 4) && !strncmp(nm, "P_wing_0", 8)) put16(b.p + c + 0x5C, (uint16_t)nmat);
            }
    }
    put32(b.p + 8, (uint32_t)b.n);
    put16(b.p + 0xE, (uint16_t)(nsec + extra * (iend - iw + 1)));
    *out_n = b.n;
    return b.p;
}

// DoButton.arc (U8) with birdGauge_00.brlyt replaced: the new file goes at the end, its node pointed there.
static uint8_t *patch_arc(const uint8_t *arc, size_t n, int extra, size_t *out_n) {
    if (n < 0x20 || be32(arc) != 0x55AA382D) return NULL;
    uint32_t root = be32(arc + 4);
    if (root + 12 > n) return NULL;
    uint32_t count = be32(arc + root + 8), strtab = root + count * 12;
    if (strtab > n) return NULL;
    for (uint32_t i = 1; i < count; i++) {
        const uint8_t *node = arc + root + i * 12;
        uint32_t name = strtab + (be32(node) & 0xFFFFFF), off = be32(node + 4), size = be32(node + 8);
        if (node[0] || name >= n || strcmp((const char *)arc + name, "birdGauge_00.brlyt") || off + size > n) continue;
        size_t ln;
        uint8_t *lyt = dash_gauge_brlyt(arc + off, size, extra, &ln);
        if (!lyt) return NULL;
        size_t at = (n + 0x1F) & ~(size_t)0x1F;
        uint8_t *out = calloc(1, at + ln);
        memcpy(out, arc, n);
        memcpy(out + at, lyt, ln);
        free(lyt);
        put32(out + root + i * 12 + 4, (uint32_t)at);
        put32(out + root + i * 12 + 8, (uint32_t)ln);
        *out_n = at + ln;
        return out;
    }
    return NULL;
}

static int g_extra;  // copies in the layout this boot

// runtime/services/fs.c, for each RomFS file as it is opened: the patched DoButton.arc's host path, or false.
bool dash_gauge_override(int fd, uint64_t off, uint64_t size, const char *path, char *out, size_t cap) {
    size_t pl = strlen(path);
    if (!g_is_skyward || pl < 19 || strcmp(path + pl - 19, "Layout/DoButton.arc")) return false;
    g_extra = 0;
    int extra = (int)rt_dash_max() - 3;
    if (extra <= 0 || size > (64u << 20)) return false;  // (the game's own gauge: nothing to patch)
    uint8_t *arc = malloc(size);
    size_t n = 0;
    uint8_t *p = NULL;
    if (pread(fd, arc, size, (off_t)off) == (ssize_t)size) p = patch_arc(arc, size, extra, &n);
    free(arc);
    if (!p) { LOG("[gauge] DoButton.arc isn't the layout we know: the game's own gauge\n"); return false; }
    snprintf(out, cap, "%s/patched", rt_data_root());
    mkdir(out, 0755);
    snprintf(out, cap, "%s/patched/DoButton.arc", rt_data_root());
    FILE *f = fopen(out, "wb");
    bool ok = f && fwrite(p, 1, n, f) == n;
    if (f) fclose(f);
    free(p);
    if (ok) g_extra = extra, LOG("[gauge] dash gauge with %d wings\n", 3 + extra);
    return ok;
}

// nw4r::lyt::Pane (this build): scale (2 floats) at +0x70, alpha at +0xe8, flags at +0xeb, name[17] at +0xec.
static uint64_t g_wing[MAX_WINGS], g_wing_s[MAX_WINGS];
static bool g_checked[MAX_WINGS][2];
static int g_bad;

static bool named(uint64_t pane, const char *prefix, int w) {
    char name[17];
    snprintf(name, sizeof name, "%s%02d", prefix, w);
    return pane && !strncmp((const char *)(uintptr_t)(pane + 0xec), name, 17);
}

// A pane that just got its name (tools/patches.py GAUGE_PICTURE_SITES): keep our copies.
void rt_gauge_picture(uint64_t pane) {
    if (!g_extra) return;
    for (int w = 3; w < 3 + g_extra; w++) {
        if (named(pane, "P_wing_", w)) g_wing[w] = pane, g_checked[w][0] = false, LOG("[gauge] P_wing_%02d\n", w);
        if (named(pane, "P_wingS_", w)) g_wing_s[w] = pane, g_checked[w][1] = false;
    }
}

// The first time a copy is used, its scale and alpha are still the layout's own (nothing animates the copies): else
// the fields aren't where we think, and the copies are left alone.
static bool check(uint64_t pane, bool *checked, uint8_t alpha) {
    if (g_bad) return false;
    if (*checked) return true;
    float s;
    memcpy(&s, (const void *)(uintptr_t)(pane + 0x70), 4);
    uint8_t a = *(const uint8_t *)(uintptr_t)(pane + 0xe8);
    if (s != 0.8f || a != alpha) {
        LOG("[gauge] %s: scale %.2f alpha %u, not 0.80 / %u: copies left alone\n", (const char *)(uintptr_t)(pane + 0xec),
            s, a, alpha);
        g_bad = 1;
        return false;
    }
    return *checked = true;
}

// Each frame the HUD shows the gauge, with the Loftwing's charges (tools/patches.py DASH_GAUGE_SITE).
void rt_dash_gauge(int32_t charges) {
    static bool told;
    if (!told) told = true, LOG("[gauge] HUD reads %d charges (copies: %s)\n", charges, g_wing[3] ? "found" : "none");
    if (!g_extra || g_bad) return;
    for (int w = 3; w < 3 + g_extra; w++) {
        bool lit = charges > w;
        float s = lit ? 1.0f : 0.8f;
        if (named(g_wing[w], "P_wing_", w) && check(g_wing[w], &g_checked[w][0], 255)) {  // (still that pane)
            memcpy((void *)(uintptr_t)(g_wing[w] + 0x70), &s, 4);
            memcpy((void *)(uintptr_t)(g_wing[w] + 0x74), &s, 4);
            *(uint8_t *)(uintptr_t)(g_wing[w] + 0xe8) = lit ? 255 : 110;
        }
        if (named(g_wing_s[w], "P_wingS_", w) && check(g_wing_s[w], &g_checked[w][1], 80)) {
            memcpy((void *)(uintptr_t)(g_wing_s[w] + 0x70), &s, 4);
            memcpy((void *)(uintptr_t)(g_wing_s[w] + 0x74), &s, 4);
        }
    }
}
