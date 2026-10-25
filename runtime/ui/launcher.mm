// Start screen, shown before the game boots: PLAY (with ‹ Vanilla / seed › picker), SETTINGS, QUIT, and game-files
// status at the lower left. Nothing of the game ships with Sky Recomp: on first launch the player picks the folder
// with their own game files (v1.0.0), we check the main module's build ID, copy the files, and build the game's data
// image and native code library; PLAY opens setup until that finishes.
//
// Skipped with --play, LARECOMP_NO_LAUNCHER=1, or any scripted test run (LARECOMP_INPUT): tests boot straight in.
// The in-game menu's "Title screen" comes back here without leaving the process (runtime/main.c reboots the game).
#ifdef __APPLE__
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#endif
#include <SDL3/SDL.h>
#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <pthread.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <algorithm>
#include <atomic>

#include "imgui.h"
#include "imgui_internal.h"  // (SetKeyOwner)
#include "option_rows.h"
#include "osk.h"
#ifdef __APPLE__
#include "imgui_impl_metal.h"
#else
#include "imgui_impl_vulkan.h"
#endif
#include "imgui_impl_sdl3.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include "platform.h"
#include "paths.h"
#include "gamebuild.h"
#include "gamelib.h"
#include "romfs_build.h"
#include "settings.h"
#include "input_bind.h"
void controls_bindings_draw(float scale);  // ui/controls_ui.mm
void ui_window_init(void);
SDL_Window *ui_window(void);
#ifdef __APPLE__
void *ui_metal_layer(void);
void *ui_metal_device(void);
#else
// runtime/nvn/vulkan: window frames and ImGui plumbing
bool vk_frame_begin(const float clear[4]);
void vk_frame_end(const char *shot_path);
void *vk_frame_cmd(void);
bool vk_imgui_backend_init(void);
void vk_imgui_backend_shutdown(void);
uint64_t vk_imgui_texture_rgba(const void *rgba, int w, int h);
#endif
}
#ifdef __APPLE__
#import <ImageIO/ImageIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

// (test) BGRA texture -> PNG (the game renderer's saver isn't up yet before the game boots)
static void save_png(id<MTLTexture> tex, const char *path) {
    size_t w = tex.width, h = tex.height;
    std::vector<uint8_t> px(w * h * 4);
    [tex getBytes:px.data() bytesPerRow:w * 4 fromRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0];
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(px.data(), w, h, 8, w * 4, cs,
                                             kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little);
    CGImageRef img = CGBitmapContextCreateImage(ctx);
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)path, strlen(path), false);
    CGImageDestinationRef dest = CGImageDestinationCreateWithURL(url, (__bridge CFStringRef)UTTypePNG.identifier, 1, NULL);
    if (dest) {  // (NULL when the file can't be created, e.g. its folder isn't there: CFRelease(NULL) traps)
        CGImageDestinationAddImage(dest, img, NULL);
        CGImageDestinationFinalize(dest);
        CFRelease(dest);
    } else fprintf(stderr, "[launcher] can't write %s\n", path);
    CFRelease(url), CGImageRelease(img), CGContextRelease(ctx), CGColorSpaceRelease(cs);
}
#endif

static const uint8_t BUILD_ID[8] = {0xd8, 0x24, 0xd4, 0xb1, 0xbb, 0xd4, 0x5d, 0x03};  // Skyward Sword HD v1.0.0 main
// The release tag this build is (also names the release files; Makefile / tools/bundle_macos.sh / make_dmg.sh read
// it): beta_<n> until 1.0, then v<major>.<minor>.<patch>.
#define LARECOMP_VERSION "v2.0.1"

// ------------------------------------------------------------------ Skyward Sword HD design tokens & vector ornaments
static const ImU32 kColGold       = IM_COL32(238, 198, 92, 255);
static const ImU32 kColGoldBright = IM_COL32(255, 230, 142, 255);
static const ImU32 kColAmber      = IM_COL32(232, 142, 54, 255);
static const ImU32 kColTeal       = IM_COL32(102, 214, 206, 255);
static const ImU32 kColIvory      = IM_COL32(246, 241, 227, 255);
static const ImU32 kColSageMuted  = IM_COL32(182, 188, 176, 255);
static const ImU32 kColPanelBg    = IM_COL32(13, 23, 26, 244);

static void draw_diamond(ImDrawList *dl, ImVec2 c, float rx, float ry, ImU32 col) {
    ImVec2 pts[4] = {ImVec2(c.x, c.y - ry), ImVec2(c.x + rx, c.y), ImVec2(c.x, c.y + ry), ImVec2(c.x - rx, c.y)};
    dl->AddConvexPolyFilled(pts, 4, col);
}

// Skyward Sword sacred tablet frame: dark slate fill, outer gold hairline, inner inset hairline, and 4 corner diamonds
static void draw_tablet_frame(ImDrawList *dl, ImVec2 a, ImVec2 b, float sc, float rounding = 14.0f,
                              ImU32 bg = kColPanelBg) {
    float r = rounding * sc;
    dl->AddRectFilled(a, b, bg, r);
    dl->AddRect(a, b, IM_COL32(238, 198, 92, 125), r, 0, 1.6f * sc);
    float in = 5.0f * sc;
    dl->AddRect(ImVec2(a.x + in, a.y + in), ImVec2(b.x - in, b.y - in),
                IM_COL32(238, 198, 92, 36), fmaxf(2.0f, r - 3.0f * sc), 0, 1.0f);
    float dc = 11.0f * sc, dr = 3.8f * sc;
    draw_diamond(dl, ImVec2(a.x + dc, a.y + dc), dr, dr, kColGold);
    draw_diamond(dl, ImVec2(b.x - dc, a.y + dc), dr, dr, kColGold);
    draw_diamond(dl, ImVec2(a.x + dc, b.y - dc), dr, dr, kColGold);
    draw_diamond(dl, ImVec2(b.x - dc, b.y - dc), dr, dr, kColGold);
}

// Sacred Triforce & Goddess Wing emblem drawn with pure vector geometry
static void draw_goddess_crest(ImDrawList *dl, ImVec2 c, float u, ImU32 col, ImU32 wing_col) {
    float h = u * 0.866f;
    // Top triangle
    dl->AddTriangleFilled(ImVec2(c.x, c.y - h), ImVec2(c.x - u * 0.5f, c.y), ImVec2(c.x + u * 0.5f, c.y), col);
    // Bottom-left triangle
    dl->AddTriangleFilled(ImVec2(c.x - u * 0.5f, c.y), ImVec2(c.x - u, c.y + h), ImVec2(c.x, c.y + h), col);
    // Bottom-right triangle
    dl->AddTriangleFilled(ImVec2(c.x + u * 0.5f, c.y), ImVec2(c.x, c.y + h), ImVec2(c.x + u, c.y + h), col);
    // Swept Loftwing / Goddess crest wings on left and right
    for (int side : {-1, 1}) {
        float sx = (float)side;
        ImVec2 w0(c.x + sx * u * 1.15f, c.y + h * 0.55f);
        ImVec2 w1(c.x + sx * u * 2.55f, c.y - h * 0.15f);
        ImVec2 w2(c.x + sx * u * 2.15f, c.y + h * 0.35f);
        ImVec2 w3(c.x + sx * u * 1.35f, c.y + h * 0.85f);
        dl->AddQuadFilled(w0, w1, w2, w3, wing_col);
    }
}

// ------------------------------------------------------------------ update check
// The start screen asks GitHub for the project's releases once per launch (a background curl, which macOS and
// Windows 10+ ship) and offers the best one if it outranks this build: any v1.0.0+ above every beta, betas by
// number; other tags (the alpha_* ones) are ignored. Offline / no curl / anything unexpected: nothing is shown.
#define RELEASES_URL "https://api.github.com/repos/Apolinario-coder/SkyRecomp/releases?per_page=100"
#define RELEASE_PAGE "https://github.com/Apolinario-coder/SkyRecomp/releases/tag/"

static long long tag_rank(const std::string &t) {
    int n, a, b, c;
    char end;
    if (sscanf(t.c_str(), "beta_%d%c", &n, &end) == 1 && n > 0) return n;
    if (sscanf(t.c_str(), "v%d.%d.%d%c", &a, &b, &c, &end) == 3 && a >= 1 && b >= 0 && c >= 0 && b < 4096 && c < 4096)
        return (1LL << 48) + ((long long)a << 24) + ((long long)b << 12) + c;
    return -1;
}

// "Beta 2" / "1.0.0" (with_word: "Version 1.0.0")
static std::string tag_label(const std::string &t, bool with_word) {
    int n;
    char end;
    if (sscanf(t.c_str(), "beta_%d%c", &n, &end) == 1) return "Beta " + std::to_string(n);
    std::string v = t.size() > 1 && t[0] == 'v' ? t.substr(1) : t;
    return with_word ? "Version " + v : v;
}

static std::string g_update_tag;  // a newer release's tag (g_update_lock)
static pthread_mutex_t g_update_lock = PTHREAD_MUTEX_INITIALIZER;

static void *update_thread(void *) {
    std::string body;
    if (const char *t = getenv("LARECOMP_UPDATE_TAGS")) {  // (test) "beta_2,v1.0.0": these releases, no network
        for (const char *p = t; *p;) {
            const char *c = strchr(p, ',');
            body += "\"tag_name\": \"" + std::string(p, c ? c - p : strlen(p)) + "\"\n";
            p = c ? c + 1 : p + strlen(p);
        }
    } else {
#ifdef _WIN32
        const char *sr = getenv("SystemRoot");
        std::string curl = std::string(sr ? sr : "C:\\Windows") + "\\System32\\curl.exe";
#else
        std::string curl = "/usr/bin/curl";
#endif
        std::string out = std::string(rt_data_root()) + "/cache/releases.json";
        mkdir((std::string(rt_data_root()) + "/cache").c_str(), 0755);
        const char *av[] = {curl.c_str(), "-fsSL", "--max-time", "15", "-H", "Accept: application/vnd.github+json",
                            "-A", "SkyRecomp/" LARECOMP_VERSION, RELEASES_URL, NULL};
        PlatProc p = plat_spawn(av, out.c_str(), false);
        if (!p || plat_proc_wait(p, true) != 0) { remove(out.c_str()); return NULL; }
        if (FILE *f = fopen(out.c_str(), "rb")) {
            char buf[8192];
            size_t n;
            while ((n = fread(buf, 1, sizeof buf, f)) > 0 && body.size() < (8u << 20)) body.append(buf, n);
            fclose(f);
        }
        remove(out.c_str());
    }
    std::string best;
    long long best_rank = tag_rank(LARECOMP_VERSION);
    for (size_t at = 0; (at = body.find("\"tag_name\"", at)) != std::string::npos;) {
        size_t q1 = body.find('"', body.find(':', at) + 1), q2 = q1 == std::string::npos ? q1 : body.find('"', q1 + 1);
        if (q2 == std::string::npos) break;
        std::string tag = body.substr(q1 + 1, q2 - q1 - 1);
        if (tag_rank(tag) > best_rank) best_rank = tag_rank(tag), best = tag;
        at = q2;
    }
    if (!best.empty()) fprintf(stderr, "[launcher] update available: %s (this build %s)\n", best.c_str(), LARECOMP_VERSION);
    pthread_mutex_lock(&g_update_lock);
    g_update_tag = best;
    pthread_mutex_unlock(&g_update_lock);
    return NULL;
}

// ------------------------------------------------------------------ game files
static std::string g_root;  // extracted game: <root>/exefs, <root>/romfs

static bool file_exists(const std::string &p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}

// The copied game files alone ("" = there), without the game code built from them
static std::string check_copied_files(const std::string &root) {
    FILE *f = fopen((root + "/exefs/main").c_str(), "rb");
    if (!f) return "No game files";
    uint8_t id[8] = {0};
    fseek(f, 0x40, SEEK_SET);
    size_t n = fread(id, 1, 8, f);
    fclose(f);
    if (n != 8 || memcmp(id, BUILD_ID, 8)) return "Wrong game version (needs v1.0.0)";
    if (!file_exists(root + "/romfs.bin") && !file_exists(root + "/romfs/region_common") && !file_exists(root + "/romfs/Object")) return "Game data missing";
    return "";
}

// "" = ready to play, else what is wrong
static std::string check_game_files(const std::string &root) {
    std::string why = check_copied_files(root);
    if (why.empty() && !gamelib_present(root.c_str())) return "Game code not built yet";
    return why;
}

// ------------------------------------------------------------------ setup (background thread)
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static std::string g_src;           // the picked folder
static std::string g_job;           // current setup step / result for the status line
static std::string g_job_hint;      // a failure: what to do about it (shown under it)
static bool g_busy, g_job_failed;
static bool g_confirm_delete;       // game-files page: "Delete the game files?" shown
static std::atomic<bool> g_play_requested{false};

static void set_job(const std::string &s, bool failed = false, const std::string &hint = "") {
    pthread_mutex_lock(&g_lock);
    g_job = s;
    g_job_failed = failed;
    g_job_hint = hint;
    pthread_mutex_unlock(&g_lock);
}

// Setup failures: a short headline and what to do next (also LARECOMP_LAUNCHER_SHOT=<f>:fail<1-6>, screenshots)
enum { FAIL_NOT_FOUND = 1, FAIL_UNUSABLE, FAIL_COPY, FAIL_VERSION, FAIL_MISSING, FAIL_BUILD };
static void set_failure(int k) {
    static const char *const head[] = {
        "",
        "No game files found in that folder",
        "Packaged or archived files cannot be used directly",
        "Setup could not finish copying files",
        "Unsupported game version",
        "Game files are incomplete",
        "The game code could not be built"};
    static const char *const hint[] = {
        "",
        "Select the folder that contains your unpacked game files and assets.",
        "Unpack your game files into a regular folder first, then select that folder.",
        "Check that your drive has at least 8 GB of free space and try again.",
        "Sky Recomp requires the base 1.0.0 version of the game without update patches.",
        "Some required program or asset files are missing. Verify your source folder and try again.",
        "Make sure the skygen and zig folders are next to skyrecomp.exe, then try again."};
    if (k >= 1 && k <= 6) set_job(head[k], true, hint[k]);
}

static bool is_dir(const std::string &p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// exefs: the folder with main and main.npdm; romfs: the folder with Object/THP (the game's data root). Found
// in the picked folder or a few levels below it (dump tools nest them).
static void find_game(const std::string &dir, int depth, std::string &exefs, std::string &romfs, bool &unusable) {
    if (depth > 4 || (!exefs.empty() && !romfs.empty())) return;
    if (exefs.empty() && file_exists(dir + "/main")) exefs = dir;
    if (romfs.empty() && (is_dir(dir + "/Object") || is_dir(dir + "/THP") || is_dir(dir + "/region_common") || file_exists(dir + "/romfs.bin"))) romfs = dir;
    DIR *d = opendir(dir.c_str());
    if (!d) return;
    std::vector<std::string> sub;
    while (struct dirent *e = readdir(d)) {
        std::string n = e->d_name;
        if (n[0] == '.') continue;
        std::string ext = n.size() > 4 ? n.substr(n.size() - 4) : "";
        for (auto &ch : ext) ch = (char)tolower((unsigned char)ch);
        if (ext == ".xci" || ext == ".nsp" || ext == ".nca" || ext == ".nsz" || ext == ".xcz") unusable = true;
        if (n != "region_common" && is_dir(dir + "/" + n)) sub.push_back(dir + "/" + n);
    }
    closedir(d);
    std::sort(sub.begin(), sub.end());
    for (auto &s : sub) find_game(s, depth + 1, exefs, romfs, unusable);
}

static bool build_id_ok(const std::string &main_path) {
    FILE *f = fopen(main_path.c_str(), "rb");
    if (!f) return false;
    uint8_t id[8] = {0};
    fseek(f, 0x40, SEEK_SET);
    size_t n = fread(id, 1, 8, f);
    fclose(f);
    return n == 8 && !memcmp(id, BUILD_ID, 8);
}

// progress (the overlay's bar): the romfs copy, then the image build - each about the size of romfs/
static std::atomic<unsigned long long> g_done, g_total;
// The bar: copying and the data image are the first tenth, building the game code (runtime/gamebuild.c) the rest
static unsigned long long g_phase_base;
static std::atomic<float> g_build_frac{-1};  // -1: not building
static bool g_copy_skipped;                  // (only the game code to build: the bar is all of that)
static void setup_progress(uint64_t done, uint64_t) { g_done = g_phase_base + done; }
static void build_progress(double f, const char *step) {
    g_build_frac = (float)f;
    static const char *last;
    if (step != last) last = step, set_job(step);
}
static float extract_progress(void) {
    unsigned long long t = g_total, d = g_done;
    float b = g_build_frac, f = b >= 0 ? (g_copy_skipped ? b : 0.1f + 0.9f * b) : t ? 0.1f * (float)((double)d / (double)t) : 0;
    return f < 0 ? 0 : f > 0.99f ? 0.99f : f;
}

// Builds the game code library from the copied exefs (runtime/gamebuild.c), unless a matching one is there already
static bool build_game_code(void) {
    if (gamelib_present(g_root.c_str())) return true;
    int cores = SDL_GetNumLogicalCPUCores(), ram_gb = SDL_GetSystemRAM() / 1024;
    GameBuildOpts o = {build_progress, cores < ram_gb - 2 ? cores : ram_gb - 2 > 1 ? ram_gb - 2 : 1};
    char why[256] = "";
    g_build_frac = 0;
    bool ok = gamebuild_run(g_root.c_str(), &o, why, sizeof why);
    if (!ok) {
        fprintf(stderr, "[launcher] game code: %s\n", why);
        set_failure(FAIL_BUILD);
    }
    return ok;
}

static void *extract_thread(void *) {
    pthread_mutex_lock(&g_lock);
    std::string src = g_src;
    pthread_mutex_unlock(&g_lock);

    g_done = 0, g_total = 0, g_phase_base = 0, g_build_frac = -1, g_copy_skipped = false;

    // 1. If game files are already present and valid, launch directly
    if (check_game_files(g_root).empty()) {
        set_job("Game files ready.");
        g_play_requested = true;
        goto done;
    }

    if (!src.empty() || check_copied_files(g_root).empty()) {
        std::string exefs, romfs;
        bool unusable = false;
        if (src.empty() && check_copied_files(g_root).empty()) {
            g_copy_skipped = true;
            if (!build_game_code()) goto done;
            goto check;
        }
        set_job("Scanning folder for game files...");
        find_game(src, 0, exefs, romfs, unusable);

        if (exefs.empty() && file_exists(src + "/main")) exefs = src;
        if (romfs.empty()) {
            if (file_exists(g_root + "/romfs.bin") || is_dir(g_root + "/romfs/region_common") || is_dir(g_root + "/romfs/Object")) {
                romfs = g_root;
            } else if (file_exists(src + "/../romfs.bin")) {
                romfs = src + "/../romfs.bin";
            } else if (is_dir(src + "/../romfs")) {
                romfs = src + "/../romfs";
            }
        }

        if (exefs.empty()) {
            set_failure(unusable ? FAIL_UNUSABLE : FAIL_NOT_FOUND);
            goto done;
        }
        if (!build_id_ok(exefs + "/main")) {
            set_failure(FAIL_VERSION);
            goto done;
        }
        if (romfs.empty() && !file_exists(g_root + "/romfs.bin") && !is_dir(g_root + "/romfs/region_common")) {
            set_failure(FAIL_MISSING);
            goto done;
        }

        {
            mkdir(g_root.c_str(), 0755);
            set_job("Copying executable modules...");
            mkdir((g_root + "/exefs").c_str(), 0755);
            if (!romfs_copy_tree(exefs.c_str(), (g_root + "/exefs").c_str(), NULL)) {
                set_failure(FAIL_COPY);
                goto done;
            }

            if (file_exists(romfs) && !is_dir(romfs)) {
                set_job("Copying game data archive...");
                FILE *in = fopen(romfs.c_str(), "rb");
                FILE *out = fopen((g_root + "/romfs.bin").c_str(), "wb");
                if (in && out) {
                    static char buf[1024 * 1024];  // (not on the stack: macOS gives this thread 512 KB)
                    size_t n;
                    while ((n = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, n, out);
                }
                if (in) fclose(in);
                if (out) fclose(out);
            } else if (file_exists(romfs + "/romfs.bin")) {
                set_job("Copying game data archive...");
                FILE *in = fopen((romfs + "/romfs.bin").c_str(), "rb");
                FILE *out = fopen((g_root + "/romfs.bin").c_str(), "wb");
                if (in && out) {
                    static char buf[1024 * 1024];  // (not on the stack: macOS gives this thread 512 KB)
                    size_t n;
                    while ((n = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, n, out);
                }
                if (in) fclose(in);
                if (out) fclose(out);
            } else if (romfs != g_root && is_dir(romfs)) {
                unsigned long long rbytes = romfs_dir_bytes(romfs.c_str());
                g_total = 2 * rbytes;
                set_job("Copying game assets...");
                if (!romfs_copy_tree(romfs.c_str(), (g_root + "/romfs").c_str(), setup_progress)) {
                    set_failure(FAIL_COPY);
                    goto done;
                }
                g_phase_base = rbytes, g_done = rbytes;
                set_job("Building game data image...");
                char why[256] = "";
                if (!romfs_build((g_root + "/romfs").c_str(), (g_root + "/romfs.bin").c_str(), setup_progress, why, sizeof why)) {
                    set_failure(FAIL_COPY);
                    goto done;
                }
            }

            if (!build_game_code()) goto done;
        }
    } else {
        set_job("Select your game files folder first.", true, "Choose 'Game files folder' above to pick the folder with your game files.");
        goto done;
    }

check:
    {
        std::string why = check_game_files(g_root);
        if (why.empty()) {
            set_job("Game files ready.");
            g_done = 100;
            g_play_requested = true;
        } else {
            set_failure(why.find("version") != std::string::npos ? FAIL_VERSION : FAIL_MISSING);
        }
    }
done:
    pthread_mutex_lock(&g_lock);
    g_busy = false;
    pthread_mutex_unlock(&g_lock);
    return NULL;
}

// Set up in the background, on a stack like the main thread's (macOS gives other threads 512 KB by default)
static void start_extract(void) {
    pthread_attr_t a;
    pthread_attr_init(&a);
    int error = pthread_attr_setstacksize(&a, 8 << 20);
    if (!error) error = pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
    pthread_t t;
    if (!error) error = pthread_create(&t, &a, extract_thread, NULL);
    pthread_attr_destroy(&a);
    if (error) {
        set_job("Setup could not start.", true, "Close other running applications and try again.");
        pthread_mutex_lock(&g_lock);
        g_busy = false;
        pthread_mutex_unlock(&g_lock);
    }
}

// ------------------------------------------------------------------ folder dialog (SDL calls back on any thread)
static void SDLCALL picked(void *, const char *const *files, int) {
    if (!files || !files[0]) return;
    pthread_mutex_lock(&g_lock);
    g_src = files[0];
    pthread_mutex_unlock(&g_lock);
}


// ------------------------------------------------------------------ the user's own Nintendo fonts (not shipped)
// runtime/hle/pl.c loads fonts/ first, else a close substitute (Hiragino Sans). Import copies recognized files only.
static const char *FONT_STD[] = {"nintendo_udsg-r_std_003.bfttf", "nintendo_udsg-r_std_003.ttf",
                                 "nintendo_udsgr_std_003.ttf", "nintendo_udsgr_std_003.bfttf"};
static const char *FONT_EXT[] = {"nintendo_ext_003.bfttf", "nintendo_ext_003.ttf"};
static std::vector<std::string> g_font_picks;
static std::string g_font_msg;

static bool have_font(const char *const *names, int n) {
    for (int i = 0; i < n; i++)
        if (file_exists(std::string("fonts/") + names[i])) return true;
    return false;
}

static void SDLCALL picked_fonts(void *, const char *const *files, int) {
    if (!files) return;
    pthread_mutex_lock(&g_lock);
    for (int i = 0; files[i]; i++) g_font_picks.push_back(files[i]);
    pthread_mutex_unlock(&g_lock);
}

static void import_fonts() {
    pthread_mutex_lock(&g_lock);
    std::vector<std::string> picks;
    picks.swap(g_font_picks);
    pthread_mutex_unlock(&g_lock);
    if (picks.empty()) return;
    mkdir("fonts", 0755);
    int ok = 0, skipped = 0;
    for (auto &src : picks) {
        std::string base = src.substr(src.find_last_of("/\\") + 1), low = base;
        for (auto &c : low) c = (char)tolower(c);
        bool known = false;
        for (auto n : FONT_STD) known |= low == n;
        for (auto n : FONT_EXT) known |= low == n;
        if (!known) { skipped++; continue; }
        FILE *in = fopen(src.c_str(), "rb"), *out = fopen(("fonts/" + low).c_str(), "wb");
        if (in && out) {
            char buf[65536];
            size_t n;
            while ((n = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, n, out);
            ok++;
        }
        if (in) fclose(in);
        if (out) fclose(out);
    }
    g_font_msg = std::to_string(ok) + " font file(s) imported" + (skipped ? ", " + std::to_string(skipped) + " skipped" : "") +
                 ". Changes take effect next time you start the game.";
}

static void remove_fonts() {
    for (auto n : FONT_STD) unlink((std::string("fonts/") + n).c_str());
    for (auto n : FONT_EXT) unlink((std::string("fonts/") + n).c_str());
    g_font_msg = "Imported fonts removed. The built-in fallback font will be used.";
}

// ------------------------------------------------------------------ background: a title movie frame
#ifdef __APPLE__
static id<MTLTexture> g_bg;
#define BG_TEXID ((ImTextureID)(__bridge void *)g_bg)
typedef id<MTLDevice> GpuDevice;
#else
static uint64_t g_bg;  // ImGui texture id (Vulkan descriptor set)
#define BG_TEXID ((ImTextureID)g_bg)
typedef void *GpuDevice;
#endif
static int g_bg_w, g_bg_h;

static void load_background(GpuDevice dev) {
    std::string path = g_root + "/romfs/THP/Demo01_01.webm";
    char res_buf[1024];
    if (!file_exists(path) && rt_resource("Gamedata/THP/Demo01_01.webm", NULL, res_buf, sizeof res_buf)) path = res_buf;
    if (!file_exists(path)) path = g_root + "/romfs/region_common/movie/title.webm";
    AVFormatContext *fmt = NULL;
    if (avformat_open_input(&fmt, path.c_str(), NULL, NULL) < 0) return;
    int si = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (si >= 0 && avformat_find_stream_info(fmt, NULL) >= 0) {
        AVStream *st = fmt->streams[si];
        const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
        AVCodecContext *cc = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(cc, st->codecpar);
        if (codec && avcodec_open2(cc, codec, NULL) == 0) {
            AVPacket *pkt = av_packet_alloc();
            AVFrame *fr = av_frame_alloc();
            bool is_demo = path.find("Demo01_01.webm") != std::string::npos;
            int64_t want = (int64_t)((is_demo ? 112.0 : 6.0) / av_q2d(st->time_base));  // Goddess Hylia fresco / island
            av_seek_frame(fmt, si, want, AVSEEK_FLAG_BACKWARD);
            bool got = false;
            while (!got && av_read_frame(fmt, pkt) >= 0) {
                if (pkt->stream_index == si && avcodec_send_packet(cc, pkt) == 0)
                    while (avcodec_receive_frame(cc, fr) == 0)
                        if (fr->best_effort_timestamp >= want) { got = true; break; }
                av_packet_unref(pkt);
            }
            if (got) {
                int w = fr->width, h = fr->height;
                std::vector<uint8_t> rgba((size_t)w * h * 4);
                SwsContext *sws = sws_getContext(w, h, (AVPixelFormat)fr->format, w, h, AV_PIX_FMT_RGBA, SWS_BILINEAR, NULL, NULL, NULL);
                uint8_t *dst[1] = {rgba.data()};
                int dst_ls[1] = {w * 4};
                sws_scale(sws, fr->data, fr->linesize, 0, h, dst, dst_ls);
                sws_freeContext(sws);
#ifdef __APPLE__
                MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                              width:w height:h mipmapped:NO];
                g_bg = [dev newTextureWithDescriptor:td];
                [g_bg replaceRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0 withBytes:rgba.data() bytesPerRow:w * 4];
#else
                (void)dev;
                g_bg = vk_imgui_texture_rgba(rgba.data(), w, h);
#endif
                g_bg_w = w, g_bg_h = h;
            }
            av_frame_free(&fr);
            av_packet_free(&pkt);
        }
        avcodec_free_context(&cc);
    }
    avformat_close_input(&fmt);
}

// ------------------------------------------------------------------ seeds
static std::vector<std::string> list_seeds() {
    std::vector<std::string> out;
    if (DIR *d = opendir("rando")) {
        while (struct dirent *e = readdir(d)) {
            if (e->d_name[0] == '.') continue;
            std::string dir = std::string("rando/") + e->d_name;
            if (file_exists(dir + "/settings.json") && file_exists(dir + "/romfs")) out.push_back(e->d_name);
        }
        closedir(d);
    }
    std::sort(out.begin(), out.end());
    return out;
}

// ------------------------------------------------------------------ screen
static ImFont *g_font_title, *g_font_menu, *g_font_small;
enum Page { MAIN, SETTINGS, SETUP };  // SETTINGS: the in-game menu's tabs (runtime/ui/menu.mm)
static int g_refocus = MAIN;  // back on the main page: select the item of the page we came from (controllers)

// A big menu line: gold + a marker when focused / hovered (keyboard, gamepad or mouse), grey when disabled
static bool g_menu_small;  // sub pages: buttons in the small font
void menu_tabs_draw(float scale, ImFont *font, ImFont *font_big, bool in_game, void (*prelaunch)(float));  // menu.mm
void menu_tabs_focus(bool first_tab);
extern int g_tab_request_ext;
extern "C" void menu_apply_window_requests(void *sdl_window);
extern "C" int g_start_screen;  // runtime/hle/warp.c: the start screen is up (no game loaded: don't read its memory)
// The Randomizer tab's Play / Play Now on the start screen (rando_tab.mm): start the game with that seed
static std::string g_play_seed;
static bool g_play_seed_req, g_play_pending;
extern "C" void launcher_play_seed(const char *seed) {
    g_play_seed = seed ? seed : "";
    g_play_seed_req = true;
}
static float g_menu_gap = 0.35f;  // space under each item, in line heights

static bool g_tip_test;  // (LARECOMP_LAUNCHER_SHOT=<frame>:tip)

// First PLAY ever, before the menu note: authentic 16:9 or the unlocked (fill the window) aspect ratio.
static bool aspect_prompt(float s) {
    bool done = false;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(fminf(ds.x * 0.82f, 780 * s), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(38 * s, 30 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16 * s);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.05f, 0.09f, 0.10f, 0.97f));
    bool open = ImGui::BeginPopupModal("##aspect", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    if (open) {
        ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
        draw_tablet_frame(ImGui::GetWindowDrawList(), wp, ImVec2(wp.x + ws.x, wp.y + ws.y), s, 16.0f, IM_COL32(0, 0, 0, 0));
        ImGui::PushFont(g_font_menu);
        ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.38f, 1.0f), "Choose your display mode");
        ImGui::PopFont();
        ImGui::SetWindowFontScale(1.18f);
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted("Select how the game camera fits your screen. You can change this at any time in Settings > Video.");
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 16 * s));
        float bw = 310 * s, gap = 20 * s;
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 2 * bw - gap) * 0.5f);
        int pick = -1;
        ImGui::SetItemDefaultFocus();
        if (ImGui::Button("Original 16:9", ImVec2(bw, 48 * s))) pick = ASPECT_ORIGINAL;
        ImGui::SameLine(0, gap);
        if (ImGui::Button("Unlocked (fill window)", ImVec2(bw, 48 * s))) pick = ASPECT_UNLOCKED;
        ImGui::SetWindowFontScale(1.0f);
        if (pick >= 0) {
            g_settings.aspect = pick;
            g_settings.aspect_asked = true;
            settings_save();
            ImGui::CloseCurrentPopup();
            done = true;
        }
        ImGui::EndPopup();
    }
    return done;
}

static bool menu_tip(float s) {
    bool go = false;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(fminf(ds.x * 0.82f, 780 * s), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(38 * s, 30 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16 * s);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.05f, 0.09f, 0.10f, 0.97f));
    bool open = ImGui::BeginPopupModal("##menutip", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    if (open) {
        ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
        draw_tablet_frame(ImGui::GetWindowDrawList(), wp, ImVec2(wp.x + ws.x, wp.y + ws.y), s, 16.0f, IM_COL32(0, 0, 0, 0));
        static const char *const home[] = {"Home button", "Xbox button", "PlayStation button"};
        static const char *const pad[] = {"Minus + Plus", "View + Menu", "Create + Options"};
        ImGui::PushFont(g_font_menu);
        ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.38f, 1.0f), "Opening the in-game menu");
        ImGui::PopFont();
        ImGui::SetWindowFontScale(1.18f);
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted("Access controls, video, audio, and mods at any time while playing.");
        ImGui::Spacing();
        int fam = bind_label_family() % 3;
        ImGui::Text("Press F1 or Esc on a keyboard, or press the %s (or %s together) on your controller.", home[fam], pad[fam]);
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 16 * s));
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 220 * s) * 0.5f);
        ImGui::SetItemDefaultFocus();
        if (ImGui::Button("Start game", ImVec2(220 * s, 48 * s)) || ImGui::IsKeyPressed(ImGuiKey_Enter) ||
            ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown)) {
            g_settings.menu_tip_seen = true;
            settings_save();
            ImGui::CloseCurrentPopup();
            go = true;
        }
        ImGui::SetWindowFontScale(1.0f);
        ImGui::EndPopup();
    }
    return go;
}

// Start-screen text: a soft drop shadow, then the text with a top-to-bottom colour gradient (top == bottom: flat)
static void text_fx(ImDrawList *dl, ImFont *font, float size, ImVec2 p, ImU32 top, ImU32 bottom, const char *t,
                    float shadow) {
    if (shadow > 0)
        for (float d : {shadow, shadow * 0.5f}) dl->AddText(font, size, ImVec2(p.x + d, p.y + d), IM_COL32(4, 10, 18, 135), t);
    int v0 = dl->VtxBuffer.Size;
    dl->AddText(font, size, p, top, t);
    if (top == bottom) return;
    float y0 = p.y + size * 0.2f, y1 = p.y + size * 0.85f;
    for (int i = v0; i < dl->VtxBuffer.Size; i++) {
        float f = (dl->VtxBuffer[i].pos.y - y0) / (y1 - y0);
        f = f < 0 ? 0 : f > 1 ? 1 : f;
        ImVec4 a = ImGui::ColorConvertU32ToFloat4(top), b = ImGui::ColorConvertU32ToFloat4(bottom);
        dl->VtxBuffer[i].col = ImGui::ColorConvertFloat4ToU32(ImVec4(a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f,
                                                                     a.z + (b.z - a.z) * f, a.w + (b.w - a.w) * f));
    }
}

// A four-point sparkle (PLAY's twinkle): k = 0..1 grows and fades it
static void sparkle(ImDrawList *dl, ImVec2 c, float r, float k, float spin) {
    if (k <= 0) return;
    ImVec2 pts[8];
    for (int i = 0; i < 8; i++) {
        float a = spin + i * 3.14159265f / 4, rr = (i & 1 ? r * 0.22f : r) * k;
        pts[i] = ImVec2(c.x + cosf(a) * rr, c.y + sinf(a) * rr);
    }
    dl->AddCircleFilled(c, r * 0.55f * k, IM_COL32(255, 230, 150, (int)(65 * k)));  // soft glow
    dl->AddConvexPolyFilled(pts, 8, IM_COL32(255, 248, 215, (int)(240 * k)));
}

// Atmospheric Skyloft background: drifting sacred light motes and cloud-sea horizon warmth
static void draw_skyloft_atmosphere(ImDrawList *bg, ImVec2 ds, float s, bool has_video_bg) {
    float t = (float)ImGui::GetTime();
    if (!has_video_bg) {
        // Painterly Skyloft twilight sky & golden cloud horizon when title movie is not yet extracted
        bg->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(ds.x, ds.y * 0.62f),
                                    IM_COL32(12, 26, 38, 255), IM_COL32(26, 52, 64, 255),
                                    IM_COL32(46, 78, 76, 255), IM_COL32(18, 36, 44, 255));
        bg->AddRectFilledMultiColor(ImVec2(0, ds.y * 0.62f), ds,
                                    IM_COL32(18, 36, 44, 255), IM_COL32(46, 78, 76, 255),
                                    IM_COL32(28, 38, 36, 255), IM_COL32(10, 18, 22, 255));
        // Subtle sacred golden sun-glow on the right horizon
        ImVec2 sun(ds.x * 0.74f, ds.y * 0.44f);
        for (int i = 5; i >= 1; i--) {
            float rad = (90.0f + i * 85.0f) * s;
            bg->AddCircleFilled(sun, rad, IM_COL32(238, 194, 96, 7 * (6 - i)), 48);
        }
        // Subtle Goddess Crest watermark in the upper-right sky
        draw_goddess_crest(bg, ImVec2(ds.x * 0.74f, ds.y * 0.40f), 64.0f * s,
                           IM_COL32(238, 198, 92, 22), IM_COL32(238, 198, 92, 14));
    }
    // Gentle drifting Skyloft light motes (deterministic function of time t)
    for (int i = 0; i < 18; i++) {
        float fi = (float)i;
        float speed = 0.012f + (i % 5) * 0.004f;
        float px = fmodf(0.12f + fi * 0.053f + sinf(t * 0.35f + fi * 1.7f) * 0.03f, 1.0f);
        float py = 1.0f - fmodf(fi * 0.173f + t * speed, 1.0f);
        float pulse = 0.5f + 0.5f * sinf(t * 1.8f + fi * 2.3f);
        float rad = (2.0f + (i % 3) * 1.3f) * s;
        ImU32 col = (i % 4 == 0)
                  ? IM_COL32(102, 214, 206, (int)(45 * pulse))
                  : IM_COL32(255, 226, 136, (int)(55 * pulse));
        bg->AddCircleFilled(ImVec2(ds.x * px, ds.y * py), rad * 2.2f, IM_COL32(255, 215, 110, (int)(16 * pulse)));
        bg->AddCircleFilled(ImVec2(ds.x * px, ds.y * py), rad, col);
    }
}

// Smooth per-item hover/focus interpolation state
struct MenuItemAnim { ImGuiID id; float k; };
static MenuItemAnim g_item_anims[8];

static float step_item_anim(ImGuiID id, bool hot, float dt) {
    MenuItemAnim *slot = &g_item_anims[0];
    for (int i = 0; i < 8; i++) {
        if (g_item_anims[i].id == id || g_item_anims[i].id == 0) {
            slot = &g_item_anims[i];
            slot->id = id;
            break;
        }
    }
    float target = hot ? 1.0f : 0.0f;
    float rate = hot ? 14.0f : 10.0f;
    slot->k += (target - slot->k) * fminf(1.0f, dt * rate);
    if (fabsf(slot->k - target) < 0.005f) slot->k = target;
    return slot->k;
}

// split .. split_end: that part of the label in its own colour (PLAY's choice: Vanilla in warm cream)
static bool menu_item(const char *label, bool enabled, bool *hovered = NULL, const char *split = NULL, ImU32 col2 = 0,
                      const char *split_end = NULL) {
    ImGui::PushFont(g_menu_small ? g_font_small : g_font_menu);
    ImGui::BeginDisabled(!enabled);
    ImVec2 at = ImGui::GetCursorScreenPos();
    // (the ID from the label's fixed part: PLAY's changes with the seed shown, and a new ID loses the selection)
    ImGui::PushID(label, split ? split : label + strlen(label));
    ImGuiID item_id = ImGui::GetID("##b");
    ImVec2 sz = ImGui::CalcTextSize(label);
    ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));  // (the hover band shows the selection, no box)
    bool clicked = ImGui::InvisibleButton("##b", ImVec2(sz.x + 56, sz.y + 8), ImGuiButtonFlags_EnableNav);
    ImGui::PopStyleColor();
    bool hot = ImGui::IsItemFocused() || ImGui::IsItemHovered();
    ImGui::PopID();
    if (hovered) *hovered = hot;

    float dt = ImGui::GetIO().DeltaTime > 0 ? ImGui::GetIO().DeltaTime : (1.0f / 60.0f);
    float anim = enabled ? step_item_anim(item_id, hot, dt) : 0.0f;
    // Cubic ease-out feel on the horizontal slide and glow
    float slide = anim * (2.0f - anim) * (sz.y * 0.16f);

    ImU32 col = !enabled ? IM_COL32(155, 162, 168, 130)
              : hot      ? IM_COL32(255, 228, 118, 255)
                         : IM_COL32(248, 244, 232, 255);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    if (anim > 0.01f && enabled) {
        // Feathered Skyloft slate banner + sacred gold hairline borders
        float x0 = ImGui::GetWindowPos().x;
        float x1 = at.x + sz.x * 0.80f + sz.y * 0.6f;
        float x2 = at.x + sz.x + sz.y * 3.8f;
        float f = sz.y * 0.20f, y0 = at.y - f * 0.35f, y1 = y0 + f * 0.75f, y3 = at.y + sz.y + f * 0.25f, y2 = y3 - f * 0.75f;
        ImU32 c = IM_COL32(8, 18, 24, (int)(185 * anim));
        ImU32 g = IM_COL32(238, 190, 76, (int)(44 * anim));
        ImU32 n = IM_COL32(8, 18, 24, 0), ng = IM_COL32(238, 190, 76, 0);
        for (int pass = 0; pass < 2; pass++) {
            ImU32 a = pass ? g : c, z = pass ? ng : n;
            dl->AddRectFilledMultiColor(ImVec2(x0, y1), ImVec2(x1, y2), a, a, a, a);
            dl->AddRectFilledMultiColor(ImVec2(x1, y1), ImVec2(x2, y2), a, z, z, a);
            dl->AddRectFilledMultiColor(ImVec2(x0, y0), ImVec2(x1, y1), z, z, a, a);
            dl->AddRectFilledMultiColor(ImVec2(x1, y0), ImVec2(x2, y1), z, z, z, a);
            dl->AddRectFilledMultiColor(ImVec2(x0, y2), ImVec2(x1, y3), a, a, z, z);
            dl->AddRectFilledMultiColor(ImVec2(x1, y2), ImVec2(x2, y3), a, z, z, z);
        }
        // Subtle top and bottom golden hairlines along the active banner
        ImU32 hl_l = IM_COL32(238, 198, 92, (int)(135 * anim)), hl_r = IM_COL32(238, 198, 92, 0);
        dl->AddRectFilledMultiColor(ImVec2(x0, y1), ImVec2(x1, y1 + 1.5f), hl_l, hl_r, hl_r, hl_l);
        dl->AddRectFilledMultiColor(ImVec2(x0, y2 - 1.5f), ImVec2(x1, y2), hl_l, hl_r, hl_r, hl_l);

        // Skyward Sword sacred diamond pointer to the left of the active menu item
        float cy = at.y + sz.y * 0.50f;
        float dx = at.x - sz.y * 0.34f + slide * 0.45f;
        float dr = sz.y * 0.14f * anim;
        draw_diamond(dl, ImVec2(dx, cy), dr * 1.25f, dr, IM_COL32(255, 226, 132, (int)(255 * anim)));
        draw_diamond(dl, ImVec2(dx, cy), dr * 0.55f, dr * 0.45f, IM_COL32(255, 250, 225, (int)(255 * anim)));
    }

    ImVec2 draw_at(at.x + slide, at.y);
    ImU32 col_b = hot && enabled ? IM_COL32(242, 152, 48, 255) : col;
    if (split && enabled) {
        std::string head(label, split - label);
        std::string mid = split_end ? std::string(split, split_end - split) : std::string(split);
        ImVec2 p2(draw_at.x + ImGui::CalcTextSize(label, split).x, draw_at.y);
        // Pill frame drawn before text so both ‹ and › chevrons render cleanly on top
        const char *lt = strstr(label, "\xe2\x80\xb9");
        float pill_x0 = lt ? draw_at.x + ImGui::CalcTextSize(label, lt).x - sz.y * 0.20f : p2.x - sz.y * 0.28f;
        float pill_x1 = draw_at.x + sz.x + sz.y * 0.20f;
        float pad_y = sz.y * 0.04f;
        ImVec2 pa(pill_x0, p2.y + pad_y), pb(pill_x1, p2.y + sz.y - pad_y);
        dl->AddRectFilled(pa, pb, hot ? IM_COL32(22, 42, 44, 215) : IM_COL32(12, 22, 26, 175), (pb.y - pa.y) * 0.46f);
        dl->AddRect(pa, pb, hot ? IM_COL32(238, 198, 92, 175) : IM_COL32(238, 198, 92, 85), (pb.y - pa.y) * 0.46f, 0, 1.5f);

        text_fx(dl, ImGui::GetFont(), ImGui::GetFontSize(), draw_at, col, col_b, head.c_str(), sz.y * 0.05f);
        text_fx(dl, ImGui::GetFont(), ImGui::GetFontSize(), p2, col2, col2, mid.c_str(), sz.y * 0.05f);
        if (split_end)
            text_fx(dl, ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(draw_at.x + ImGui::CalcTextSize(label, split_end).x, draw_at.y),
                    col, col, split_end, sz.y * 0.05f);
    } else {
        text_fx(dl, ImGui::GetFont(), ImGui::GetFontSize(), draw_at, col, col_b, label, enabled ? sz.y * 0.05f : 0);
    }
    if (g_menu_gap >= 0) ImGui::Dummy(ImVec2(0, sz.y * g_menu_gap));  // line spacing
    else ImGui::SetCursorPosY(ImGui::GetCursorPosY() + sz.y * g_menu_gap);  // (tighter than the font's line height)
    ImGui::EndDisabled();
    ImGui::PopFont();
    return clicked;
}

// Controllers, as in the in-game menu (runtime/ui/menu.mm): read straight from SDL, Nintendo layout - the right face
// button (A) confirms, the bottom one (B) goes back; d-pad / left stick move.
static ImGuiKey launcher_pad_key(Uint8 b) {
    switch (b) {
    case SDL_GAMEPAD_BUTTON_EAST: return ImGuiKey_GamepadFaceDown;   // A: activate
    case SDL_GAMEPAD_BUTTON_SOUTH: return ImGuiKey_GamepadFaceRight; // B: back
    case SDL_GAMEPAD_BUTTON_NORTH: return ImGuiKey_GamepadFaceUp;
    case SDL_GAMEPAD_BUTTON_WEST: return ImGuiKey_GamepadFaceLeft;
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return ImGuiKey_GamepadDpadUp;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return ImGuiKey_GamepadDpadDown;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return ImGuiKey_GamepadDpadLeft;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return ImGuiKey_GamepadDpadRight;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return ImGuiKey_GamepadL1;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return ImGuiKey_GamepadR1;
    case SDL_GAMEPAD_BUTTON_START: return ImGuiKey_GamepadStart;
    case SDL_GAMEPAD_BUTTON_BACK: return ImGuiKey_GamepadBack;
    default: return ImGuiKey_None;
    }
}

static void launcher_pad_open_all(void) {
    int n = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&n);
    for (int i = 0; ids && i < n; i++) SDL_OpenGamepad(ids[i]);
    SDL_free(ids);
}

// true when the event was a controller event (handled here)
static bool launcher_pad_event(const SDL_Event &e) {
    ImGuiIO &io = ImGui::GetIO();
    switch (e.type) {
    case SDL_EVENT_GAMEPAD_ADDED:
        SDL_OpenGamepad(e.gdevice.which);
        io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
        return true;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        ImGuiKey k = launcher_pad_key(e.gbutton.button);
        if (k != ImGuiKey_None) io.AddKeyEvent(k, e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
        return true;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        float v = e.gaxis.value / 32767.0f;
        if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX || e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
            static bool held[4];  // left, right, up, down
            bool x = e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX;
            int neg = x ? 0 : 2, pos = neg + 1;
            const ImGuiKey keys[4] = {ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadDpadUp,
                                      ImGuiKey_GamepadDpadDown};
            for (int i : {neg, pos}) {
                float m = i == neg ? -v : v;
                bool now = held[i] ? m > 0.3f : m > 0.5f;
                if (now != held[i]) { held[i] = now; io.AddKeyEvent(keys[i], now); }
            }
        }
        if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) {
            io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickLeft, v < -0.3f, v < -0.3f ? -v : 0);
            io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickRight, v > 0.3f, v > 0.3f ? v : 0);
        } else if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
            io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickUp, v < -0.3f, v < -0.3f ? -v : 0);
            io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickDown, v > 0.3f, v > 0.3f ? v : 0);
        }
        return true;
    }
    default: return false;
    }
}

// Open a folder in Finder / Explorer (relative paths are in the data root, the working directory)
static void reveal_folder(const std::string &dir) {
    std::string path = dir;
    bool abs = !path.empty() && (path[0] == '/' || path[0] == '\\' || (path.size() > 1 && path[1] == ':'));
    if (!abs) path = std::string(rt_data_root()) + "/" + path;
    std::string url = "file://";
    if (path[0] != '/') url += "/";  // (Windows: file:///C:/...)
    for (char ch : path) {
        if (ch == '\\') ch = '/';
        if (ch == ' ') url += "%20";
        else if (ch == '%') url += "%25";
        else if (ch == '#') url += "%23";
        else url += ch;
    }
    SDL_OpenURL(url.c_str());
}

static SDL_Window *g_launcher_win;
static bool g_files_deleted;  // set here: the start screen goes back to setting the game files up
static void prelaunch_tab(float sc) {
    const ImVec4 gold(0.93f, 0.76f, 0.33f, 1);
    auto heading = [&](const char *t) {
        ImGui::PushFont(g_font_menu);
        ImGui::TextColored(gold, "%s", t);
        ImGui::PopFont();
        ImGui::Separator();
        ImGui::Spacing();
    };
    heading("Game files");
    static double gb = -1;
    if (gb < 0) gb = rt_game_files_size(g_root.c_str()) / 1e9;
    ImGui::TextColored(ImVec4(0.84f, 0.86f, 0.80f, 1.0f),
                       "Extracted game data and compiled library currently use %.1f GB.", gb > 0 ? gb : 0.0);
    ImGui::Spacing();
#ifdef _WIN32
    if (ImGui::Button("Show in Explorer")) reveal_folder(g_root);
#elif defined(__linux__)
    if (ImGui::Button("Open folder")) reveal_folder(g_root);
#else
    if (ImGui::Button("Show in Finder")) reveal_folder(g_root);
#endif
    ImGui::SameLine();
    static bool shot_del = getenv("LARECOMP_LAUNCHER_SHOT") && strstr(getenv("LARECOMP_LAUNCHER_SHOT"), ":askdelete");
    if (ImGui::Button("Delete game files") || shot_del) ImGui::OpenPopup("##delfiles"), shot_del = false;  // (test: :askdelete)
    if (ImGui::BeginPopupModal("##delfiles", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
        draw_tablet_frame(ImGui::GetWindowDrawList(), wp, ImVec2(wp.x + ws.x, wp.y + ws.y), sc, 12.0f, IM_COL32(0, 0, 0, 0));
        ImGui::PushFont(g_font_menu);
        ImGui::TextColored(ImVec4(0.96f, 0.45f, 0.38f, 1), "Delete installed game files?");
        ImGui::PopFont();
        ImGui::Text("This removes Sky Recomp's copy of the game files (%.1f GB).", gb);
        ImGui::TextDisabled("Your save files, settings, and original source folder are kept.");
        if (rt_dev_mode()) {  // (a source checkout: the files are the project's own extracted/)
            bool abs = !g_root.empty() && (g_root[0] == '/' || (g_root.size() > 1 && g_root[1] == ':'));
            std::string where = abs ? g_root : std::string(rt_data_root()) + "/" + g_root;
            ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "Dev checkout: %s", where.c_str());
        }
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.48f, 0.16f, 0.14f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.64f, 0.22f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.74f, 0.26f, 0.22f, 1.0f));
        if (ImGui::Button("Delete game files", ImVec2(230 * sc, 0))) {
            bool ok = rt_remove_game_files(g_root.c_str());  // (runtime/paths.c: only what the extraction made)
            set_job(ok ? "Game files deleted." : "Could not delete the game files (outside Sky Recomp's data folder).", !ok);
            g_files_deleted = true, gb = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(3);
        ImGui::SameLine();
        ImGui::SetItemDefaultFocus();
        if (ImGui::Button("Keep files", ImVec2(210 * sc, 0)) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::Spacing();
    ImGui::Spacing();
    heading("System fonts");
    import_fonts();
    bool fs = have_font(FONT_STD, 4), fe = have_font(FONT_EXT, 2);
    ImGui::Text("Dialogue font: %s    Controller glyphs: %s",
                fs ? "Custom (imported)" : "Built-in fallback",
                fe ? "Imported" : "Built-in fallback");
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Custom system fonts are optional. You can import compatible .bfttf or .ttf font files "
                        "for dialogue and controller glyphs, or use the built-in fallback typeface.");
    ImGui::PopTextWrapPos();
    if (!g_font_msg.empty()) ImGui::TextColored(ImVec4(0.45f, 0.92f, 0.65f, 1.0f), "%s", g_font_msg.c_str());
    static const SDL_DialogFileFilter font_f[] = {{"Font files", "bfttf;ttf"}};
    if (ImGui::Button("Import font files")) SDL_ShowOpenFileDialog(picked_fonts, NULL, g_launcher_win, font_f, 1, NULL, true);
    if (fs || fe) {
        ImGui::SameLine();
        if (ImGui::Button("Restore fallback font")) remove_fonts();
    }
    ImGui::Spacing();
    ImGui::Spacing();
    heading("Game language");
    char autol[64];
    snprintf(autol, sizeof autol, "Auto (%s)", settings_language_name());
    const char *names[N_LANGUAGES];
    names[0] = autol;
    for (int i = 1; i < N_LANGUAGES; i++) names[i] = g_language_names[i];
    int lang = g_settings.language;
    ImGui::SetNextItemWidth(280 * sc);
    if (ImGui::Combo("##language", &lang, names, N_LANGUAGES)) {
        g_settings.language = lang;
        settings_save();
    }
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Selects the in-game text language. Changes take effect when the game boots.");
    ImGui::PopTextWrapPos();
}

extern "C" void launcher_run(const char *root) {
    g_root = root;
    settings_load();
    g_start_screen = 1;
    static bool update_checked;  // (once per launch, not again after a "Title screen" reboot)
    if (!update_checked && !getenv("LARECOMP_NO_UPDATE_CHECK")) {
        update_checked = true;
        pthread_t t;
        if (pthread_create(&t, NULL, update_thread, NULL) == 0) pthread_detach(t);
    }
    ui_window_init();
    SDL_Window *win = ui_window();
    g_launcher_win = win;
#ifdef __APPLE__
    CAMetalLayer *layer = (__bridge CAMetalLayer *)ui_metal_layer();
    id<MTLDevice> dev = (__bridge id<MTLDevice>)ui_metal_device();
    id<MTLCommandQueue> queue = [dev newCommandQueue];
#else
    GpuDevice dev = NULL;
#endif

    IMGUI_CHECKVERSION();
    ImGuiContext *prev_ctx = ImGui::GetCurrentContext();  // the in-game menu's, when back here after a reboot
    ImGuiContext *ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(ctx);
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.IniFilename = NULL;
    g_font_small = g_font_menu = g_font_title = NULL;
    // Baloo 2 SemiBold (OFL, shipped: fonts/) - rounded, game-like; it runs small, hence the sizes
    char baloo[1024];
    std::string own = rt_resource("Baloo2-SemiBold.ttf", "fonts/Baloo2-SemiBold.ttf", baloo, sizeof baloo) ? baloo : "";
    const char *fonts[] = {getenv("LARECOMP_LAUNCHER_FONT") ? getenv("LARECOMP_LAUNCHER_FONT") : "",  // (test)
                           own.c_str(), "fonts/nintendo_udsgr_std_003.ttf", "/System/Library/Fonts/Avenir Next.ttc",
                           "/System/Library/Fonts/Helvetica.ttc", "C:/Windows/Fonts/segoeui.ttf"};
    static const ImWchar ranges[] = {0x20, 0xFF, 0x2039, 0x203A, 0x2192, 0x2192, 0x2713, 0x2713, 0x25B8, 0x25B8, 0};  // + ‹ › → ✓ ▸
    for (const char *p : fonts) {
        if (g_font_menu || !*p || !file_exists(p)) continue;
        bool b2 = p == own.c_str() || getenv("LARECOMP_LAUNCHER_FONT");
        g_font_small = io.Fonts->AddFontFromFileTTF(p, 30 * (b2 ? 1.75f : 1), NULL, ranges);
        g_font_menu = io.Fonts->AddFontFromFileTTF(p, 52 * (b2 ? 1.85f : 1), NULL, ranges);
        g_font_title = io.Fonts->AddFontFromFileTTF(p, 92 * (b2 ? 2.3f : 1), NULL, ranges);
    }
    if (!g_font_menu) g_font_small = g_font_menu = g_font_title = io.Fonts->AddFontDefault();
    ImGuiStyle &st = ImGui::GetStyle();
    st.Colors[ImGuiCol_Header] = ImVec4(0, 0, 0, 0);
    st.Colors[ImGuiCol_HeaderHovered] = ImVec4(1, 1, 1, 0.08f);
    st.Colors[ImGuiCol_HeaderActive] = ImVec4(1, 1, 1, 0.14f);
    st.Colors[ImGuiCol_NavCursor] = ImVec4(0.95f, 0.78f, 0.35f, 1);
    st.FrameRounding = st.WindowRounding = 10;
#ifdef __APPLE__
    ImGui_ImplSDL3_InitForMetal(win);
    ImGui_ImplMetal_Init(dev);
#else
    ImGui_ImplSDL3_InitForVulkan(win);
    vk_imgui_backend_init();
#endif
    ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, NULL, 0);  // (controllers: launcher_pad_event)
    launcher_pad_open_all();
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;

    std::string status = check_game_files(g_root);
    if (status.empty()) load_background(dev);
    std::vector<std::string> seeds = list_seeds();
    std::vector<std::string> choices = {"Vanilla"};
    choices.insert(choices.end(), seeds.begin(), seeds.end());
    int choice = 0;
    for (size_t i = 0; i < seeds.size(); i++)
        if (seeds[i] == g_settings.rando_seed) choice = (int)i + 1;
    Page page = status.empty() ? MAIN : SETUP;
    bool play = false;
    // LARECOMP_LAUNCHER_SHOT=<frame>[:<page>] (test): save build/launcher.png at that frame and exit; page: settings[:<tab>] / askdelete / setup
    long shot_at = getenv("LARECOMP_LAUNCHER_SHOT") ? atol(getenv("LARECOMP_LAUNCHER_SHOT")) : 0, frame = 0;
    if (shot_at) {
#ifdef __APPLE__
        layer.framebufferOnly = NO;
#endif
        const char *pg = strchr(getenv("LARECOMP_LAUNCHER_SHOT"), ':');
        if (pg && (!strncmp(pg + 1, "settings", 8) || !strcmp(pg + 1, "askdelete"))) page = SETTINGS, menu_tabs_focus(true);
        if (pg && (!strcmp(pg + 1, "setup") || !strcmp(pg + 1, "extracting"))) page = SETUP;
        if (pg && !strncmp(pg + 1, "fail", 4)) page = SETUP, set_failure(atoi(pg + 5));  // (the failure messages)
        if (pg && !strncmp(pg + 1, "settings", 8) && pg[9] == ':') g_tab_request_ext = atoi(pg + 10);  // settings:<tab>
        if (pg && !strcmp(pg + 1, "delete")) page = SETUP, g_confirm_delete = true;
        if (pg && !strcmp(pg + 1, "tip")) g_settings.menu_tip_seen = false, g_tip_test = true;
        if (pg && !strcmp(pg + 1, "aspect")) g_settings.aspect_asked = g_settings.menu_tip_seen = false, g_tip_test = true;
    }

    static int runs;  // (test: =once - only the first time, so a "Title screen" reboot lands on the start screen)
    const char *ap = getenv("LARECOMP_LAUNCHER_AUTOPLAY");
    if (ap && status.empty() && !(runs && !strcmp(ap, "once"))) play = true;  // (test: PLAY at once)
    runs++;
    if (const char *t = getenv("LARECOMP_LAUNCHER_EXTRACT")) {  // (test) <folder>: set the game files up, log, exit
        g_src = t;
        g_busy = true;
        extract_thread(NULL);
        fprintf(stderr, "[launcher] setup: %s -> %s\n", g_job.c_str(), check_game_files(g_root).empty() ? "READY" : "NOT READY");
        exit(0);
    }
    if (const char *t = getenv("LARECOMP_LAUNCHER_SETUP")) {  // (test) <folder>: Set up started on the start screen
        g_src = t;
        g_busy = true;
        page = SETUP;
        start_extract();
    }
    if (getenv("LARECOMP_LAUNCHER_DELETE")) {  // (test) what DELETE GAME FILES does, logged; then the start screen
        double gb = rt_game_files_size(g_root.c_str()) / 1e9;
        bool ok = rt_remove_game_files(g_root.c_str());
        status = check_game_files(g_root);
        fprintf(stderr, "[launcher] delete %.1f GB: %s -> %s\n", gb, ok ? "ok" : "refused", status.c_str());
        if (!status.empty()) { g_bg = 0; page = SETUP; }
    }
    while (!play) {
        if (g_play_requested) {
            status = check_game_files(g_root);
            if (status.empty()) {
                play = true;
            }
            g_play_requested = false;
        }
        menu_apply_window_requests(win);  // (the Video tab's Fullscreen)
        snprintf(g_settings.rando_seed, sizeof g_settings.rando_seed, "%s", choice ? choices[choice].c_str() : "");
        if (g_files_deleted) {  // (Settings > Prelaunch deleted them)
            g_files_deleted = false;
            status = check_game_files(g_root);
            if (!status.empty()) { g_bg = 0; bind_capture_cancel(); settings_save(); page = SETUP; }
        }
        if (g_play_seed_req) {  // the Randomizer tab's Play: that seed, then PLAY as if pressed (first-launch questions)
            g_play_seed_req = false;
            seeds = list_seeds();
            choices = {"Vanilla"};
            choices.insert(choices.end(), seeds.begin(), seeds.end());
            choice = 0;
            for (size_t i = 0; i < seeds.size(); i++)
                if (seeds[i] == g_play_seed) choice = (int)i + 1;
            bind_capture_cancel();
            settings_save();
            page = MAIN;
            g_play_pending = true;
        }
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (page == SETTINGS && bind_capture_event(&e)) continue;  // (waiting for a new binding: controls_ui.mm)
            if (!launcher_pad_event(e)) ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT) exit(0);
        }
        // extraction finished: re-check, load the background
        pthread_mutex_lock(&g_lock);
        bool busy = g_busy;
        pthread_mutex_unlock(&g_lock);
        static bool was_busy;
        if (was_busy && !busy) {
            status = check_game_files(g_root);
            if (status.empty()) { load_background(dev); page = MAIN; }
        }
        was_busy = busy;

#ifdef __APPLE__
        int w, h;
        SDL_GetWindowSizeInPixels(win, &w, &h);
        layer.drawableSize = CGSizeMake(w, h);
        @autoreleasepool {
            id<CAMetalDrawable> drawable = [layer nextDrawable];
            if (!drawable) { SDL_Delay(16); continue; }
            MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
            rp.colorAttachments[0].texture = drawable.texture;
            rp.colorAttachments[0].loadAction = MTLLoadActionClear;
            rp.colorAttachments[0].clearColor = MTLClearColorMake(0.04, 0.05, 0.06, 1);
            rp.colorAttachments[0].storeAction = MTLStoreActionStore;
            id<MTLCommandBuffer> cb = [queue commandBuffer];
            id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
            ImGui_ImplMetal_NewFrame(rp);
#else
        {
            static uint64_t next_frame;
            const SDL_DisplayMode *dm = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(win));
            uint64_t period = (uint64_t)(1e9 / (dm && dm->refresh_rate > 1 ? dm->refresh_rate : 60.0f)), now = plat_now_ns();
            bool on_time = next_frame > now && next_frame - now <= period;
            if (on_time) plat_sleep_until_ns(next_frame);
            next_frame = (on_time ? next_frame : now) + period;
            static const float clear_col[4] = {0.04f, 0.05f, 0.06f, 1};
            if (!vk_frame_begin(clear_col)) { SDL_Delay(16); continue; }
            ImGui_ImplVulkan_NewFrame();
#endif
            ImGui_ImplSDL3_NewFrame();
            if (SDL_HasGamepad()) io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
            ImGui::NewFrame();

            ImVec2 ds = io.DisplaySize;
            float s = ds.y / 720.0f;  // layout scale (fonts are built for ~2x)
            ImGui::GetIO().FontGlobalScale = s * 0.5f;
            ImDrawList *bg = ImGui::GetBackgroundDrawList();
            if (g_bg) {  // cover the window, keep the aspect
                float ia = (float)g_bg_w / g_bg_h, wa = ds.x / ds.y;
                ImVec2 uv0(0, 0), uv1(1, 1);
                if (wa > ia) { float v = ia / wa; uv0.y = (1 - v) / 2; uv1.y = 1 - uv0.y; }
                else { float u = wa / ia; uv0.x = (1 - u) / 2; uv1.x = 1 - uv0.x; }
                bg->AddImage(BG_TEXID, ImVec2(0, 0), ds, uv0, uv1);
            }
            draw_skyloft_atmosphere(bg, ds, s, g_bg != 0);
            {  // Asymmetric Skyloft night-slate shadow banner on the left behind the menu, and bottom-right corner
                const float stops[][2] = {{0.0f, 232}, {0.28f, 218}, {0.43f, 168}, {0.55f, 92}, {0.66f, 32}, {0.76f, 0}};
                for (int i = 0; i + 1 < (int)(sizeof stops / sizeof stops[0]); i++) {
                    ImU32 a = IM_COL32(7, 14, 24, (int)stops[i][1]), b = IM_COL32(7, 14, 24, (int)stops[i + 1][1]);
                    bg->AddRectFilledMultiColor(ImVec2(ds.x * stops[i][0], 0), ImVec2(ds.x * stops[i + 1][0], ds.y), a, b, b, a);
                }
                float y0 = ds.y - 195 * s, y1 = ds.y - 135 * s, y2 = ds.y - 40 * s, y3 = ds.y, x0r = ds.x * 0.48f;
                ImU32 c0 = IM_COL32(7, 14, 24, 0), c1 = IM_COL32(7, 14, 24, 235);
                bg->AddRectFilledMultiColor(ImVec2(x0r, y0), ImVec2(ds.x, y1), c0, c0, c1, c0);
                bg->AddRectFilledMultiColor(ImVec2(x0r, y1), ImVec2(ds.x, y2), c0, c1, c1, c0);
                bg->AddRectFilledMultiColor(ImVec2(x0r, y2), ImVec2(ds.x, y3), c0, c1, c0, c0);
            }

            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ds);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
            ImGui::Begin("##launcher", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
            ImGui::SetScrollX(0);
            ImGui::SetScrollY(0);
            float x0 = 88 * s;

            if (page == MAIN) {
                ImDrawList *dl = ImGui::GetWindowDrawList();
                // Sacred Triforce & Goddess Crest eyebrow above the main title
                ImVec2 wp = ImGui::GetWindowPos();
                float ey_y = wp.y + 84 * s;
                draw_goddess_crest(dl, ImVec2(wp.x + x0 + 24 * s, ey_y + 10 * s), 11.5f * s, kColGoldBright,
                                   IM_COL32(238, 198, 92, 185));
                ImGui::PushFont(g_font_small);
                float ey_fs = ImGui::GetFontSize() * 0.78f;
                const char *eyebrow = "THE LEGEND OF ZELDA  \xc2\xb7  SKYWARD SWORD HD";
                dl->AddText(g_font_small, ey_fs, ImVec2(wp.x + x0 + 62 * s, ey_y + 1.5f * s), IM_COL32(4, 10, 18, 160), eyebrow);
                dl->AddText(g_font_small, ey_fs, ImVec2(wp.x + x0 + 60 * s, ey_y), kColGold, eyebrow);
                ImGui::PopFont();

                ImGui::SetCursorPos(ImVec2(x0, 114 * s));
                ImGui::PushFont(g_font_title);
                const char *t = "Sky Recomp";
                ImVec2 p = ImGui::GetCursorScreenPos();
                ImVec2 tsz = ImGui::CalcTextSize(t);
                text_fx(dl, ImGui::GetFont(), ImGui::GetFontSize(), p, IM_COL32(255, 238, 142, 255),
                        IM_COL32(236, 138, 46, 255), t, 3.2f * s);
                ImGui::Dummy(tsz);
                ImGui::PopFont();

                // Ornamental Hylian horizontal rule with center diamond under "Sky Recomp"
                float rule_y = p.y + tsz.y + 4 * s, rule_w = tsz.x * 0.92f;
                float mid_x = p.x + rule_w * 0.42f;
                dl->AddRectFilledMultiColor(ImVec2(p.x, rule_y), ImVec2(mid_x - 12 * s, rule_y + 2.0f * s),
                                            IM_COL32(238, 198, 92, 195), IM_COL32(238, 198, 92, 225),
                                            IM_COL32(238, 198, 92, 225), IM_COL32(238, 198, 92, 195));
                draw_diamond(dl, ImVec2(mid_x, rule_y + 1.0f * s), 5.5f * s, 5.5f * s, kColGoldBright);
                dl->AddRectFilledMultiColor(ImVec2(mid_x + 12 * s, rule_y), ImVec2(p.x + rule_w, rule_y + 2.0f * s),
                                            IM_COL32(238, 198, 92, 225), IM_COL32(238, 198, 92, 0),
                                            IM_COL32(238, 198, 92, 0), IM_COL32(238, 198, 92, 225));
            }
            ImGui::SetCursorPos(ImVec2(x0, 285 * s));
            static bool typed_before;
            bool typing = typed_before || ui_text_busy();

            if (page == MAIN) {
                {  // the four items close together, anchored where QUIT always was
                    ImGui::PushFont(g_font_menu);
                    float h = ImGui::CalcTextSize("A").y, sp = ImGui::GetStyle().ItemSpacing.y;
                    ImGui::PopFont();
                    auto pitch = [&](float gap) { return h + 8 + gap * h + (gap >= 0 ? 2 : 1) * sp; };
                    g_menu_gap = -0.16f;
                    ImGui::SetCursorPosY(285 * s + 3 * pitch(0.35f) - 3 * pitch(g_menu_gap));
                }
                bool hov = false;
                ImGui::SetCursorPosX(x0);
                static bool focused_once;
                if (!focused_once) { ImGui::SetKeyboardFocusHere(); focused_once = true; }
                ImVec2 play_at = ImGui::GetCursorScreenPos();
                bool pressed = menu_item("PLAY", true, &hov);
                if (status.empty()) {  // PLAY twinkles now and then while the game is ready
                    ImGui::PushFont(g_font_menu);
                    float w = ImGui::CalcTextSize("PLAY").x, h = ImGui::CalcTextSize("PLAY").y;
                    ImGui::PopFont();
                    float t = (float)ImGui::GetTime(), ph = fmodf(t, 3.6f), ph2 = fmodf(t + 0.45f, 3.6f);
                    float k1 = ph < 0.9f ? sinf(ph / 0.9f * 3.14159265f) : 0, k2 = ph2 < 0.7f ? sinf(ph2 / 0.7f * 3.14159265f) : 0;
                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    sparkle(dl, ImVec2(play_at.x + w + h * 0.08f, play_at.y + h * 0.22f), h * 0.2f, k1, t * 0.6f);
                    sparkle(dl, ImVec2(play_at.x + w - h * 0.30f, play_at.y + h * 0.08f), h * 0.11f, k2 * 0.8f, -t * 0.8f);
                }
                if (g_play_pending && status.empty()) pressed = true, g_play_pending = false;
                if (pressed) {
                    if (!status.empty()) page = SETUP;
                    else if (!g_settings.aspect_asked) ImGui::OpenPopup("##aspect");
                    else if (!g_settings.menu_tip_seen) ImGui::OpenPopup("##menutip");
                    else play = true;
                }
                if (g_tip_test) ImGui::OpenPopup(g_settings.aspect_asked ? "##menutip" : "##aspect"), g_tip_test = false;
                if (aspect_prompt(s)) {
                    if (g_settings.menu_tip_seen) play = true;
                    else ImGui::OpenPopup("##menutip");
                }
                if (menu_tip(s)) play = true;
                ImGui::SetCursorPosX(x0);
                if (g_refocus == SETTINGS) ImGui::SetKeyboardFocusHere(), g_refocus = MAIN;
                if (menu_item("SETTINGS", true)) {
                    static bool opened;
                    menu_tabs_focus(!opened);
                    opened = true;
                    page = SETTINGS;
                }
                if (!status.empty() || busy) {
                    ImGui::SetCursorPosX(x0);
                    if (g_refocus == SETUP) ImGui::SetKeyboardFocusHere(), g_refocus = MAIN;
                    if (menu_item("SET UP GAME FILES", !busy)) page = SETUP;
                }
                ImGui::SetCursorPosX(x0);
                if (menu_item("QUIT", true)) exit(0);
                g_menu_gap = 0.35f;
            } else if (page == SETTINGS) {
                float sc = ds.y / 1080.0f, w = fminf(ds.x * 0.92f, 1520 * sc), h = ds.y - 2 * 22 * sc;
                ImDrawList *wdl = ImGui::GetWindowDrawList();
                wdl->AddRectFilled(ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + ds.x,
                                   ImGui::GetWindowPos().y + ds.y), IM_COL32(4, 10, 14, 145));
                ImVec2 wp = ImGui::GetWindowPos();
                ImVec2 pa(wp.x + (ds.x - w) * 0.5f, wp.y + (ds.y - h) * 0.5f), pb(pa.x + w, pa.y + h);
                draw_tablet_frame(wdl, pa, pb, sc, 14.0f);
                ImGui::SetCursorScreenPos(pa);
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 14 * sc);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(32 * sc, 26 * sc));
                ImGui::BeginChild("##settings", ImVec2(w, h), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NavFlattened);
                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor();
                ImGui::PushFont(g_font_small);
                ImGui::PushFont(g_font_menu);
                ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.38f, 1), "Settings");
                ImGui::PopFont();
                bool back;
                {  // close: a drawn X in the corner (B / Esc too)
                    float bx = 44 * sc;
                    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - bx);
                    ImVec2 p = ImGui::GetCursorScreenPos();
                    ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));
                    back = ImGui::InvisibleButton("##close", ImVec2(bx, bx), ImGuiButtonFlags_EnableNav);
                    ImGui::PopStyleColor();
                    bool hot = ImGui::IsItemHovered() || ImGui::IsItemFocused();
                    ImU32 col = hot ? kColGoldBright : IM_COL32(220, 216, 205, 255);
                    float m = bx * 0.3f, t = (hot ? 3.0f : 2.4f) * sc * 1.5f;
                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    if (hot) {
                        dl->AddRectFilled(p, ImVec2(p.x + bx, p.y + bx), IM_COL32(238, 198, 92, 32), 8 * sc);
                        dl->AddRect(p, ImVec2(p.x + bx, p.y + bx), kColGold, 8 * sc, 0, 1.5f * sc);
                    }
                    dl->AddLine(ImVec2(p.x + m, p.y + m), ImVec2(p.x + bx - m, p.y + bx - m), col, t);
                    dl->AddLine(ImVec2(p.x + bx - m, p.y + m), ImVec2(p.x + m, p.y + bx - m), col, t);
                }
                bool popup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
                menu_tabs_draw(sc, g_font_small, g_font_menu, false, prelaunch_tab);
                int cap_dev;
                bind_capture_active(&cap_dev, NULL, NULL);
                if (!popup && !typing && cap_dev < 0 && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                    (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
                    back = true;
                ImGui::PopFont();
                ImGui::EndChild();
                if (back) {
                    bind_capture_cancel();
                    settings_save();
                    page = MAIN, g_refocus = SETTINGS;
                    std::string cur = choices[choice];
                    seeds = list_seeds();
                    choices = {"Vanilla"};
                    choices.insert(choices.end(), seeds.begin(), seeds.end());
                    choice = 0;
                    for (size_t i = 0; i < choices.size(); i++)
                        if (choices[i] == cur) choice = (int)i;
                }
            } else {
                // SETUP: the game files, in a sacred tablet window like Settings
                float sc = ds.y / 1080.0f, w = fminf(ds.x * 0.92f, 1520 * sc), h = ds.y - 2 * 22 * sc;
                ImDrawList *wdl = ImGui::GetWindowDrawList();
                wdl->AddRectFilled(ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + ds.x,
                                   ImGui::GetWindowPos().y + ds.y), IM_COL32(4, 10, 14, 145));
                ImVec2 wp = ImGui::GetWindowPos();
                ImVec2 pa(wp.x + (ds.x - w) * 0.5f, wp.y + (ds.y - h) * 0.5f), pb(pa.x + w, pa.y + h);
                draw_tablet_frame(wdl, pa, pb, sc, 14.0f);
                ImGui::SetCursorScreenPos(pa);
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 14 * sc);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(32 * sc, 26 * sc));
                ImGui::BeginChild("##setup", ImVec2(w, h), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NavFlattened);
                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor();
                ImGui::PushFont(g_font_small);
                ImGui::PushFont(g_font_menu);
                ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.38f, 1), "Game Files Setup");
                ImGui::PopFont();
                bool back = false;
                if (!busy) {
                    float bx = 44 * sc;
                    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - bx);
                    ImVec2 p = ImGui::GetCursorScreenPos();
                    ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));
                    back = ImGui::InvisibleButton("##close", ImVec2(bx, bx), ImGuiButtonFlags_EnableNav);
                    ImGui::PopStyleColor();
                    bool hot = ImGui::IsItemHovered() || ImGui::IsItemFocused();
                    ImU32 col = hot ? kColGoldBright : IM_COL32(220, 216, 205, 255);
                    float m = bx * 0.3f, t = (hot ? 3.0f : 2.4f) * sc * 1.5f;
                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    if (hot) {
                        dl->AddRectFilled(p, ImVec2(p.x + bx, p.y + bx), IM_COL32(238, 198, 92, 32), 8 * sc);
                        dl->AddRect(p, ImVec2(p.x + bx, p.y + bx), kColGold, 8 * sc, 0, 1.5f * sc);
                    }
                    dl->AddLine(ImVec2(p.x + m, p.y + m), ImVec2(p.x + bx - m, p.y + bx - m), col, t);
                    dl->AddLine(ImVec2(p.x + bx - m, p.y + m), ImVec2(p.x + m, p.y + bx - m), col, t);
                }
                ImGui::Spacing();

                pthread_mutex_lock(&g_lock);
                std::string src = g_src, job = g_job, job_hint = g_job_hint;
                bool failed = g_job_failed;
                pthread_mutex_unlock(&g_lock);
                bool ready = status.empty() && !busy;
                static double files_gb = -1;
                std::string src_v = src.empty() ? "Choose folder..." : "Selected";
                auto check_left_of_value = [&](const std::string &v) {
                    if (v != "Selected") return;
                    ImVec2 rb = ImGui::GetItemRectMax(), ra = ImGui::GetItemRectMin();
                    float fs = ImGui::GetFontSize(), vw = ImGui::CalcTextSize(v.c_str()).x;
                    float x = rb.x - 18 * sc - vw - fs * 0.95f, cy = (ra.y + rb.y) * 0.5f, u = fs * 0.62f;
                    ImU32 g = kColTeal;
                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    dl->AddLine(ImVec2(x, cy), ImVec2(x + u * 0.35f, cy + u * 0.35f), g, fs * 0.11f);
                    dl->AddLine(ImVec2(x + u * 0.35f, cy + u * 0.35f), ImVec2(x + u, cy - u * 0.4f), g, fs * 0.11f);
                };

                std::string src_d = "Select the folder containing your unpacked game files (base version 1.0.0). "
                                    "Sky Recomp does not include any game assets or copyrighted data.";
                if (!src.empty()) src_d += "\n\nSelected folder:\n" + src;

                const std::string &job_line = job;
                std::string ex_d = "Copies the required files, builds the local data archive, and compiles the game code into "
                                   "a native library for your PC. This runs once during initial setup and leaves your source folder untouched.";
                if (!job.empty()) ex_d += "\n\n" + job + (job_hint.empty() ? "" : "\n" + job_hint);

                static int seen_frame = -10, focus_first;
                if (seen_frame != ImGui::GetFrameCount() - 1) focus_first = 4;
                seen_frame = ImGui::GetFrameCount();
                auto first_row = [&] {
                    if (focus_first > 0 && focus_first--) ImGui::SetFocusID(ImGui::GetItemID(), ImGui::GetCurrentWindow());
                };
                opt_begin("##files", sc);
                if (!ready) {
                    opt_section("1. Source Files");
                    if (busy) opt_disable_next();
                    if (opt_action("Game files folder", src_v.c_str(), src_d.c_str(), true))
                        SDL_ShowOpenFolderDialog(picked, NULL, win, NULL, false);
                    check_left_of_value(src_v);
                    first_row();

                    opt_section("2. Install & Compile");
                    if (busy || (src.empty() && !check_copied_files(g_root).empty())) opt_disable_next();
                    if (opt_action("Set up game files", busy ? "Working..." : "Start", ex_d.c_str(), true)) {
                        pthread_mutex_lock(&g_lock);
                        g_busy = true;
                        pthread_mutex_unlock(&g_lock);
                        start_extract();
                    }
                    files_gb = -1;
                } else {
                    opt_section("Ready to Play");
                    if (opt_action("Start Game", "Launch", "Launch the game now.", true)) {
                        play = true;
                    }
                    first_row();

                    if (files_gb < 0) files_gb = rt_game_files_size(g_root.c_str()) / 1e9;
                    char gb[32];
                    snprintf(gb, sizeof gb, "%.1f GB", files_gb);
                    opt_section("Manage Game Files");
                    if (!g_confirm_delete) {
                        if (opt_action("Delete game files", gb, "Removes Sky Recomp's installed game data and compiled library to free disk space. "
                                       "Your save data, settings, and original source folder are kept.", true))
                            g_confirm_delete = true;
                    } else {
                        if (opt_action("Delete game files now", gb, "Permanently deletes the installed game files from Sky Recomp's folder.", true)) {
                            bool ok = rt_remove_game_files(g_root.c_str());
                            status = check_game_files(g_root);
                            if (ok) set_job("Game files deleted.");
                            else set_job("Could not delete the game files", true, "They are outside Sky Recomp's data folder, so nothing was removed.");
                            if (!status.empty()) g_bg = 0;
                            g_confirm_delete = false, files_gb = -1;
                        }
                        if (opt_action("Keep game files", "", "Cancels deletion and keeps your installed game files.", true)) g_confirm_delete = false;
                    }
                }
                if (!job_line.empty()) {
                    ImGui::Dummy(ImVec2(0, 16 * sc));
                    ImVec2 sp = ImGui::GetCursorScreenPos();
                    float sw = ImGui::GetContentRegionAvail().x, pad_in = 16 * sc;
                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    ImU32 bcol = failed ? IM_COL32(235, 98, 82, 165) : busy ? IM_COL32(238, 198, 92, 165) : IM_COL32(102, 214, 206, 165);
                    ImU32 fcol = failed ? IM_COL32(48, 18, 18, 210) : busy ? IM_COL32(36, 32, 16, 210) : IM_COL32(14, 38, 36, 210);
                    float wrap_w = sw - 2 * pad_in;
                    ImVec2 txt_sz = ImGui::CalcTextSize(job_line.c_str(), NULL, false, wrap_w);
                    ImVec2 hnt_sz = (failed && !job_hint.empty()) ? ImGui::CalcTextSize(job_hint.c_str(), NULL, false, wrap_w) : ImVec2(0, 0);
                    float box_h = txt_sz.y + (hnt_sz.y > 0 ? hnt_sz.y + 8 * sc : 0) + 24 * sc;
                    dl->AddRectFilled(sp, ImVec2(sp.x + sw, sp.y + box_h), fcol, 8 * sc);
                    dl->AddRect(sp, ImVec2(sp.x + sw, sp.y + box_h), bcol, 8 * sc, 0, 1.4f * sc);
                    ImGui::SetCursorScreenPos(ImVec2(sp.x, sp.y + 12 * sc));
                    ImGui::Indent(pad_in);
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap_w);
                    ImGui::TextColored(failed ? ImVec4(1.0f, 0.56f, 0.48f, 1) : busy ? ImVec4(0.95f, 0.80f, 0.38f, 1)
                                              : ImVec4(0.45f, 0.95f, 0.75f, 1), "%s", job_line.c_str());
                    if (failed && !job_hint.empty())
                        ImGui::TextColored(ImVec4(0.88f, 0.87f, 0.82f, 1), "%s", job_hint.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::Unindent(pad_in);
                    ImGui::Dummy(ImVec2(0, 8 * sc));
                }
                opt_end();
                if (g_confirm_delete && (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
                                         ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
                    g_confirm_delete = false;
                else if (!busy && !typing && (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
                                              ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
                    back = true;
                ImGui::PopFont();
                ImGui::EndChild();
                if (back && !busy) {
                    settings_save();
                    g_confirm_delete = false;
                    page = MAIN, g_refocus = SETUP;
                }
                static bool test_busy = getenv("LARECOMP_LAUNCHER_SHOT") && strstr(getenv("LARECOMP_LAUNCHER_SHOT"), ":extracting");
                if (busy || test_busy) {  // extracting: everything dimmed, a progress bar in front (input waits)
                    ImDrawList *fg = ImGui::GetForegroundDrawList();
                    ImVec2 o = ImGui::GetMainViewport()->Pos;
                    fg->AddRectFilled(o, ImVec2(o.x + ds.x, o.y + ds.y), IM_COL32(4, 10, 14, 185));
                    float f = test_busy ? 0.42f : extract_progress();
                    bool recompiling = g_build_frac >= 0;
                    const char *title = recompiling ? "Recompiling game code" : "Setting up game files";
                    static const char *const notes[] = {"This runs once during initial setup and takes a couple of minutes."};
                    int nnotes = recompiling ? 1 : 0;
                    float tfs = g_font_menu->FontSize * 0.75f, sfs = g_font_small->FontSize * 0.85f;
                    ImVec2 ts = g_font_menu->CalcTextSizeA(tfs, FLT_MAX, 0, title);
                    float line_h = g_font_small->CalcTextSizeA(sfs, FLT_MAX, 0, "A").y;
                    float pw = fminf(ds.x * 0.62f, 920 * sc), bar_y = 42 * sc + ts.y + 30 * sc;
                    for (int i = 0; i < nnotes; i++)
                        pw = fmaxf(pw, g_font_small->CalcTextSizeA(sfs, FLT_MAX, 0, notes[i]).x + 96 * sc);
                    pw = fminf(fmaxf(pw, ts.x + 96 * sc), ds.x - 32 * sc);
                    float ph = bar_y + 26 * sc + 26 * sc + line_h + (nnotes ? 14 * sc + nnotes * line_h : 0) + 44 * sc;
                    ImVec2 a(o.x + (ds.x - pw) * 0.5f, o.y + (ds.y - ph) * 0.5f), b(a.x + pw, a.y + ph);
                    draw_tablet_frame(fg, a, b, sc, 14.0f, IM_COL32(12, 22, 25, 252));
                    fg->AddText(g_font_menu, tfs, ImVec2((a.x + b.x - ts.x) * 0.5f, a.y + 36 * sc), kColGoldBright, title);
                    ImVec2 ba(a.x + 48 * sc, a.y + bar_y), bb(b.x - 48 * sc, a.y + bar_y + 24 * sc);
                    fg->AddRectFilled(ba, bb, IM_COL32(255, 255, 255, 22), 12 * sc);
                    fg->AddRect(ba, bb, IM_COL32(238, 198, 92, 95), 12 * sc, 0, 1.2f * sc);
                    if (f > 0) {
                        float fill_x = ba.x + (bb.x - ba.x) * f;
                        fg->AddRectFilled(ba, ImVec2(fill_x, bb.y), kColGold, 12 * sc);
                        draw_diamond(fg, ImVec2(fill_x, (ba.y + bb.y) * 0.5f), 5.5f * sc, 5.5f * sc, kColGoldBright);
                    }
                    char line[200];
                    snprintf(line, sizeof line, "%s   %d%%", test_busy ? "Setting up the game files..." : job.c_str(),
                             (int)(f * 100));
                    ImVec2 ls = g_font_small->CalcTextSizeA(sfs, FLT_MAX, 0, line);
                    fg->AddText(g_font_small, sfs, ImVec2((a.x + b.x - ls.x) * 0.5f, bb.y + 26 * sc), kColIvory, line);
                    for (int i = 0; i < nnotes; i++) {
                        ImVec2 ns = g_font_small->CalcTextSizeA(sfs, FLT_MAX, 0, notes[i]);
                        fg->AddText(g_font_small, sfs, ImVec2((a.x + b.x - ns.x) * 0.5f, bb.y + 26 * sc + (i + 1) * line_h + 14 * sc),
                                    kColSageMuted, notes[i]);
                    }
                }
            }

            // status card (lower left) and version + input legend (lower right) on the MAIN screen
            if (page == MAIN) {
                ImGui::PushFont(g_font_small);
                ImDrawList *dl = ImGui::GetWindowDrawList();
                ImVec2 wp = ImGui::GetWindowPos();
                float card_x = wp.x + x0 - 14 * s, card_y = wp.y + ds.y - 126 * s;
                float card_w = 430 * s, card_h = 84 * s;
                ImVec2 ca(card_x, card_y), cb(card_x + card_w, card_y + card_h);
                dl->AddRectFilled(ca, cb, IM_COL32(10, 20, 24, 195), 10 * s);
                dl->AddRect(ca, cb, status.empty() ? IM_COL32(102, 214, 206, 95) : IM_COL32(238, 198, 92, 110), 10 * s, 0, 1.3f * s);
                draw_diamond(dl, ImVec2(ca.x + 8 * s, ca.y + 8 * s), 3.0f * s, 3.0f * s, status.empty() ? kColTeal : kColGold);
                draw_diamond(dl, ImVec2(cb.x - 8 * s, cb.y - 8 * s), 3.0f * s, 3.0f * s, status.empty() ? kColTeal : kColGold);

                ImGui::SetCursorPos(ImVec2(x0 + 4 * s, ds.y - 115 * s));
                if (status.empty()) {
                    {  // drawn check mark
                        ImVec2 p = ImGui::GetCursorScreenPos();
                        float u = ImGui::GetTextLineHeight();
                        ImU32 g = kColTeal;
                        dl->AddLine(ImVec2(p.x + u * 0.1f, p.y + u * 0.55f), ImVec2(p.x + u * 0.35f, p.y + u * 0.8f), g, u * 0.13f);
                        dl->AddLine(ImVec2(p.x + u * 0.35f, p.y + u * 0.8f), ImVec2(p.x + u * 0.85f, p.y + u * 0.2f), g, u * 0.13f);
                        ImGui::SetCursorPosX(x0 + 4 * s + u * 1.25f);
                    }
                    ImGui::TextColored(ImVec4(0.45f, 0.94f, 0.78f, 1), "Game files ready");
                    ImGui::SetCursorPosX(x0 + 4 * s);
                    std::string sub = "Base v1.0.0";
#ifndef __APPLE__
                    if (g_settings.hd_textures) sub += "  \xc2\xb7  4K Textures";
#endif
#ifdef _WIN32
                    if (g_settings.reshade) sub += "  \xc2\xb7  ReShade";
#endif
                    ImGui::TextColored(ImVec4(0.82f, 0.86f, 0.84f, 1), "%s", sub.c_str());
                } else {
                    ImVec2 p = ImGui::GetCursorScreenPos();
                    float u = ImGui::GetTextLineHeight();
                    draw_diamond(dl, ImVec2(p.x + u * 0.4f, p.y + u * 0.5f), u * 0.32f, u * 0.32f, kColGoldBright);
                    ImGui::SetCursorPosX(x0 + 4 * s + u * 1.05f);
                    ImGui::TextColored(ImVec4(0.98f, 0.78f, 0.42f, 1), "%s", busy ? "Setting up game files..." : status.c_str());
                    ImGui::SetCursorPosX(x0 + 4 * s);
                    ImGui::TextColored(ImVec4(0.75f, 0.78f, 0.74f, 1), "Open Set Up Game Files to get started.");
                }
                if (const char *f = getenv("LARECOMP_LAUNCHER_FONT")) {  // (test) which typeface this window shows
                    const char *b = strrchr(f, '/');
                    dl->AddText(ImVec2(wp.x + 20 * s, wp.y + 16 * s), IM_COL32(255, 255, 255, 255), b ? b + 1 : f);
                }
                {  // version, update link, and controller/keyboard prompt (lower right)
                    ImFont *font = ImGui::GetFont();
                    float fs = ImGui::GetFontSize(), right = wp.x + ds.x - 56 * s;
                    auto text = [&](ImVec2 p, float size, ImU32 col, const char *t, bool bold) {
                        for (float d : {2.5f, 1.5f, 0.8f}) dl->AddText(font, size, ImVec2(p.x + d * s, p.y + d * s), IM_COL32(4, 8, 22, 165), t);
                        dl->AddText(font, size, p, col, t);
                        if (bold) dl->AddText(font, size, ImVec2(p.x + 0.7f * s, p.y), col, t);
                    };
                    std::string ver = tag_label(LARECOMP_VERSION, true);
                    float vs = fs * 1.32f, y = wp.y + ds.y - 128 * s;
                    ImVec2 vsz = font->CalcTextSizeA(vs, FLT_MAX, 0, ver.c_str());
                    text(ImVec2(right - vsz.x, y), vs, kColIvory, ver.c_str(), false);

                    const char *nav_hint = "Up / Down: Navigate    Enter / A: Select";
                    float hfs = fs * 0.84f;
                    ImVec2 hsz = font->CalcTextSizeA(hfs, FLT_MAX, 0, nav_hint);
                    text(ImVec2(right - hsz.x, y + vsz.y + 4 * s), hfs, IM_COL32(196, 204, 198, 225), nav_hint, false);

                    pthread_mutex_lock(&g_update_lock);
                    std::string upd = g_update_tag;
                    pthread_mutex_unlock(&g_update_lock);
                    if (!upd.empty()) {
                        const char *arrow = font->FindGlyphNoFallback(0x2192) ? " \xe2\x86\x92" : " \xe2\x96\xb8";
                        std::string pre = "Update available:  ", link = "DOWNLOAD " + tag_label(upd, false) + arrow;
                        float wp_w = font->CalcTextSizeA(fs, FLT_MAX, 0, pre.c_str()).x;
                        ImVec2 lsz = font->CalcTextSizeA(fs, FLT_MAX, 0, link.c_str());
                        ImVec2 lp(right - lsz.x, y - fs - 4 * s);
                        text(ImVec2(lp.x - wp_w, lp.y), fs, IM_COL32(200, 224, 240, 255), pre.c_str(), false);
                        ImGui::SetCursorScreenPos(lp);
                        bool click = ImGui::InvisibleButton("update", lsz);
                        bool hover = ImGui::IsItemHovered();
                        ImU32 col = hover ? IM_COL32(255, 238, 150, 255) : IM_COL32(255, 214, 64, 255);
                        ImU32 col_b = hover ? IM_COL32(255, 196, 110, 255) : IM_COL32(255, 160, 50, 255);
                        text_fx(dl, font, fs, lp, col, col_b, link.c_str(), 1.5f * s);
                        text_fx(dl, font, fs, ImVec2(lp.x + 0.7f * s, lp.y), col, col_b, link.c_str(), 0);
                        if (hover) {
                            dl->AddLine(ImVec2(lp.x, lp.y + lsz.y), ImVec2(lp.x + lsz.x, lp.y + lsz.y), col, 1.5f * s);
                            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        }
                        if (click) SDL_OpenURL((RELEASE_PAGE + upd).c_str());
                    }
                }
                ImGui::PopFont();
            }
            ImGui::End();
            ImGui::PopStyleColor();
            osk_draw(g_font_menu, s * 0.7f);
            typed_before = ui_text_busy();

            ImGui::Render();
#ifdef __APPLE__
            ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), cb, enc);
            [enc endEncoding];
            id<MTLTexture> shot = nil;
            if (shot_at && ++frame == shot_at) {
                MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:drawable.texture.pixelFormat
                                             width:drawable.texture.width height:drawable.texture.height mipmapped:NO];
                td.storageMode = MTLStorageModeShared;
                shot = [dev newTextureWithDescriptor:td];
                id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
                [bl copyFromTexture:drawable.texture toTexture:shot];
                [bl endEncoding];
            }
            [cb presentDrawable:drawable];
            [cb commit];
            if (shot) {
                [cb waitUntilCompleted];
                save_png(shot, getenv("LARECOMP_SHOT_PATH") ? getenv("LARECOMP_SHOT_PATH") : "build/launcher.png");
                exit(0);
            }
#else
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), (VkCommandBuffer)vk_frame_cmd());
            bool shot = shot_at && ++frame == shot_at;
            vk_frame_end(shot ? getenv("LARECOMP_SHOT_PATH") ? getenv("LARECOMP_SHOT_PATH") : "build/launcher.png" : NULL);
            if (shot) exit(0);
#endif
        }
    }
    // play: remember the choice, hand the window to the game
    snprintf(g_settings.rando_seed, sizeof g_settings.rando_seed, "%s", choice ? choices[choice].c_str() : "");
    settings_save();
#ifdef __APPLE__
    ImGui_ImplMetal_Shutdown();
#else
    vk_imgui_backend_shutdown();
#endif
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(ctx);
    ImGui::SetCurrentContext(prev_ctx);
    g_bg = 0;
    g_start_screen = 0;
}
