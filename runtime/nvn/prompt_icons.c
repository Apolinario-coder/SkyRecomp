// Button prompt icons (prompt_icons.h): shapes as signed distances, antialiased over one pixel and painted at the
// texture's own resolution. Controller buttons: a dark face inside a light rim, labelled in the button's own colours
// (Xbox letters, PlayStation symbols) or white; keyboard keys: a light keycap with a dark label. mono: light buttons
// with dark ink. Every shape gets a soft shadow under it, so the icons read on the game's light and dark panels.
#include "prompt_icons.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

typedef struct { float ax, ay, bx, by; } Seg;

// ------------------------------------------------------------------ stroke font
// Centre lines on a cap height of 100, y down. M x y: move; L x y: line; A cx cy rx ry a0 a1: an elliptical arc from
// angle a0 to a1 (degrees: 0 right, 90 down), joined to the pen by a line unless the pen was just moved. Drawn with
// round caps, FONT_WEIGHT wide. Codes 1-8: Enter, Backspace, Shift, Tab, arrows up / down / left / right.
#define FONT_WEIGHT 17.0f
#define FONT_GAP 12.0f  // between letters' ink
static const char *const k_font[128] = {
    ['A'] = "M0 100 L36 0 L72 100 M15 62 L57 62",
    ['B'] = "M0 100 L0 0 L32 0 A32 24 24 24 -90 90 L0 48 L34 48 A34 74 28 26 -90 90 L0 100",
    ['C'] = "A46 50 46 50 -42 -318",
    ['D'] = "M0 0 L0 100 L22 100 A22 50 50 50 90 -90 L0 0",
    ['E'] = "M58 0 L0 0 L0 100 L58 100 M0 50 L50 50",
    ['F'] = "M58 0 L0 0 L0 100 M0 50 L50 50",
    ['G'] = "A46 50 46 50 -42 -360 L54 50",
    ['H'] = "M0 0 L0 100 M68 0 L68 100 M0 50 L68 50",
    ['I'] = "M0 0 L0 100",
    ['J'] = "M52 0 L52 68 A26 68 26 32 0 180",
    ['K'] = "M0 0 L0 100 M64 0 L0 64 M22 42 L66 100",
    ['L'] = "M0 0 L0 100 L54 100",
    ['M'] = "M0 100 L0 0 L42 68 L84 0 L84 100",
    ['N'] = "M0 100 L0 0 L68 100 L68 0",
    ['O'] = "A48 50 48 50 0 360",
    ['P'] = "M0 100 L0 0 L32 0 A32 27 28 27 -90 90 L0 54",
    ['Q'] = "A48 50 48 50 0 360 M58 68 L94 104",
    ['R'] = "M0 100 L0 0 L32 0 A32 27 28 27 -90 90 L0 54 M30 54 L62 100",
    ['S'] = "A31 26 29 26 -25 -270 A31 75 31 25 -90 155",
    ['T'] = "M0 0 L64 0 M32 0 L32 100",
    ['U'] = "M0 0 L0 66 A33 66 33 34 180 0 L66 0",
    ['V'] = "M0 0 L36 100 L72 0",
    ['W'] = "M0 0 L24 100 L50 28 L76 100 L100 0",
    ['X'] = "M0 0 L66 100 M66 0 L0 100",
    ['Y'] = "M0 0 L35 50 L70 0 M35 50 L35 100",
    ['Z'] = "M2 0 L62 0 L0 100 L64 100",
    ['0'] = "A32 50 32 50 0 360",
    ['1'] = "M0 22 L28 0 L28 100",
    ['2'] = "A31 30 30 30 -160 20 L0 100 L62 100",
    ['3'] = "A30 25 28 25 -160 90 A30 74 31 26 -90 160",
    ['4'] = "M50 100 L50 0 L0 70 L68 70",
    ['5'] = "M60 0 L8 0 L4 46 A31 70 31 30 -125 155",
    ['6'] = "M50 0 L8 50 M0 0 A31 69 31 31 0 360",
    ['7'] = "M0 0 L62 0 L18 100",
    ['8'] = "A31 25 27 25 0 360 M0 0 A31 74 31 26 0 360",
    ['9'] = "A31 31 31 31 0 360 M54 52 L14 100",
    ['?'] = "A28 26 28 26 -170 70 L30 62 L30 72 M30 100 L30 100",
    ['-'] = "M0 56 L40 56",
    ['+'] = "M32 24 L32 88 M0 56 L64 56",
    ['='] = "M0 38 L56 38 M0 72 L56 72",
    ['['] = "M28 -4 L0 -4 L0 104 L28 104",
    [']'] = "M0 -4 L28 -4 L28 104 L0 104",
    [';'] = "M0 40 L0 40 M2 86 L-6 108",
    [','] = "M2 92 L-6 112",
    ['.'] = "M0 100 L0 100",
    ['/'] = "M46 -2 L0 102",
    ['\\'] = "M0 -2 L46 102",
    ['\''] = "M0 0 L0 28",
    ['`'] = "M0 0 L16 20",
    [1] = "M84 8 L84 62 L8 62 M34 36 L8 62 L34 88",
    [2] = "M90 50 L6 50 M34 22 L6 50 L34 78",
    [3] = "M40 0 L80 48 L58 48 L58 100 L22 100 L22 48 L0 48 L40 0",
    [4] = "M0 50 L78 50 M52 24 L78 50 L52 76 M90 16 L90 84",
    [5] = "M32 100 L32 4 M4 32 L32 4 L60 32",
    [6] = "M32 0 L32 96 M4 68 L32 96 L60 68",
    [7] = "M96 50 L4 50 M32 22 L4 50 L32 78",
    [8] = "M0 50 L92 50 M64 22 L92 50 L64 78",
};

static int glyph_segs(int ch, Seg *s, int max) {  // (font units)
    const char *p = ch > 0 && ch < 128 && k_font[ch] ? k_font[ch] : k_font['?'];
    int n = 0;
    float x = 0, y = 0;
    bool down = false;
    for (char c; (c = *p++);) {
        if (c != 'M' && c != 'L' && c != 'A') continue;
        float v[6] = {0};
        for (int i = 0; i < (c == 'A' ? 6 : 2); i++) v[i] = strtof(p, (char **)&p);
        if (c == 'M') { x = v[0], y = v[1], down = false; continue; }
        if (c == 'L') {
            if (n < max) s[n++] = (Seg){x, y, v[0], v[1]};
            x = v[0], y = v[1], down = true;
            continue;
        }
        int steps = (int)(fabsf(v[5] - v[4]) / 10) + 2;
        for (int i = 0; i <= steps; i++) {
            float a = (v[4] + (v[5] - v[4]) * i / steps) * 3.14159265f / 180;
            float nx = v[0] + v[2] * cosf(a), ny = v[1] + v[3] * sinf(a);
            if ((i || down) && n < max) s[n++] = (Seg){x, y, nx, ny};
            x = nx, y = ny;
        }
        down = true;
    }
    return n;
}

// Text as strokes in pixels, centred on cx, cy: cap height `cap`, smaller if its ink would be wider than max_w
static int text_segs(const char *t, float cx, float cy, float cap, float max_w, Seg *out, int max, float *radius) {
    Seg g[160];
    float x = 0;
    int n = 0;
    for (const char *p = t; *p; p++) {
        if (*p == ' ') { x += 34; continue; }
        int k = glyph_segs((unsigned char)*p, g, 160);
        float lo = 1e9f, hi = -1e9f;
        for (int i = 0; i < k; i++) {
            lo = fminf(lo, fminf(g[i].ax, g[i].bx));
            hi = fmaxf(hi, fmaxf(g[i].ax, g[i].bx));
        }
        if (p != t && p[-1] != ' ') x += FONT_WEIGHT + FONT_GAP;
        for (int i = 0; i < k && n < max; i++)
            out[n++] = (Seg){g[i].ax - lo + x, g[i].ay, g[i].bx - lo + x, g[i].by};
        x += hi - lo;
    }
    float s = cap / 100;
    if ((x + FONT_WEIGHT) * s > max_w) s = max_w / (x + FONT_WEIGHT);
    for (int i = 0; i < n; i++)
        out[i] = (Seg){cx + (out[i].ax - x / 2) * s, cy + (out[i].ay - 50) * s, cx + (out[i].bx - x / 2) * s,
                       cy + (out[i].by - 50) * s};
    *radius = FONT_WEIGHT / 2 * s;
    return n;
}

// ------------------------------------------------------------------ shapes
typedef struct { float *px; int w, h; } Img;
enum { SH_CIRCLE, SH_BOX, SH_CROSS, SH_SEGS, SH_TRI };
typedef struct Shape {
    int type;
    float cx, cy, hw, hh;      // centre, half size (circle: radius hw; cross: arms hw long, hh wide)
    float r[4];                // corner radii: top-left, top-right, bottom-right, bottom-left; strokes: r[0] = radius
    float edge;                // > 0: only an outline this wide, inside the shape
    const Seg *seg;            // strokes (round-capped)
    int n;
    float t[6];                // triangle
    const struct Shape *clip;  // painted only inside this one too
} Shape;

static float sat(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

static float seg_dist(float px, float py, const Seg *s) {
    float vx = s->bx - s->ax, vy = s->by - s->ay, wx = px - s->ax, wy = py - s->ay;
    float t = sat((wx * vx + wy * vy) / (vx * vx + vy * vy + 1e-6f));
    return hypotf(wx - t * vx, wy - t * vy);
}

static float box_dist(float px, float py, float hw, float hh, const float *r) {  // (centred)
    float rr = py < 0 ? (px < 0 ? r[0] : r[1]) : (px < 0 ? r[3] : r[2]);
    float qx = fabsf(px) - hw + rr, qy = fabsf(py) - hh + rr;
    return fminf(fmaxf(qx, qy), 0) + hypotf(fmaxf(qx, 0), fmaxf(qy, 0)) - rr;
}

static float sdist(const Shape *s, float x, float y) {  // signed distance, in pixels (negative inside)
    float px = x - s->cx, py = y - s->cy, d;
    switch (s->type) {
    case SH_CIRCLE: d = hypotf(px, py) - s->hw; break;
    case SH_BOX: d = box_dist(px, py, s->hw, s->hh, s->r); break;
    case SH_CROSS: d = fminf(box_dist(px, py, s->hw, s->hh, s->r), box_dist(px, py, s->hh, s->hw, s->r)); break;
    case SH_SEGS:
        d = 1e9f;
        for (int i = 0; i < s->n; i++) d = fminf(d, seg_dist(x, y, &s->seg[i]));
        d -= s->r[0];
        break;
    default: {  // triangle: the farthest of its edges' lines (exact enough near the edges, all that's needed)
        const float *t = s->t;
        float o = (t[2] - t[0]) * (t[5] - t[1]) - (t[4] - t[0]) * (t[3] - t[1]) > 0 ? 1 : -1;
        d = -1e9f;
        for (int i = 0; i < 3; i++) {
            float ax = t[2 * i], ay = t[2 * i + 1], ex = t[(2 * i + 2) % 6] - ax, ey = t[(2 * i + 3) % 6] - ay;
            d = fmaxf(d, -o * (ex * (y - ay) - ey * (x - ax)) / hypotf(ex, ey));
        }
    }
    }
    if (s->edge > 0) d = fabsf(d + s->edge / 2) - s->edge / 2;
    return s->clip ? fmaxf(d, sdist(s->clip, x, y)) : d;
}

static void bounds(const Shape *s, float b[4]) {
    if (s->type == SH_SEGS || s->type == SH_TRI) {
        b[0] = b[1] = 1e9f, b[2] = b[3] = -1e9f;
        for (int i = 0; i < (s->type == SH_TRI ? 3 : s->n * 2); i++) {
            float x = s->type == SH_TRI ? s->t[2 * i] : i & 1 ? s->seg[i / 2].bx : s->seg[i / 2].ax;
            float y = s->type == SH_TRI ? s->t[2 * i + 1] : i & 1 ? s->seg[i / 2].by : s->seg[i / 2].ay;
            b[0] = fminf(b[0], x), b[1] = fminf(b[1], y), b[2] = fmaxf(b[2], x), b[3] = fmaxf(b[3], y);
        }
        float m = s->type == SH_SEGS ? s->r[0] : 0;
        b[0] -= m, b[1] -= m, b[2] += m, b[3] += m;
        return;
    }
    float ex = s->type == SH_CROSS ? fmaxf(s->hw, s->hh) : s->hw, ey = s->type == SH_CIRCLE ? s->hw : ex;
    if (s->type == SH_BOX) ey = s->hh;
    b[0] = s->cx - ex, b[1] = s->cy - ey, b[2] = s->cx + ex, b[3] = s->cy + ey;
}

// Paints s over the image in a gradient from top to bot down its height, with opacity a; soft: the edge's width
// in pixels (1: antialiased; more: a shadow's blur)
static void paint(Img *im, const Shape *s, const float *top, const float *bot, float a, float soft) {
    float b[4];
    bounds(s, b);
    int x0 = (int)fmaxf(0, floorf(b[0] - soft)), y0 = (int)fmaxf(0, floorf(b[1] - soft));
    int x1 = (int)fminf((float)im->w, ceilf(b[2] + soft)), y1 = (int)fminf((float)im->h, ceilf(b[3] + soft));
    for (int y = y0; y < y1; y++) {
        float t = sat((y + 0.5f - b[1]) / fmaxf(b[3] - b[1], 1)), c[3];
        for (int k = 0; k < 3; k++) c[k] = top[k] + (bot[k] - top[k]) * t;
        for (int x = x0; x < x1; x++) {
            float cov = sat(0.5f - sdist(s, x + 0.5f, y + 0.5f) / soft) * a;
            if (cov <= 0) continue;
            float *p = im->px + ((size_t)y * im->w + x) * 4;
            for (int k = 0; k < 3; k++) p[k] = c[k] * cov + p[k] * (1 - cov);
            p[3] = cov + p[3] * (1 - cov);
        }
    }
}

static Shape circle(float cx, float cy, float r) { return (Shape){.type = SH_CIRCLE, .cx = cx, .cy = cy, .hw = r, .hh = r}; }
static Shape box(float cx, float cy, float hw, float hh, float rt, float rb) {
    return (Shape){.type = SH_BOX, .cx = cx, .cy = cy, .hw = hw, .hh = hh, .r = {rt, rt, rb, rb}};
}
static Shape tri(float x0, float y0, float x1, float y1, float x2, float y2) {
    return (Shape){.type = SH_TRI, .t = {x0, y0, x1, y1, x2, y2}};
}
static Shape inset(Shape s, float t) {
    s.hw -= t, s.hh -= t;
    for (int k = 0; k < 4; k++) s.r[k] = fmaxf(s.r[k] - t, 0);
    return s;
}

// ------------------------------------------------------------------ looks
#define C(r, g, b) {(r) / 255.0f, (g) / 255.0f, (b) / 255.0f}
typedef struct { float rim[3], top[3], bot[3], ink[3], dim[3], accent[3]; } Pal;
static const Pal k_dark = {C(228, 229, 234), C(74, 77, 87), C(30, 31, 36), C(246, 246, 248), C(122, 126, 136), C(232, 72, 64)};
static const Pal k_mono = {C(62, 62, 68), C(252, 252, 252), C(208, 209, 214), C(30, 30, 34), C(140, 140, 146), C(30, 30, 34)};
static const float k_black[3] = {0};
// keycaps (light in both): outline, sides, top face, label
static const float k_key_edge[3] = C(38, 39, 44), k_key_side[3] = C(160, 162, 170), k_key_side2[3] = C(112, 114, 122),
                   k_key_top[3] = C(252, 252, 253), k_key_top2[3] = C(220, 221, 226), k_key_ink[3] = C(36, 37, 42);
// by Switch position (east, south, north, west): Xbox B A Y X, PlayStation circle cross triangle square
static const float k_xbox[4][3] = {C(240, 76, 66), C(104, 200, 64), C(250, 200, 40), C(64, 150, 240)};
static const float k_ps[4][3] = {C(240, 100, 108), C(124, 170, 240), C(64, 214, 160), C(232, 128, 208)};

static void shadow(Img *im, Shape s, float size) {
    s.cy += size * 0.04f;
    paint(im, &s, k_black, k_black, 0.4f, fmaxf(1, size * 0.07f));
}

// A controller button: its shadow, the light rim, the dark face inside it (rim t wide)
static void button(Img *im, Shape s, float size, float t, const Pal *p) {
    shadow(im, s, size);
    paint(im, &s, p->rim, p->rim, 1, 1);
    s = inset(s, t);
    paint(im, &s, p->top, p->bot, 1, 1);
}

static void text(Img *im, const char *t, float cx, float cy, float cap, float max_w, const float *col) {
    Seg s[400];
    float r;
    int n = text_segs(t, cx, cy, cap, max_w, s, 400, &r);
    Shape sh = {.type = SH_SEGS, .seg = s, .n = n, .r = {r}};
    paint(im, &sh, col, col, 1, 1);
}

// Strokes given in -1..1, drawn at cx, cy scaled by sc, r: their half width in pixels
static void lines(Img *im, const Seg *u, int n, float cx, float cy, float sc, float r, const float *col) {
    Seg s[16];
    for (int i = 0; i < n && i < 16; i++) s[i] = (Seg){cx + u[i].ax * sc, cy + u[i].ay * sc, cx + u[i].bx * sc, cy + u[i].by * sc};
    Shape sh = {.type = SH_SEGS, .seg = s, .n = n < 16 ? n : 16, .r = {r}};
    paint(im, &sh, col, col, 1, 1);
}

static void fit(float bw, float bh, float aspect, float *w, float *h) {  // (room left for the shadow)
    *h = fminf(bh * 0.88f, bw * 0.92f / aspect);
    *w = *h * aspect;
}

static void face(Img *im, float cx, float cy, float d, int label, int style, const Pal *p, bool mono) {
    float r = d * 0.45f;
    button(im, circle(cx, cy, r), d, r * 0.1f, p);
    int i = label - R_A;
    const float *col = mono || style == STYLE_NINTENDO ? p->ink : style == STYLE_XBOX ? k_xbox[i] : k_ps[i];
    if (style == STYLE_PS) {
        static const Seg cross[2] = {{-.42f, -.42f, .42f, .42f}, {.42f, -.42f, -.42f, .42f}};
        static const Seg triangle[3] = {{0, -.54f, .52f, .32f}, {.52f, .32f, -.52f, .32f}, {-.52f, .32f, 0, -.54f}};
        static const Seg square[4] = {{-.4f, -.4f, .4f, -.4f}, {.4f, -.4f, .4f, .4f}, {.4f, .4f, -.4f, .4f}, {-.4f, .4f, -.4f, -.4f}};
        float th = r * 0.075f;
        if (label == R_A) {
            Shape ring = circle(cx, cy, r * 0.5f + th);
            ring.edge = 2 * th;
            paint(im, &ring, col, col, 1, 1);
        } else if (label == R_B) lines(im, cross, 2, cx, cy, r, th, col);
        else if (label == R_X) lines(im, triangle, 3, cx, cy, r, th, col);
        else lines(im, square, 4, cx, cy, r, th, col);
        return;
    }
    char t[2] = {(style == STYLE_XBOX ? "BAYX" : "ABXY")[i], 0};
    text(im, t, cx, cy, r * 0.95f, r * 1.3f, col);
}

static void shoulder(Img *im, float cx, float cy, float bw, float bh, int label, int style, const Pal *p) {
    bool trigger = label == R_ZL || label == R_ZR;
    float w, h;
    fit(bw, bh, trigger ? 1.3f : 1.7f, &w, &h);
    button(im, box(cx, cy, w / 2, h / 2, h * (trigger ? 0.5f : 0.42f), h * (trigger ? 0.16f : 0.42f)), h, h * 0.07f, p);
    static const char *const xbox[4] = {"LB", "RB", "LT", "RT"}, *const ps[4] = {"L1", "R1", "L2", "R2"},
                             *const nin[4] = {"L", "R", "ZL", "ZR"};
    int i = label - R_L;
    text(im, style == STYLE_XBOX ? xbox[i] : style == STYLE_PS ? ps[i] : nin[i], cx, cy + (trigger ? h * 0.04f : 0),
         h * (trigger ? 0.42f : 0.46f), w * 0.7f, p->ink);
}

// Plus / Minus: Xbox Menu / View, PlayStation Options / Create (small pills), Nintendo + / -
static void system_button(Img *im, float cx, float cy, float bw, float bh, int label, int style, const Pal *p) {
    float d = fminf(bw, bh), sc;
    if (style == STYLE_PS) {
        float w, h;
        fit(bw, bh, 1.5f, &w, &h);
        button(im, box(cx, cy, w / 2, h / 2, h / 2, h / 2), h, h * 0.07f, p);
        sc = h * 0.3f;
    } else {
        button(im, circle(cx, cy, d * 0.4f), d, d * 0.04f, p);
        sc = d * 0.2f;
    }
    if (style == STYLE_NINTENDO) {
        static const Seg plus[2] = {{-1, 0, 1, 0}, {0, -1, 0, 1}};
        lines(im, plus, label == R_PLUS ? 2 : 1, cx, cy, sc, sc * 0.2f, p->ink);
    } else if (label == R_PLUS) {
        static const Seg bars[3] = {{-1, -.62f, 1, -.62f}, {-1, 0, 1, 0}, {-1, .62f, 1, .62f}};
        lines(im, bars, 3, cx, cy, sc, sc * 0.14f, p->ink);
    } else if (style == STYLE_XBOX) {  // two windows, the front one hiding a corner of the back one
        static const Seg view[8] = {{-.95f, -.35f, .35f, -.35f}, {.35f, -.35f, .35f, .95f}, {.35f, .95f, -.95f, .95f},
                                    {-.95f, .95f, -.95f, -.35f}, {-.35f, -.95f, .95f, -.95f}, {.95f, -.95f, .95f, .35f},
                                    {.95f, .35f, .35f, .35f}, {-.35f, -.95f, -.35f, -.35f}};
        lines(im, view, 8, cx, cy, sc, sc * 0.12f, p->ink);
    } else {  // three rays
        static const Seg rays[3] = {{0, -.9f, 0, .25f}, {-.9f, -.45f, -.3f, .55f}, {.9f, -.45f, .3f, .55f}};
        lines(im, rays, 3, cx, cy + sc * 0.1f, sc, sc * 0.15f, p->ink);
    }
}

static void stick_click(Img *im, float cx, float cy, float d, int label, int style, const Pal *p) {
    float r = d * 0.45f;
    button(im, circle(cx, cy, r), d, r * 0.1f, p);
    Shape ring = circle(cx, cy, r * 0.74f);
    ring.edge = r * 0.07f;
    paint(im, &ring, p->dim, p->dim, 1, 1);
    bool l = label == L_LS;
    text(im, style == STYLE_PS ? (l ? "L3" : "R3") : style == STYLE_XBOX ? (l ? "LS" : "RS") : (l ? "L" : "R"), cx, cy,
         r * 0.6f, r * 0.95f, p->ink);
}

// The d-pad: a cross, the pressed arm lit (label L_DUP..L_DRIGHT; L_DPAD: none, all four arrows lit)
static void dpad(Img *im, float cx, float cy, float d, int label, const Pal *p) {
    float L = d * 0.46f, a = d * 0.17f, rr = a * 0.5f, t = d * 0.035f;
    button(im, (Shape){.type = SH_CROSS, .cx = cx, .cy = cy, .hw = L, .hh = a, .r = {rr, rr, rr, rr}}, d, t, p);
    static const float dir[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};  // L_DUP, L_DDOWN, L_DLEFT, L_DRIGHT
    int lit = label - L_DUP < 4 ? label - L_DUP : -1;
    for (int i = 0; i < 4; i++) {
        float ux = dir[i][0], uy = dir[i][1], m = (L + a) / 2 - t / 2, ex = cx + ux * m, ey = cy + uy * m;
        if (i == lit) {
            float hl = (L - a) / 2 - t / 2, o = rr - t;
            Shape arm = box(ex, ey, ux ? hl : a - t, ux ? a - t : hl, 0, 0);
            arm.r[0] = uy < 0 || ux < 0 ? o : 0, arm.r[1] = uy < 0 || ux > 0 ? o : 0;
            arm.r[2] = uy > 0 || ux > 0 ? o : 0, arm.r[3] = uy > 0 || ux < 0 ? o : 0;
            paint(im, &arm, p->ink, p->ink, 1, 1);
        }
        float q = a * 0.9f, bx = ex - ux * q * 0.45f, by = ey - uy * q * 0.45f, nx = -uy * q * 0.62f, ny = ux * q * 0.62f;
        Shape arrow = tri(ex + ux * q * 0.55f, ey + uy * q * 0.55f, bx + nx, by + ny, bx - nx, by - ny);
        const float *c = i == lit ? p->bot : lit < 0 ? p->ink : p->dim;
        paint(im, &arrow, c, c, 1, 1);
    }
}

// ------------------------------------------------------------------ keyboard
static const char *key_label(int sc, char *buf, size_t n) {  // a key's text in the stroke font (uppercase, symbols)
    switch (sc) {
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: return "\x01";
    case SDL_SCANCODE_BACKSPACE: return "\x02";
    case SDL_SCANCODE_LSHIFT: case SDL_SCANCODE_RSHIFT: return "\x03";
    case SDL_SCANCODE_TAB: return "\x04";
    case SDL_SCANCODE_UP: return "\x05";
    case SDL_SCANCODE_DOWN: return "\x06";
    case SDL_SCANCODE_LEFT: return "\x07";
    case SDL_SCANCODE_RIGHT: return "\x08";
    case SDL_SCANCODE_SPACE: return "SPACE";
    case SDL_SCANCODE_ESCAPE: return "ESC";
    case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: return "CTRL";
    case SDL_SCANCODE_LALT: case SDL_SCANCODE_RALT: return "ALT";
    case SDL_SCANCODE_LGUI: case SDL_SCANCODE_RGUI: return "WIN";
    case SDL_SCANCODE_CAPSLOCK: return "CAPS";
    case SDL_SCANCODE_PAGEUP: return "PGUP";
    case SDL_SCANCODE_PAGEDOWN: return "PGDN";
    case SDL_SCANCODE_DELETE: return "DEL";
    case SDL_SCANCODE_INSERT: return "INS";
    }
    if (sc > 512 && sc <= 512 + SDL_BUTTON_X2) { snprintf(buf, n, "M%d", sc - 512); return buf; }  // (in a cluster)
    const char *s = sc > 0 && sc < SDL_SCANCODE_COUNT ? SDL_GetScancodeName((SDL_Scancode)sc) : "";
    if (!strncmp(s, "Keypad ", 7)) s += 7;
    size_t i = 0;
    for (; s[i] && i + 1 < n; i++) buf[i] = (char)toupper((unsigned char)s[i]);
    buf[i] = 0;
    return i ? buf : "?";
}

static void keycap(Img *im, float cx, float cy, float kw, float kh, const char *label, float cap) {  // (cap: of kh)
    Shape o = box(cx, cy, kw / 2, kh / 2, kh * 0.2f, kh * 0.2f);
    shadow(im, o, kh);
    paint(im, &o, k_key_edge, k_key_edge, 1, 1);
    Shape side = inset(o, kh * 0.05f);
    paint(im, &side, k_key_side, k_key_side2, 1, 1);
    float tt = kh * 0.08f, tb = kh * 0.2f;  // the top face: inset more at the bottom (the key's front)
    Shape top = box(cx, cy + (tt - tb) / 2, kw / 2 - kh * 0.11f, kh / 2 - (tt + tb) / 2, kh * 0.13f, kh * 0.13f);
    paint(im, &top, k_key_top, k_key_top2, 1, 1);
    text(im, label, top.cx, top.cy, kh * cap, top.hw * 1.64f, k_key_ink);
}

static void mouse(Img *im, float cx, float cy, float d, int button, const Pal *p) {
    float hw = d * 0.27f, hh = d * 0.42f, t = d * 0.04f, split = cy - hh * 0.12f;
    Shape m = box(cx, cy, hw, hh, hw, hw * 0.8f);
    shadow(im, m, d);
    paint(im, &m, k_key_edge, k_key_edge, 1, 1);
    Shape in = inset(m, t);
    paint(im, &in, k_key_top, k_key_top2, 1, 1);
    if (button == SDL_BUTTON_LEFT || button == SDL_BUTTON_RIGHT) {  // the clicked half of the top, lit
        float l = button == SDL_BUTTON_LEFT ? -1 : 1;
        Shape half = box(cx + l * hw / 2, (cy - hh + split) / 2, hw / 2, (split - cy + hh) / 2, 0, 0);
        half.clip = &in;
        paint(im, &half, p->accent, p->accent, 1, 1);
    }
    Seg s[2] = {{cx, cy - hh, cx, split}, {cx - hw, split, cx + hw, split}};
    Shape sh = {.type = SH_SEGS, .seg = s, .n = 2, .r = {t * 0.55f}};
    paint(im, &sh, k_key_edge, k_key_edge, 1, 1);
    Shape wheel = box(cx, (cy - hh + split) / 2 + t, t * 1.2f, hh * 0.17f, t * 1.2f, t * 1.2f);
    const float *wc = button == SDL_BUTTON_MIDDLE ? p->accent : k_key_edge;
    paint(im, &wheel, wc, wc, 1, 1);
}

static void key(Img *im, float cx, float cy, float bw, float bh, int sc, const Pal *p) {
    if (sc > 512 && sc <= 512 + SDL_BUTTON_RIGHT) { mouse(im, cx, cy, fminf(bw, bh), sc - 512, p); return; }
    char buf[24];
    const char *t = key_label(sc, buf, sizeof buf);
    size_t n = strlen(t);
    float w, h;
    fit(bw, bh, n <= 1 ? 1 : n == 2 ? 1.25f : n == 3 ? 1.5f : n == 4 ? 1.8f : 1.95f, &w, &h);
    keycap(im, cx, cy, w, h, t, n > 1 ? 0.3f : 0.4f);
}

static void cluster(Img *im, float cx, float cy, float bw, float bh, const int keys[4]) {  // up, left, down, right
    float k = fminf(bw * 0.92f / 3.1f, bh * 0.9f / 2.05f), g = k * 0.05f;
    static const float at[4][2] = {{0, -1}, {-1, 1}, {0, 1}, {1, 1}};
    for (int i = 0; i < 4; i++) {
        char buf[24];
        const char *t = key_label(keys[i], buf, sizeof buf);
        keycap(im, cx + at[i][0] * (k + g), cy + at[i][1] * (k + g) / 2, k, k, t, strlen(t) > 1 ? 0.36f : 0.52f);
    }
}

void prompt_icon(float *px, int w, int h, float x, float y, float bw, float bh, int style, int label, const int keys[4],
                 bool mono) {
    Img im = {px, w, h};
    const Pal *p = mono ? &k_mono : &k_dark;
    float cx = x + bw / 2, cy = y + bh / 2, d = fminf(bw, bh);
    if (style == STYLE_KEYBOARD) {
        if (label == L_LS || label == L_RS || label == L_DPAD) cluster(&im, cx, cy, bw, bh, keys);
        else key(&im, cx, cy, bw, bh, keys[0], p);
    } else if (label >= R_A && label <= R_Y) face(&im, cx, cy, d, label, style, p, mono);
    else if (label >= R_L && label <= R_ZR) shoulder(&im, cx, cy, bw, bh, label, style, p);
    else if (label == R_PLUS || label == R_MINUS) system_button(&im, cx, cy, bw, bh, label, style, p);
    else if (label == L_LS || label == L_RS) stick_click(&im, cx, cy, d, label, style, p);
    else if ((label >= L_DUP && label <= L_DRIGHT) || label == L_DPAD) dpad(&im, cx, cy, d, label, p);
    else {  // another button (a paddle): a dot
        float r = d * 0.45f;
        button(&im, circle(cx, cy, r), d, r * 0.1f, p);
        Shape dot = circle(cx, cy, r * 0.24f);
        paint(&im, &dot, p->ink, p->ink, 1, 1);
    }
}
