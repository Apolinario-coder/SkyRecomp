// Widescreen: the camera code's 16:9 aspect literal loads are patched (tools/patches.py) to call
// rt_aspect_site(); the renderer sets the wide factor (output aspect / 16:9) each frame.
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

#include "../hle.h"
#include "../settings.h"

#define BACKDROP_HASHES 0x71fe31bbf03c3515ull

static float g_wide = 1.0f;  // output aspect divided by 16:9 (1 = normal)

void widescreen_set_factor(float f) { g_wide = f; }
float widescreen_factor(void) { return g_wide; }

// Effects drawn as a mesh around the camera, sized to cover a 16:9 view: with the widened camera their outer edge
// shows (Mysterious Woods fog ring: a hard-edged oval at 32:9, a stray cloud piece at the far left). Their draws
// get a viewport widened by the widescreen factor (centred), so they cover the window as they cover 16:9.
// Identified by fragment shader hash (both backends hash the shader code the same way).
float widescreen_effect_stretch(uint64_t fs_hash) {
    static const uint64_t k[] = {
        0xffad3b2d795231a7ull,  // Mysterious Woods fog ring (Field 06C...; found 2026-10-09 by skipping shaders)
        0xd66b33b13b2eac29ull,  // Mysterious Woods haze layer
        0x1372d4fe34dd4bb4ull,  // Mysterious Woods light-shaft boxes (their sides: vertical streaks at the far left)
    };
    if (g_wide <= 1.0f) return 1.0f;
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++)
        if (k[i] == fs_hash) return g_wide;
    // dungeon darkness around the camera: a hard-edged dark oval once a room shows wider than 16:9 (boxed rooms
    // keep the original vignette)
    extern bool g_room_wide;
    if (fs_hash == 0xc42e882c08c5e5ebull && g_room_wide) return g_wide;
    return 1.0f;
}

// Off in rooms that should keep 16:9 framing (interiors, dungeons) -- see widescreen_update.
static bool g_allowed = true;
static bool g_indoor;
bool widescreen_allowed(void) { return g_allowed && g_settings.aspect == ASPECT_UNLOCKED && !ENV("LARECOMP_NO_WIDESCREEN"); }
void widescreen_set_allowed(bool on) { g_allowed = on; }

// Interiors and dungeons use cameras that never load the patched overworld aspect (site 0). Count
// site-0 loads per presented frame; frames without any are framed 16:9 (pillar/letterboxed).
static volatile uint64_t g_site0_frame = ~0ull;
static int g_indoor_streak, g_outdoor_streak;

RT_ON_RESET(widescreen_reset) { g_indoor = false; g_indoor_streak = g_outdoor_streak = 0; }

void widescreen_frame_end(uint64_t frame) {
    bool saw = g_site0_frame == frame;
    if (saw) { g_outdoor_streak++; g_indoor_streak = 0; }
    else { g_indoor_streak++; g_outdoor_streak = 0; }
    bool was = g_indoor;
    if (!g_indoor && g_indoor_streak >= 3) g_indoor = true;
    if (g_indoor && g_outdoor_streak >= 1) g_indoor = false;
    if (was != g_indoor && ENV("LARECOMP_LOG_INDOOR"))
        LOG("[widescreen] %s at frame %llu\n", g_indoor ? "interior (16:9 framing)" : "overworld (wide)", (unsigned long long)frame);
}

float rt_aspect_site(int site) {
    if (site == 0) { extern uint64_t renderer_present_count(void); g_site0_frame = renderer_present_count(); }
    static long mask = -2;
    if (mask == -2) mask = getenv("LARECOMP_ASPECT_SITES") ? strtol(getenv("LARECOMP_ASPECT_SITES"), NULL, 0) : -1;
    float base = 16.0f / 9.0f;
    if (mask != -1 && !(mask & (1L << site))) return base;
    return base * g_wide;  // the output's aspect (wider or narrower than 16:9)
}

// Culling aspect: the game's own value (camera field), widened like the camera.
float rt_cull_aspect(float orig) {
    static float last = -1;
    if (orig != last && ENV("LARECOMP_LOG_CULL")) { LOG("[widescreen] culling aspect field %f\n", orig); last = orig; }
    return rt_aspect_site(6) * (orig > 0.1f && orig < 10.0f ? orig / (16.0f / 9.0f) : 1.0f);
}

// Vertical field of view: unchanged for wide outputs; for tall ones (factor < 1) widened so the
// horizontal view stays what the game shows at 16:9.
float rt_fovy(float orig) {
    static float last = -1;
    if (orig != last && ENV("LARECOMP_LOG_CULL")) { LOG("[widescreen] fovy field %f\n", orig); last = orig; }
    if (g_wide >= 1.0f || !(orig > 0)) return orig;
    bool degrees = orig > 3.2f;  // radians otherwise
    float half = (degrees ? orig * (float)M_PI / 180.0f : orig) * 0.5f;
    float wide = 2.0f * atanf(tanf(half) / g_wide);
    return degrees ? wide * 180.0f / (float)M_PI : wide;
}

// Interiors: the indoor shadow setup runs while an interior is rendered (tools/patches.py).
static uint64_t g_indoor_frame;
float rt_indoor_aspect(void) {
    extern uint64_t renderer_present_count(void);
    uint64_t f = renderer_present_count();
    if (ENV("LARECOMP_LOG_INDOOR") && f / 60 != g_indoor_frame / 60) LOG("[widescreen] indoor at frame %llu\n", (unsigned long long)f);
    g_indoor_frame = f;
    return 16.0f / 9.0f;
}

void romfs_file_read_hook(const char *path) { (void)path; }

// Interior backdrop (16:9 quad covering the overworld behind rooms), identified by shader code hash.
static uint64_t g_backdrop_hash = 0;  // set below once known
static uint64_t g_backdrop_frame = ~0ull;
void widescreen_backdrop_seen(uint64_t hash) {
    static const uint64_t known[] = {BACKDROP_HASHES};
    for (unsigned i = 0; i < sizeof known / sizeof known[0]; i++)
        if (known[i] && hash == known[i]) { extern uint64_t renderer_present_count(void); g_backdrop_frame = renderer_present_count(); }
    (void)g_backdrop_hash;
}
// Interiors and dungeons only render their 16:9 centre. Besides the backdrop detection, the game's current level
// decides: in play and not on the overworld ("Field") -> boxed. That also covers fades and loading frames, where
// no backdrop is drawn and the sides would show stale pixels.
static bool level_is_interior(void) {
    extern int game_in_play(void);
    if (!game_in_play()) return false;
    uint64_t ps = LD64(MAIN_BASE + 0x1CC1368);  // PlayerStatus*: current level name at +0
    if (!ps) return false;
    const char *lv = (const char *)(uintptr_t)ps;
    return lv[0] && strcmp(lv, "Field") != 0;
}

bool widescreen_frame_boxed(uint64_t frame) {
    static bool last;
    static uint64_t interior_until;  // keep boxing briefly after leaving: the first (dark) frames still show stale sides
    bool in = level_is_interior();
    if (in) interior_until = frame + 8;
    // title / file select (not in play): 16:9 screens; the title's logo glow is drawn too large in the wide image
    // (a blurred copy in the left bar, user 2026-10-09)
    extern int game_in_play(void);
    bool b = g_backdrop_frame == frame || in || frame < interior_until || !game_in_play();
    if (b != last && ENV("LARECOMP_LOG_INDOOR")) LOG("[widescreen] boxed %d at frame %llu\n", b, (unsigned long long)frame);
    last = b;
    return b;
}

// Rooms wider than one screen (multi-cell dungeon rooms, side-view passages) show what the wide camera sees of the
// room itself: the image is cut at the room's outer walls (the camera still scrolls and stops as at 16:9, so near
// an end wall that side is black) instead of at the 16:9 box. Single-screen rooms stay boxed.
// The current camera zone (the level's zone record: id, AABB min at +0x10, max at +0x20, camera params at +0x30)
// is reached through [[sdk+0xcf9e38]+0x680] (found 2026-10-09 by pointer scans, LARECOMP_FIND_FLOATS_PATH);
// the camera's view-projection comes from the scene UBO (vertex slot 5: view, projection, view-projection).
static float g_vp[16];  // rows
bool g_room_wide;  // the last presented frame showed a room wider than 16:9
static uint64_t g_vp_frame = ~0ull;
void widescreen_scene_ubo(const float *u, uint64_t size) {
    extern uint64_t renderer_present_count(void);
    uint64_t f = renderer_present_count();
    if (size != 320 || g_vp_frame == f) return;  // (the frame's first scene draw)
    if (u[12] != 0 || u[13] != 0 || u[14] != 0 || u[15] != 1 || u[30] != -1 || u[31] != 0) return;  // perspective
    memcpy(g_vp, u + 32, sizeof g_vp);
    g_vp_frame = f;
}

static bool heap_ptr(uint64_t p) { return p >= HEAP_BASE && p + 0x40 <= HEAP_BASE + HEAP_MAX && !(p & 7); }

// Current camera zone's box; false when there is none (title, overworld, loading).
static bool current_zone(float lo[3], float hi[3], uint32_t *id) {
    uint64_t o = LD64(SDK_BASE + 0xcf9e38);
    if (!heap_ptr(o)) return false;
    uint64_t z = LD64(o + 0x680);
    if (!heap_ptr(z)) return false;
    const float *f = (const float *)(uintptr_t)(z + 0x10);
    for (int i = 0; i < 3; i++) {
        lo[i] = f[i], hi[i] = f[4 + i];
        if (!(hi[i] > lo[i]) || hi[i] - lo[i] > 2000) return false;
    }
    *id = *(const uint32_t *)(uintptr_t)z;
    return true;
}

// Horizontal crop (uv of the wide image): ROOM_CROP for a room wider than one screen (u0..u1), ROOM_BOX for a
// single-screen room (16:9 box), ROOM_FULL when the zone doesn't match the camera (no box: the game's camera clamps
// for the wide view, so a 16:9 box around a stale zone's centre cut Link out - user 2026-10-09), ROOM_KEEP when
// there's no recent scene camera (keep the last choice; after half a second without one, e.g. menus: box).
enum { ROOM_KEEP, ROOM_BOX, ROOM_CROP, ROOM_FULL };
static int room_crop(uint64_t frame, double shape, float *u0, float *u1) {
    float lo[3], hi[3];
    uint32_t id;
    if (ENV("LARECOMP_NO_ROOM_WIDE")) return ROOM_BOX;
    if (g_vp_frame > frame || g_vp_frame + 4 < frame) return g_vp_frame + 30 < frame ? ROOM_BOX : ROOM_KEEP;  // (menus: no scene)
    if (!current_zone(lo, hi, &id)) return ROOM_FULL;
    const float *r0 = g_vp, *r1 = g_vp + 4, *r3 = g_vp + 12;
    bool side = hi[2] - lo[2] < 6.0f;  // side-view zones are thin in z (the camera looks along z)
    // the point under the screen centre (ndc 0, 0): on the floor (y 0) for top-down rooms, on the zone's centre
    // plane for side views
    float c[3];
    if (side) {
        c[2] = (lo[2] + hi[2]) / 2;
        float a = r0[0], b = r0[1], e = -(r0[2] * c[2] + r0[3]), d = r1[0], f = r1[1], g = -(r1[2] * c[2] + r1[3]);
        float det = a * f - b * d;
        if (fabsf(det) < 1e-6f) return ROOM_KEEP;
        c[0] = (e * f - b * g) / det, c[1] = (a * g - e * d) / det;
    } else {
        c[1] = 0;
        float a = r0[0], b = r0[2], e = -r0[3], d = r1[0], f = r1[2], g = -r1[3];
        float det = a * f - b * d;
        if (fabsf(det) < 1e-6f) return ROOM_KEEP;
        c[0] = (e * f - b * g) / det, c[2] = (a * g - e * d) / det;
    }
    int r;
    bool wide = hi[0] - lo[0] >= 16.0f;  // (cells are 15)
    if (c[0] < lo[0] - 1 || c[0] > hi[0] + 1) r = ROOM_FULL;  // camera outside the zone: stale zone
    else if (!wide) r = fabsf(c[0] - (lo[0] + hi[0]) / 2) < 1.0f ? ROOM_BOX : ROOM_FULL;  // centred or stale
    else {
        float ndc[2];
        r = ROOM_CROP;
        for (int e = 0; e < 2; e++) {
            float x = e ? hi[0] : lo[0];
            float w = r3[0] * x + r3[1] * c[1] + r3[2] * c[2] + r3[3];
            if (!(w > 0.01f)) return ROOM_KEEP;
            ndc[e] = (r0[0] * x + r0[1] * c[1] + r0[2] * c[2] + r0[3]) / w;
        }
        // never less than the 16:9 view the game frames
        float b0 = (float)((1.0 - 1.0 / shape) / 2), b1 = 1.0f - b0;
        *u0 = fminf(b0, fmaxf(0.0f, (ndc[0] + 1) / 2));
        *u1 = fmaxf(b1, fminf(1.0f, (ndc[1] + 1) / 2));
    }
    static uint32_t last_id = ~0u;
    static int last_r = -1;
    if ((id != last_id || r != last_r) && ENV("LARECOMP_LOG_INDOOR"))
        LOG("[widescreen] zone %u x %.1f..%.1f z %.1f..%.1f%s, camera x %.1f: %s at frame %llu\n", id, lo[0], hi[0], lo[2],
            hi[2], side ? " (side view)" : "", c[0], r == ROOM_CROP ? "crop" : r == ROOM_BOX ? "box" : "full (stale zone)",
            (unsigned long long)frame);
    last_id = id, last_r = r;
    return r;
}

// Presented part of the image (crop: uv origin and size) and its place in the window: interiors and dungeons show
// their 16:9 centre (taller outputs: the 16:9 band), rooms wider than one screen the part inside the room.
void widescreen_present_crop(uint64_t frame, double shape, float crop[4], double *vx, double *vy, double *vw, double *vh) {
    static uint64_t last;  // (last wide boxed frame: room changes slide the edges instead of jumping)
    static float cu0, cu1;
    crop[0] = crop[1] = 0, crop[2] = crop[3] = 1;
    g_room_wide = false;
    if (!g_is_las) return;
    if (shape == 1.0 || !widescreen_frame_boxed(frame)) return;
    if (shape < 1.0) {
        crop[1] = (float)((1.0 - shape) / 2); crop[3] = (float)shape;
        double nh = *vh * shape; *vy += (*vh - nh) / 2; *vh = nh;
        return;
    }
    static int mode = ROOM_BOX;
    float u0 = 0, u1 = 1;
    static float k0, k1;  // (ROOM_KEEP: the last target)
    extern int game_in_play(void);
    int m = game_in_play() ? room_crop(frame, shape, &u0, &u1) : ROOM_BOX;
    if (m == ROOM_KEEP) m = mode, u0 = k0, u1 = k1;
    if (m == ROOM_BOX) u0 = (float)((1.0 - 1.0 / shape) / 2), u1 = 1.0f - u0;
    if (m == ROOM_FULL) u0 = 0, u1 = 1;
    mode = m, k0 = u0, k1 = u1;
    g_room_wide = m != ROOM_BOX;
    if (frame <= last + 2 && frame > last) {
        const float step = 0.03f;  // of the image width per frame
        cu0 += fmaxf(-step, fminf(step, u0 - cu0));
        cu1 += fmaxf(-step, fminf(step, u1 - cu1));
    } else {
        cu0 = u0, cu1 = u1;
    }
    last = frame;
    crop[0] = cu0, crop[2] = cu1 - cu0;
    *vx += *vw * cu0, *vw *= cu1 - cu0;
}

// Current level name for logs ("-" when not in play).
const char *game_level_name(void) {
    extern int game_in_play(void);
    uint64_t ps = LD64(MAIN_BASE + 0x1CC1368);
    return game_in_play() && ps && *(const char *)(uintptr_t)ps ? (const char *)(uintptr_t)ps : "-";
}
