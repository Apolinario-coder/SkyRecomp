// User settings (larecomp.ini next to the executable's working directory), edited from the in-game menu.
#pragma once
#include <stdbool.h>

enum { ASPECT_UNLOCKED = 0, ASPECT_ORIGINAL = 1 };
enum { RES_MATCH = 0, /* 1..8 = 1x..8x */ RES_DYNAMIC = 9, RES_Q75 = 10, RES_Q25 = 11, RES_Q50 = 12 };
// (11 was 67% of the display: larecomp.ini "67%" now loads as RES_Q75)
enum { UPSCALE_BILINEAR = 0, UPSCALE_METALFX = 1 /* macOS */, UPSCALE_FSR1 = 2 /* Windows: AMD FSR 1 */, UPSCALE_NIS = 3 /* NVIDIA Image Scaling */ };
#ifdef __APPLE__
#define UPSCALE_SHARP UPSCALE_METALFX  // each OS's non-bilinear upscaler (the default)
#else
#define UPSCALE_SHARP UPSCALE_FSR1
#endif
enum { AA_OFF = 0, AA_SMAA = 1, AA_FXAA = 2 };
enum { COLORBLIND_OFF, COLORBLIND_PROTAN, COLORBLIND_DEUTAN, COLORBLIND_TRITAN };
enum { PERF_OFF = 0, PERF_FPS = 1, PERF_FULL = 2 };  // the performance overlay (runtime/perfmon.c, F10)
// What a draw does when its shaders or pipeline are still compiling (renderer.m, vulkan/exec.c): hybrid skips only
// draws that are redrawn every frame and waits for the rest; wait never skips (stutters); async always skips
enum { SHADERS_HYBRID = 0, SHADERS_WAIT = 1, SHADERS_ASYNC = 2 };
enum { PROMPTS_AUTO = 0, PROMPTS_NINTENDO = 1, PROMPTS_XBOX = 2, PROMPTS_PLAYSTATION = 3, PROMPTS_KEYBOARD = 4 };
enum { PROMPT_LOOK_LIGHT = 0, PROMPT_LOOK_DARK = 1 };
enum { CHAOS_LETTERS_OFF = 0, CHAOS_LETTERS_EXPLORED = 1, CHAOS_LETTERS_MAP = 2 /* explored, all with the Map */ };
enum { GAME_BUILD_FAST = 0, GAME_BUILD_OPTIMIZED = 1 };  // the start screen's game code build: runtime/gamebuild.h
// System language served to the game (runtime/services/am.c GetDesiredLanguage, services/hid_set.c GetLanguageCode):
// the game picks its text language from it, as it would from the Switch's own system setting. LANG_AUTO follows the
// host locale ($LC_ALL / $LC_MESSAGES / $LANG); the rest are the codes of the languages the game has text for.
enum { LANG_AUTO = 0, N_LANGUAGES = 15 };
extern const char *const g_language_codes[N_LANGUAGES];  // what the game is told: "ja", "en-US", "es", ...
extern const char *const g_language_names[N_LANGUAGES];  // for the menu ("Auto", "Japanese", ..., "Spanish")

typedef struct {
    int aspect;          // ASPECT_*: unlocked = fill the window (wider/taller views), original = 16:9 with bars
    bool fullscreen;
    int res_mode;        // RES_MATCH, 1..4 (N x the Switch's 1080p), RES_DYNAMIC, RES_Q75/50/25 (share of the display)
    int upscaler;        // UPSCALE_*: how a lower internal resolution reaches the window (MetalFX: macOS, FSR 1: Windows)
    int antialias;       // AA_*: anti-aliasing of the game image before it is scaled to the window
    int aniso;           // anisotropic filtering at least this (2..16; 0 = the game's own): nvn_sampler_aniso
    float volume;        // master volume, 1 = 100%
    bool reverb;         // room reverb effect
    bool movie_audio;
    int perf_overlay;    // PERF_*: off, the frame rate, or the full overlay (F10 cycles them)
    int shader_compile;  // SHADERS_*: see shader_compile_mode
    bool hd_textures;    // external texture replacements, applied live on Vulkan
    bool reshade;        // load the bundled Windows ReShade layer on the next launch
    int frame_rate;       // 60 or 120; the Skyward Sword simulation step follows this target
    bool dpad_move;      // the d-pad (and arrow keys) move Link like the stick
    bool free_cam;       // right stick looks around without holding L
    float free_cam_sens; // camera-only stick speed, 0.25x to 2x
    bool mouse_controls; // mouse camera and combat while free camera look is enabled
    float mouse_sens;    // relative mouse speed, 0.25x to 4x
    float gyro_sens;     // controller gyro aim speed, 0.1x to 2x: runtime/hle/input.c motion_sample
    bool move_360;       // analog movement in any direction (the game snaps the stick to 8): runtime/hle/qol.c
    bool blur_removal;   // no tilt-shift blur at the top / bottom of the screen
    bool hide_hero_icon; // no Hero Mode icon on the HUD
    bool mirror_mode;    // the world mirrored left to right (Gameplay tab): renderers + runtime/hle/input.c
    bool amiibo;         // Fi's "Use amiibo" finds the player's loaded Zelda & Loftwing dump (runtime/services/nfp.c)
    int language;        // LANG_AUTO or an index into g_language_codes: the game's text language
    int prompts;         // PROMPTS_*: button glyphs (auto = follow the controller SDL reports): runtime/nvn/texpatch.c
    int prompt_look;     // PROMPT_LOOK_*: those glyphs light like the game's own, or dark
    bool inv_keys;       // randomizer: show every dungeon's keys on the pause menu's Items page
    bool tracker;        // randomizer: check tracker on the pause menu's Map page
    int chaos_letters;   // CHAOS_LETTERS_*: room-shuffle doorway letters on the dungeon map
    bool aspect_asked;   // the start screen's one-time "authentic 16:9 or unlocked?" question was answered
    bool menu_tip_seen;  // the start screen's one-time "how to open the menu" note was shown (first PLAY ever)
    int game_build;      // GAME_BUILD_*: a faster build of the game code, or faster game code (start screen, Set up)
    // cheats (runtime/ui/cheats.mm), applied every frame while a save is loaded
    bool cheat_hearts;   // health kept full
    bool cheat_stamina;  // the stamina gauge never drains (tools/patches.py STAMINA_STORE_SITE)
    bool cheat_rupees;   // rupees kept at 9900
    bool cheat_beetle;   // the Beetle's flight timer never runs out (tools/patches.py BEETLE_TIMER_SITE)
    bool cheat_dash;     // the Loftwing's dashes are never used up (tools/patches.py DASH_USE_SITE)
    bool cheat_skyward;  // the sword stays charged with a Skyward Strike (runtime/ui/cheats.mm)
    bool cheat_hookshot; // hookshot latches onto any surface (Gameplay tab; runtime/hle/hookshot.c; seeds made with it: always)
    bool no_buff_drops;  // no Guardian Acorns / Pieces of Power (Gameplay tab; runtime/hle/qol.c; seeds made with it: always)
    // difficulty (runtime/ui/difficulty.mm)
    float diff_damage;   // damage taken multiplier (1 = normal)
    bool diff_ohko;      // any hit kills
    bool diff_hero;      // Hero Mode flag forced on in the loaded save (2x damage, no recovery hearts)
    int colorblind;      // COLORBLIND_*: colour filter in the present shader (runtime/nvn/output_scale.c)
    int dash_max;        // Loftwing dash charges, 3 (the game's) to 7 (tools/patches.py DASH_START_SITE, DASH_CAP_SITE)
    float flight_speed;  // Loftwing forward speed multiplier (tools/patches.py FLIGHT_SPEED_SITE)
    float walk_speed;    // Link's walking / running speed multiplier (tools/patches.py WALK_SPEED_SITE)
    bool drop_hearts, drop_rupees, drop_ammo, drop_treasures;  // drop kinds allowed (tools/patches.py DROP_*, TREASURE_*)
    bool drop_always;    // drop tables and treasure rolls never come up empty (Cheats tab)
    // cosmetics (runtime/ui/cosmetics.mm; applied live by runtime/nvn/texpatch.c), never part of a seed
    int tunic;           // index into g_tunic_colours (0 = Default: the game's / the seed's own)
    int hearts;          // index into g_heart_colours (0 = Default red)
    char rando_seed[128];  // active randomizer seed (rando/<name>/), empty = vanilla
    char rom_path[1024];   // path to user ROM file (.nsp, .xci)
    char keys_path[1024];  // path to user keys file (prod.keys, title.keys)
} Settings;

enum { N_TUNIC_COLOURS = 13, N_HEART_COLOURS = 9 };
extern const char *const g_tunic_colours[N_TUNIC_COLOURS];
extern const char *const g_heart_colours[N_HEART_COLOURS];

extern Settings g_settings;

void settings_load(void);
void settings_save(void);
// The language code to serve the game ("es", "en-US", ...): the setting, or the host locale for LANG_AUTO
const char *settings_language(void);
const char *settings_language_code(void);  // SDK code: Skyward Sword uses zh-Hans / zh-Hant.
// That code's name for the menu ("Spanish")
const char *settings_language_name(void);
void settings_game_started(void);
// The SHADERS_* mode in effect: the setting, or LARECOMP_SYNC_SHADERS (1 = wait, 0 = async; tests) when set
int shader_compile_mode(void);
bool settings_language_pending(void);
