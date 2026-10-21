// Every button prompt icon (runtime/nvn/prompt_icons.c) on one sheet, build/prompt_icons.png: each style on a dark
// and a light tile, the intensity-only (mono) look, 44 px versions, and the keyboard font. Fails if an icon draws
// nothing or runs off its texture.
// clang -O1 -Iruntime tools/test_prompt_icons.c -lSDL3 -lm -o build/test_prompt_icons && build/test_prompt_icons
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "../runtime/nvn/prompt_icons.c"

#define COLS 18
#define TILE 96

static uint8_t *g_sheet;
static int g_w, g_h;

// One icon, drawn the way texpatch.c does (its own w x h canvas, the glyph filling it), onto tile col, row
static void cell(int col, int row, int size, int style, int label, const int keys[4], bool mono, bool light) {
    float *px = calloc((size_t)size * size * 4, sizeof(float));
    prompt_icon(px, size, size, 0, 0, (float)size, (float)size, style, label, keys, mono);
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
    const uint8_t bg[2][3] = {{40, 44, 56}, {226, 214, 186}};
    int ox = col * TILE + (TILE - size) / 2, oy = row * TILE + (TILE - size) / 2;
    for (int y = 0; y < TILE; y++)
        for (int x = 0; x < TILE; x++) {
            uint8_t *d = g_sheet + ((size_t)(row * TILE + y) * g_w + col * TILE + x) * 3;
            int ix = col * TILE + x - ox, iy = row * TILE + y - oy;
            const float *s = ix >= 0 && iy >= 0 && ix < size && iy < size ? px + (iy * size + ix) * 4 : NULL;
            for (int k = 0; k < 3; k++) {
                float b = bg[light][k] / 255.0f, v = s ? s[k] + b * (1 - s[3]) : b;
                if (mono && s) v = (s[0] * 0.299f + s[1] * 0.587f + s[2] * 0.114f) + b * (1 - s[3]);
                d[k] = (uint8_t)lroundf(sat(v) * 255);
            }
        }
    free(px);
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

int main(void) {
    static const int labels[COLS] = {R_A, R_B, R_X, R_Y, R_L, R_R, R_ZL, R_ZR, R_PLUS, R_MINUS,
                                     L_LS, L_RS, L_DUP, L_DDOWN, L_DLEFT, L_DRIGHT, L_DPAD, L_OTHER};
    // the default keyboard bindings (runtime/hle/bindings.c), in label order; L_OTHER: Space
    static const int keys[COLS][4] = {
        {SDL_SCANCODE_K}, {SDL_SCANCODE_J}, {SDL_SCANCODE_I}, {SDL_SCANCODE_L}, {SDL_SCANCODE_Q}, {SDL_SCANCODE_E},
        {SDL_SCANCODE_1}, {SDL_SCANCODE_3}, {SDL_SCANCODE_RETURN}, {SDL_SCANCODE_BACKSPACE},
        {SDL_SCANCODE_W, SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_D},
        {SDL_SCANCODE_KP_8, SDL_SCANCODE_KP_4, SDL_SCANCODE_KP_5, SDL_SCANCODE_KP_6},
        {SDL_SCANCODE_UP}, {SDL_SCANCODE_DOWN}, {SDL_SCANCODE_LEFT}, {SDL_SCANCODE_RIGHT},
        {SDL_SCANCODE_UP, SDL_SCANCODE_LEFT, SDL_SCANCODE_DOWN, SDL_SCANCODE_RIGHT}, {SDL_SCANCODE_SPACE}};
    static const int extra[COLS] = {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_LCTRL, SDL_SCANCODE_LALT, SDL_SCANCODE_TAB,
                                    SDL_SCANCODE_ESCAPE, SDL_SCANCODE_CAPSLOCK, SDL_SCANCODE_F1, SDL_SCANCODE_F12,
                                    SDL_SCANCODE_PAGEUP, SDL_SCANCODE_DELETE, SDL_SCANCODE_SEMICOLON,
                                    SDL_SCANCODE_SLASH, SDL_SCANCODE_LEFTBRACKET, SDL_SCANCODE_EQUALS,
                                    512 + SDL_BUTTON_LEFT, 512 + SDL_BUTTON_RIGHT, 512 + SDL_BUTTON_MIDDLE,
                                    512 + SDL_BUTTON_X1};
    int rows = 15;
    g_w = COLS * TILE, g_h = rows * TILE;
    g_sheet = calloc((size_t)g_w * g_h, 3);
    int row = 0;
    for (int light = 0; light < 2; light++)
        for (int s = STYLE_XBOX; s <= STYLE_KEYBOARD; s++, row++)
            for (int c = 0; c < COLS; c++) cell(c, row, 88, s, labels[c], keys[c], false, light);
    for (int c = 0; c < COLS; c++) cell(c, row, 88, STYLE_NINTENDO, labels[c], keys[c], false, false);
    row++;
    for (int s = STYLE_XBOX; s <= STYLE_PS; s++, row++)
        for (int c = 0; c < COLS; c++) cell(c, row, 88, s, labels[c], keys[c], true, false);
    for (int s = STYLE_XBOX; s <= STYLE_KEYBOARD; s++, row++)
        for (int c = 0; c < COLS; c++) cell(c, row, 44, s, labels[c], keys[c], false, false);
    for (int c = 0; c < COLS; c++) cell(c, row, 88, STYLE_KEYBOARD, R_A, (int[4]){extra[c]}, false, false);
    row++;
    for (int i = 0; i < 36 && row + i / COLS < rows; i++) {
        int k = i < 26 ? SDL_SCANCODE_A + i : i == 26 ? SDL_SCANCODE_0 : SDL_SCANCODE_1 + i - 27;
        cell(i % COLS, row + i / COLS, 88, STYLE_KEYBOARD, R_A, (int[4]){k}, false, true);
    }
    write_png("build/prompt_icons.png", g_sheet, g_w, g_h);
    printf("build/prompt_icons.png: %d icons, all drawn inside their textures\n", rows * COLS);
    return 0;
}
