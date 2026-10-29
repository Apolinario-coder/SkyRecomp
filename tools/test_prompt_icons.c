// Every button prompt icon (runtime/nvn/prompt_icons.c), one sheet per look (build/prompt_icons_light.png, _dark.png):
// each style on the game's parchment and on its dark boxes, the intensity-only (mono) look, the HUD's d-pad as its
// four panes, 44 px versions and the keyboard font. Fails if an icon draws nothing or runs off its texture.
// clang -O1 -Iruntime tools/test_prompt_icons.c -lSDL3 -lm -o build/test_prompt_icons && build/test_prompt_icons
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "../runtime/nvn/prompt_icons.c"

#define COLS 22
#define TILE 96

static uint8_t *g_sheet;
static int g_w, g_h;
static const uint8_t k_bg[2][3] = {{40, 44, 56}, {226, 214, 186}};  // the game's dark boxes, its parchment

static void tile(int col, int row, bool light) {
    for (int y = 0; y < TILE; y++)
        for (int x = 0; x < TILE; x++) memcpy(g_sheet + ((size_t)(row * TILE + y) * g_w + col * TILE + x) * 3, k_bg[light], 3);
}

// One icon, drawn the way texpatch.c does (its own size x size canvas, the glyph filling it), over tile col, row with
// its centre at cx, cy. Fails if it draws nothing or runs off its canvas.
static void icon(int col, int row, int cx, int cy, int size, int style, int label, const int keys[4], bool mono, bool dark) {
    float *px = calloc((size_t)size * size * 4, sizeof(float));
    prompt_icon(px, size, size, 0, 0, (float)size, (float)size, style, label, keys, mono, dark);
    float most = 0, edge = 0;
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            float a = px[(y * size + x) * 4 + 3];
            most = fmaxf(most, a);
            if (x == 0 || y == 0 || x == size - 1 || y == size - 1) edge = fmaxf(edge, a);
        }
    if (most < 0.9f || edge > 0.3f) {
        fprintf(stderr, "style %d label %d size %d: coverage %.2f, at the edge %.2f\n", style, label, size, most, edge);
        assert(0);
    }
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            int X = col * TILE + cx - size / 2 + x, Y = row * TILE + cy - size / 2 + y;
            uint8_t *d = g_sheet + ((size_t)Y * g_w + X) * 3;
            const float *s = px + (y * size + x) * 4;
            float lum = s[0] * 0.299f + s[1] * 0.587f + s[2] * 0.114f;
            for (int k = 0; k < 3; k++) d[k] = (uint8_t)lroundf(sat((mono ? lum : s[k]) + d[k] / 255.0f * (1 - s[3])) * 255);
        }
    free(px);
}

static void cell(int col, int row, int size, int style, int label, const int keys[4], bool mono, bool dark, bool light) {
    tile(col, row, light);
    icon(col, row, TILE / 2, TILE / 2, size, style, label, keys, mono, dark);
}

// ------------------------------------------------------------------ PNG (stored deflate: no zlib needed)
static uint32_t crc(uint32_t c, const uint8_t *p, size_t n) {
    c = ~c;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    }
    return ~c;
}

static void be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24), p[1] = (uint8_t)(v >> 16), p[2] = (uint8_t)(v >> 8), p[3] = (uint8_t)v; }

static void chunk(FILE *f, const char *type, const uint8_t *d, uint32_t n) {
    uint8_t h[8];
    be32(h, n);
    memcpy(h + 4, type, 4);
    fwrite(h, 1, 8, f);
    fwrite(d, 1, n, f);
    be32(h, crc(crc(0, (const uint8_t *)type, 4), d, n));
    fwrite(h, 1, 4, f);
}

static void write_png(const char *path, const uint8_t *rgb, int w, int h) {
    size_t row = (size_t)w * 3 + 1, raw_n = row * h, nblk = (raw_n + 65534) / 65535;
    uint8_t *raw = malloc(raw_n), *z = malloc(raw_n + nblk * 5 + 6);
    for (int y = 0; y < h; y++) { raw[y * row] = 0; memcpy(raw + y * row + 1, rgb + (size_t)y * w * 3, (size_t)w * 3); }
    size_t zn = 0;
    z[zn++] = 0x78, z[zn++] = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw_n; i++) a = (a + raw[i]) % 65521, b = (b + a) % 65521;
    for (size_t off = 0; off < raw_n; off += 65535) {
        uint16_t n = (uint16_t)(raw_n - off < 65535 ? raw_n - off : 65535);
        z[zn++] = off + n >= raw_n;
        z[zn++] = (uint8_t)n, z[zn++] = (uint8_t)(n >> 8), z[zn++] = (uint8_t)~n, z[zn++] = (uint8_t)(~n >> 8);
        memcpy(z + zn, raw + off, n);
        zn += n;
    }
    be32(z + zn, b << 16 | a);
    zn += 4;
    FILE *f = fopen(path, "wb");
    assert(f);
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    uint8_t ihdr[13] = {0};
    be32(ihdr, (uint32_t)w), be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8, ihdr[9] = 2;  // 8-bit RGB
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, (uint32_t)zn);
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
}

static void sheet(bool dark, const char *path) {
    static const int labels[COLS] = {R_A, R_B, R_X, R_Y, R_L, R_R, R_ZL, R_ZR, R_PLUS, R_MINUS, L_LS, L_RS,
                                     L_DUP, L_DDOWN, L_DLEFT, L_DRIGHT, L_DPAD, L_OTHER,
                                     L_ARM_UP, L_ARM_DOWN, L_ARM_LEFT, L_ARM_RIGHT};
    // the default keyboard bindings (runtime/hle/bindings.c), in label order; L_OTHER: Space
    static const int keys[COLS][4] = {
        {SDL_SCANCODE_K}, {SDL_SCANCODE_J}, {SDL_SCANCODE_I}, {SDL_SCANCODE_L}, {SDL_SCANCODE_Q}, {SDL_SCANCODE_E},
        {SDL_SCANCODE_1}, {SDL_SCANCODE_3}, {SDL_SCANCODE_RETURN}, {SDL_SCANCODE_BACKSPACE},
        {SDL_SCANCODE_W, SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_D},
        {SDL_SCANCODE_KP_8, SDL_SCANCODE_KP_4, SDL_SCANCODE_KP_5, SDL_SCANCODE_KP_6},
        {SDL_SCANCODE_UP}, {SDL_SCANCODE_DOWN}, {SDL_SCANCODE_LEFT}, {SDL_SCANCODE_RIGHT},
        {SDL_SCANCODE_UP, SDL_SCANCODE_LEFT, SDL_SCANCODE_DOWN, SDL_SCANCODE_RIGHT}, {SDL_SCANCODE_SPACE},
        {SDL_SCANCODE_UP}, {SDL_SCANCODE_DOWN}, {SDL_SCANCODE_LEFT}, {SDL_SCANCODE_RIGHT}};
    static const int extra[COLS] = {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_LCTRL, SDL_SCANCODE_LALT, SDL_SCANCODE_TAB,
                                    SDL_SCANCODE_ESCAPE, SDL_SCANCODE_CAPSLOCK, SDL_SCANCODE_F1, SDL_SCANCODE_F12,
                                    SDL_SCANCODE_PAGEUP, SDL_SCANCODE_DELETE, SDL_SCANCODE_SEMICOLON,
                                    SDL_SCANCODE_SLASH, SDL_SCANCODE_LEFTBRACKET, SDL_SCANCODE_EQUALS,
                                    512 + SDL_BUTTON_LEFT, 512 + SDL_BUTTON_RIGHT, 512 + SDL_BUTTON_MIDDLE,
                                    512 + SDL_BUTTON_X1, SDL_SCANCODE_SPACE, SDL_SCANCODE_KP_0, SDL_SCANCODE_F,
                                    SDL_SCANCODE_RETURN};
    int rows = 15;
    g_w = COLS * TILE, g_h = rows * TILE;
    g_sheet = calloc((size_t)g_w * g_h, 3);
    int row = 0;
    for (int light = 1; light >= 0; light--)
        for (int s = STYLE_XBOX; s <= STYLE_KEYBOARD; s++, row++)
            for (int c = 0; c < COLS; c++) cell(c, row, 88, s, labels[c], keys[c], false, dark, light);
    for (int c = 0; c < COLS; c++) cell(c, row, 88, STYLE_NINTENDO, labels[c], keys[c], false, dark, true);
    row++;
    for (int c = 0; c < COLS; c++) cell(c, row, 88, STYLE_PS, labels[c], keys[c], true, dark, true);  // (mono)
    row++;
    // the HUD's d-pad: four panes, one arm each, around a centre
    for (int c = 0; c < COLS; c++) tile(c, row, c >= 6 || c % 2);
    for (int c = 0; c < 6; c++) {
        static const int at[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
        int style = c / 2 == 0 ? STYLE_PS : c / 2 == 1 ? STYLE_XBOX : STYLE_KEYBOARD;
        for (int a = 0; a < 4; a++)
            icon(c, row, TILE / 2 + at[a][0] * 27, TILE / 2 + at[a][1] * 27, 36, style,
                 style == STYLE_KEYBOARD ? L_DUP + a : L_ARM_UP + a, keys[12 + a], false, dark);
    }
    row++;
    for (int s = STYLE_XBOX; s <= STYLE_KEYBOARD; s++, row++)
        for (int c = 0; c < COLS; c++) cell(c, row, 44, s, labels[c], keys[c], false, dark, s != STYLE_PS);
    for (int c = 0; c < COLS; c++) cell(c, row, 88, STYLE_KEYBOARD, R_A, (int[4]){extra[c]}, false, dark, false);
    row++;
    for (int i = 0; i < 36; i++) {
        int k = i < 26 ? SDL_SCANCODE_A + i : i == 26 ? SDL_SCANCODE_0 : SDL_SCANCODE_1 + i - 27;
        cell(i % COLS, row + i / COLS, 88, STYLE_KEYBOARD, R_A, (int[4]){k}, false, dark, true);
    }
    for (int i = 36; i < 2 * COLS; i++) tile(i % COLS, row + i / COLS, true);
    write_png(path, g_sheet, g_w, g_h);
    free(g_sheet);
    printf("%s: every icon drawn inside its texture\n", path);
}

int main(void) {
    sheet(false, "build/prompt_icons_light.png");
    sheet(true, "build/prompt_icons_dark.png");
    return 0;
}
