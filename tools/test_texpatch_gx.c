// Skyward Sword HD's half of runtime/nvn/texpatch.c without the game: GX decoding (each format's tiles and channel
// order), finding the button textures in a layout archive and the picture font, matching an upload to them in each
// NVN form the game may convert them to (RGBA8 with 5-bit channels widened another way, RG8 either way round), and
// the redraw written back in that form. A fake RomFS holds one U8 archive (a TPL) and one BRFNT.
// clang -O1 -Iruntime -Iruntime/nvn tools/test_texpatch_gx.c runtime/nvn/prompt_icons.c runtime/nvn/tegra.c runtime/nvn/formats.c -lSDL3 -lm -o build/test_texpatch_gx && build/test_texpatch_gx
#include <assert.h>

#include "../runtime/nvn/texpatch.c"

// ------------------------------------------------------------------ the runtime around texpatch.c
Settings g_settings;
bool g_is_las;
const char *g_romfs_dir = "";
int16_t g_bind_key[BIND_KEY_COUNT];
int input_pad_family(void) { return 2; }
bool input_pad_connected(void) { return true; }
uint64_t renderer_present_count(void) { return 0; }
const char *rando_seed_dir(void) { return NULL; }
void astc_decode_ldr(const uint8_t *b, uint32_t w, uint32_t h, uint32_t bw, uint32_t bh, bool srgb, uint8_t *out) {
    (void)b, (void)w, (void)h, (void)bw, (void)bh, (void)srgb, (void)out;
}
int bind_prompt_host(int slot) {  // (the default bindings: by position)
    static const int8_t host[] = {[BIND_A] = 1, [BIND_B] = 0, [BIND_X] = 3, [BIND_Y] = 2, [BIND_L] = 9, [BIND_R] = 10,
                                  [BIND_ZL] = PADB_LTRIGGER, [BIND_ZR] = PADB_RTRIGGER, [BIND_PLUS] = 6, [BIND_MINUS] = 4};
    return host[slot];
}

static int g_fd;
static struct { const char *path; uint64_t off, size; } g_files[2];
bool romfs_find_file(const char *rel, int *out_fd, uint64_t *out_off, uint64_t *out_size) {
    for (int i = 0; i < 2; i++)
        if (g_files[i].path && !strcmp(g_files[i].path, rel)) {
            *out_fd = g_fd, *out_off = g_files[i].off, *out_size = g_files[i].size;
            return true;
        }
    return false;
}
void romfs_each(const char *prefix, void (*fn)(const char *path)) {
    for (int i = 0; i < 2; i++) if (!strncmp(g_files[i].path, prefix, strlen(prefix))) fn(g_files[i].path);
}

// ------------------------------------------------------------------ building GX data
static void be16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 8), p[1] = (uint8_t)v; }
static void be32(uint8_t *p, uint32_t v) { be16(p, v >> 16), be16(p + 2, v); }

static size_t enc_rgb5a3(const uint8_t *img, int w, int h, uint8_t *out) {  // (opaque: RGB555, else A3 RGB444)
    size_t o = 0;
    for (int ty = 0; ty < h; ty += 4)
        for (int tx = 0; tx < w; tx += 4)
            for (int i = 0; i < 16; i++, o += 2) {
                const uint8_t *p = img + ((ty + i / 4) * w + tx + i % 4) * 4;
                be16(out + o, p[3] == 255 ? 0x8000u | (p[0] >> 3) << 10 | (p[1] >> 3) << 5 | p[2] >> 3
                                          : (uint32_t)(p[3] >> 5) << 12 | (p[0] >> 4) << 8 | (p[1] >> 4) << 4 | p[2] >> 4);
            }
    return o;
}

static size_t enc_ia4(const uint8_t *img, int w, int h, uint8_t *out) {  // (intensity: red)
    size_t o = 0;
    for (int ty = 0; ty < h; ty += 4)
        for (int tx = 0; tx < w; tx += 8)
            for (int i = 0; i < 32; i++) {
                const uint8_t *p = img + ((ty + i / 8) * w + tx + i % 8) * 4;
                out[o++] = (uint8_t)((p[3] >> 4) << 4 | p[0] >> 4);
            }
    return o;
}

// A button glyph: a white disc with a dark X on it, at cx, cy in an RGBA8 image
static void glyph(uint8_t *img, int w, float cx, float cy, float r) {
    for (int y = 0; y < w; y++)
        for (int x = 0; x < w; x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy, a = fminf(fmaxf(r - hypotf(dx, dy) + 0.5f, 0), 1);
            if (a <= 0) continue;
            bool ink = fabsf(fabsf(dx) - fabsf(dy)) < r * 0.15f && fabsf(dx) < r * 0.5f;
            uint8_t *p = img + (y * w + x) * 4;
            p[0] = p[1] = p[2] = ink ? 60 : 250, p[3] = (uint8_t)lroundf(a * 255);
        }
}

// An upload: an NVN texture of `img` (RGBA8) in `kind`'s layout, stored block-linear like the game's
static NvnTexture upload(const uint8_t *img, int w, int h, int kind, uint8_t **lin_out) {
    static const uint32_t fmt[] = {[K_RGBA8] = 0x25, [K_RG8] = 0x0D, [K_GR8] = 0x0D};
    NvnTexture t = {.format = fmt[kind], .width = (uint32_t)w, .height = (uint32_t)h, .depth = 1, .levels = 1};
    t.layout = (TexLayout){.width = (uint32_t)w, .height = (uint32_t)h, .depth = 1, .layers = 1, .levels = 1,
                           .fmt = nvn_format_info(t.format)};
    tegra_layout_init(&t.layout);
    uint32_t bpb = t.layout.fmt->bpb;
    uint8_t *lin = malloc((size_t)w * h * bpb), *mem = calloc(tegra_total_size(&t.layout), 1);
    for (int i = 0; i < w * h; i++) {
        const uint8_t *p = img + i * 4;
        if (kind == K_RGBA8) memcpy(lin + i * 4, p, 4);
        else lin[i * 2 + (kind == K_GR8)] = p[0], lin[i * 2 + (kind != K_GR8)] = p[3];
    }
    LevelLayout l = tegra_level(&t.layout, 0);
    tegra_swizzle(lin, mem, l.wb, l.hb, 1, bpb, l.block_height_log2, l.block_depth_log2);
    t.addr = (uint64_t)(uintptr_t)mem;
    *lin_out = lin;
    return t;
}

int main(void) {
    setenv("LARECOMP_PROMPTS", "playstation", 1);

    // 1. each GX format's tiles: known bytes -> pixels
    uint8_t b[64] = {0}, *px;
    b[0] = 0x80, b[1] = 0x40;  // IA8: alpha, then intensity
    px = gx_decode(3, b, 32, 4, 4);
    assert(px[0] == 0x40 && px[1] == 0x40 && px[3] == 0x80);
    free(px);
    b[0] = 0x1F;  // I4: the high nibble first
    px = gx_decode(0, b, 32, 8, 8);
    assert(px[0] == 0x11 && px[3] == 0x11 && px[4] == 0xFF);
    free(px);
    b[0] = 0x3C;  // IA4: alpha in the high nibble
    px = gx_decode(2, b, 32, 8, 4);
    assert(px[0] == 0xCC && px[3] == 0x33);
    free(px);
    be16(b, 0xFC00), be16(b + 2, 0x7F00);  // RGB5A3: opaque red; then 3-bit alpha 7 + RGB444 red
    px = gx_decode(5, b, 32, 4, 4);
    assert(px[0] == 255 && px[1] == 0 && px[3] == 255 && px[4] == 255 && px[7] == 255);
    free(px);
    memset(b, 0, sizeof b);
    b[0] = 0x11, b[1] = 0x22, b[32] = 0x33, b[33] = 0x44;  // RGBA8: AR pairs, then GB pairs
    px = gx_decode(6, b, 64, 4, 4);
    assert(px[0] == 0x22 && px[1] == 0x33 && px[2] == 0x44 && px[3] == 0x11);
    free(px);
    memset(b, 0, 32);  // CMPR: sub-block 0 (top left) 3-colour mode, index 3 = clear; sub-block 1 (top right) red
    be16(b, 0), be16(b + 2, 0xFFFF), b[4] = 0xC0, be16(b + 8, 0xF800), be16(b + 10, 0);
    px = gx_decode(14, b, 32, 8, 8);
    assert(px[3] == 0 && px[(0 * 8 + 4) * 4] == 255 && px[(0 * 8 + 4) * 4 + 3] == 255 && px[4 * 4 + 3] == 255);
    free(px);

    // 2. a fake RomFS: Layout/Test.arc (U8: tr_xBtn_00.tpl, RGB5A3 48x48) and Font/picture_00.brfnt (IA4, 5 sheets)
    enum { W = 48, S = 256 };
    static uint8_t img[W * W * 4], sheet[S * S * 4];
    glyph(img, W, 24, 24, 19);
    glyph(sheet, S, 40, 40, 30);  // sheet 2, cell (0, 0): the d-pad, right lit
    for (int y = 175; y < 220; y++) for (int x = 175; x < 220; x++) sheet[(y * S + x) * 4] = 200, sheet[(y * S + x) * 4 + 3] = 255;
    static uint8_t rom[1 << 20];
    size_t n = 0;
    // U8: header, root + file node, names, then the TPL
    const char names[] = "\0tr_xBtn_00.tpl";
    uint32_t node_size = 2 * 12 + sizeof names, data = 0x20 + ((node_size + 31) & ~31u);
    be32(rom, 0x55AA382D), be32(rom + 4, 0x20), be32(rom + 8, node_size), be32(rom + 12, data);
    rom[0x20] = 1, be32(rom + 0x20 + 8, 2);  // root: a directory of 2 nodes
    be32(rom + 0x2C, 1), be32(rom + 0x2C + 4, data);  // file: name at 1, its data
    memcpy(rom + 0x38, names, sizeof names);
    uint8_t *tpl = rom + data;
    be32(tpl, 0x0020AF30), be32(tpl + 4, 1), be32(tpl + 8, 0x0C), be32(tpl + 0x0C, 0x14);
    be16(tpl + 0x14, W), be16(tpl + 0x16, W), be32(tpl + 0x18, 5), be32(tpl + 0x1C, 0x40);
    size_t tpl_size = 0x40 + enc_rgb5a3(img, W, W, tpl + 0x40);
    be32(rom + 0x2C + 8, (uint32_t)tpl_size);
    n = data + tpl_size;
    g_files[0].path = "Layout/Test.arc", g_files[0].off = 0, g_files[0].size = n;
    // BRFNT: header, TGLP (cells 79 x 79, 3 per row, 3 rows), the sheets from 0x40
    uint8_t *f = rom + n;
    memcpy(f, "RFNT", 4), be16(f + 4, 0xFEFF), be16(f + 0xC, 0x10);
    memcpy(f + 0x10, "TGLP", 4), be32(f + 0x14, 0x30 + 5 * S * S);
    f[0x18] = 79, f[0x19] = 79, be32(f + 0x1C, S * S), be16(f + 0x20, 5), be16(f + 0x22, 2);
    be16(f + 0x24, 3), be16(f + 0x26, 3), be16(f + 0x28, S), be16(f + 0x2A, S), be32(f + 0x2C, 0x40);
    enc_ia4(sheet, S, S, f + 0x40 + 2 * S * S);
    g_files[1].path = "Font/picture_00.brfnt", g_files[1].off = n, g_files[1].size = 0x40 + 5 * S * S;
    n += g_files[1].size;
    FILE *tmp = tmpfile();
    assert(tmp && fwrite(rom, 1, n, tmp) == n && !fflush(tmp));
    g_fd = fileno(tmp);

    // 3. the game's uploads: RGB5A3 widened as v * 255 / 31 (not bit replication), as RGBA8 and as RG8 either way
    static uint8_t game[W * W * 4];
    px = gx_decode(5, tpl + 0x40, tpl_size - 0x40, W, W);
    for (int i = 0; i < W * W; i++) {
        uint16_t v = rd16_be(tpl + 0x40 + 2 * (size_t)((i / W / 4 * (W / 4) + i % W / 4) * 16 + i / W % 4 * 4 + i % 4));
        for (int k = 0; k < 4; k++) game[i * 4 + k] = px[i * 4 + k];
        if (v & 0x8000) for (int k = 0; k < 3; k++) game[i * 4 + k] = (uint8_t)((v >> (10 - 5 * k) & 31) * 255 / 31);
    }
    free(px);
    int kinds[3] = {K_RGBA8, K_RG8, K_GR8};
    for (int k = 0; k < 3; k++) {
        uint8_t *lin;
        NvnTexture t = upload(game, W, W, kinds[k], &lin);
        int id = texpatch_identify(&t);
        assert(id >= 0 && g_refs[id].role == R_X && g_refs[id].kind == kinds[k]);
        uint32_t bpb = t.layout.fmt->bpb, ach = kinds[k] == K_RGBA8 ? 3 : kinds[k] == K_RG8 ? 1 : 0;
        texpatch_apply(id, 0, lin, W, W, W * bpb);
        assert(lin[(24 * W + 24) * bpb + ach] == 255 && lin[ach] == 0);  // (the new icon: its centre opaque, a corner clear)
        if (kinds[k] == K_RGBA8) {  // PlayStation's triangle (Switch X: north), in its green
            int green = 0;
            for (int i = 0; i < W * W; i++) green += lin[i * 4 + 1] > lin[i * 4] + 60;
            assert(green > 20);
        }
        free(lin);
        free((void *)(uintptr_t)t.addr);
    }
    uint8_t *lin;
    static uint8_t other[W * W * 4];
    glyph(other, W, 20, 26, 12);
    NvnTexture t = upload(other, W, W, K_RGBA8, &lin);
    assert(texpatch_identify(&t) < 0);  // (another texture of the same size)
    free(lin);
    free((void *)(uintptr_t)t.addr);

    // 4. the font, uploaded as one texture (its 5 sheets one under another): found from its TGLP, sheet 2's d-pad cell
    // (0, 0) redrawn, a cell with no button in it (2, 2) kept
    static uint8_t font[5 * S * S * 4];
    for (int i = 0; i < 5; i++) {
        px = gx_decode(2, f + 0x40 + (size_t)i * S * S, S * S, S, S);
        memcpy(font + (size_t)i * S * S * 4, px, (size_t)S * S * 4);
        free(px);
    }
    t = upload(font, S, 5 * S, K_RG8, &lin);
    int id = texpatch_identify(&t);
    assert(id >= 0 && g_refs[id].role == R_PICTURE && g_cell_w == 79 && g_pitch_x == 80 && g_sheet_h == S);
    uint8_t *before = malloc((size_t)5 * S * S * 2);
    memcpy(before, lin, (size_t)5 * S * S * 2);
    texpatch_apply(id, 0, lin, S, 5 * S, S * 2);
    int changed = 0;
    for (int y = 2 * S; y < 2 * S + 79; y++) for (int x = 0; x < 79; x++) changed += lin[(y * S + x) * 2] != before[(y * S + x) * 2];
    assert(changed > 1000);
    for (int y = 2 * S + 160; y < 2 * S + 239; y++)
        for (int x = 160; x < 239; x++) assert(!memcmp(lin + (y * S + x) * 2, before + (y * S + x) * 2, 2));
    printf("texpatch GX: formats decode, the TPL matches as RGBA8 / RG8 / GR8 and is redrawn, the font's d-pad cell too\n");
    return 0;
}
