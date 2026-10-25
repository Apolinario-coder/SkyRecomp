#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <SDL3/SDL_locale.h>
#include <SDL3/SDL_stdinc.h>

#include "hle.h"
#include "input_bind.h"
#include "mods.h"

#define SETTINGS_FILE settings_file()

// SKYRECOMP_SETTINGS=<path>: another settings file (test runs never touch the player's skyrecomp.ini)
static const char *settings_file(void) {
    const char *e = getenv("SKYRECOMP_SETTINGS");
    if (!e || !*e) e = getenv("LARECOMP_SETTINGS");
    return e && *e ? e : "skyrecomp.ini";
}

Settings g_settings = {
    .aspect = ASPECT_ORIGINAL,
    .fullscreen = true,
    .aspect_asked = true,
    .menu_tip_seen = true,
    .volume = 1.0f,
    .free_cam_sens = 1.0f,
    .mouse_controls = true,
    .mouse_sens = 1.0f,
    .reverb = true,
    .movie_audio = true,
    .show_fps = false,
    .hd_textures = true,
    .reshade = true,
    .frame_rate = 60,
    .inv_keys = true,
    .tracker = true,
    .chaos_letters = CHAOS_LETTERS_MAP,
    .diff_damage = 1.0f,
    .upscaler = UPSCALE_SHARP,  // (MetalFX on macOS, AMD FSR 1 on Windows)
};

// The game's own text languages: its RomFS carries these per region (regionUS/USen|USes|USfr, regionEU/EUen|EUes|...,
// regionJP/JPja, regionKR/KRkr, regionTW/TWch, regionCN/CNch), and the game picks one from the system language it
// reads back (runtime/services/am.c), exactly as it does on the console. Codes as the Switch reports them.
const char *const g_language_codes[N_LANGUAGES] = {"auto", "ja", "en-US", "es", "fr", "de", "it", "nl", "ru", "ko",
                                                   "zh-CN", "zh-TW"};
const char *const g_language_names[N_LANGUAGES] = {"Auto", "Japanese", "English", "Spanish", "French", "German",
                                                   "Italian", "Dutch", "Russian", "Korean", "Chinese (Simplified)",
                                                   "Chinese (Traditional)"};

#ifdef _WIN32
unsigned short __stdcall GetUserDefaultUILanguage(void);
#endif

static const char *language_for_locale(const char *locale) {
    if (!locale || !*locale) return NULL;
    char language[8];
    size_t n = strcspn(locale, "_- .@");
    if (!n || n >= sizeof language) return NULL;
    for (size_t i = 0; i < n; i++) language[i] = (char)tolower((unsigned char)locale[i]);
    language[n] = 0;
    if (!strcmp(language, "zh")) {
        bool traditional = false;
        const char *p = locale + n;
        while (*p == '_' || *p == '-') {
            p++;
            size_t length = strcspn(p, "_- .@");
            char part[8] = {0};
            if (length < sizeof part) {
                for (size_t i = 0; i < length; i++) part[i] = (char)tolower((unsigned char)p[i]);
                if (!strcmp(part, "hans")) return "zh-Hans";
                if (!strcmp(part, "hant")) return "zh-Hant";
                if (!strcmp(part, "tw") || !strcmp(part, "hk") || !strcmp(part, "mo")) traditional = true;
            }
            p += length;
        }
        return traditional ? "zh-Hant" : "zh-Hans";
    }
    static const struct { const char *loc, *code; } map[] = {{"ja", "ja"}, {"ko", "ko"}, {"ru", "ru"}, {"nl", "nl"},
        {"de", "de"}, {"it", "it"}, {"fr", "fr"}, {"es", "es"}, {"en", "en-US"}};
    for (size_t i = 0; i < sizeof map / sizeof *map; i++)
        if (!strcmp(language, map[i].loc)) return map[i].code;
    return NULL;
}

static const char *host_language(void) {
#ifdef _WIN32
    unsigned short lang = GetUserDefaultUILanguage();
    switch (lang & 0x3ff) {
    case 0x09: return "en-US";
    case 0x0a: return "es";
    case 0x0c: return "fr";
    case 0x07: return "de";
    case 0x10: return "it";
    case 0x13: return "nl";
    case 0x19: return "ru";
    case 0x12: return "ko";
    case 0x11: return "ja";
    case 0x04: {
        unsigned short sub = lang >> 10;
        return (sub == 0x01 || sub == 0x03 || sub == 0x04) ? "zh-Hant" : "zh-Hans";
    }
    default: break;
    }
#elif defined(__linux__)
    const char *e = getenv("LC_ALL");
    if (!e || !*e) e = getenv("LC_MESSAGES");
    if (!e || !*e) e = getenv("LANG");
    if (e && *e) {
        const char *code = language_for_locale(e);
        return code ? code : "en-US";
    }
#endif
    int count = 0;
    SDL_Locale **locales = SDL_GetPreferredLocales(&count);
    const char *code = NULL;
    for (int i = 0; locales && i < count && !code; i++) {
        char locale[128];
        snprintf(locale, sizeof locale, "%s%s%s", locales[i]->language ? locales[i]->language : "",
                 locales[i]->country ? "-" : "", locales[i]->country ? locales[i]->country : "");
        code = language_for_locale(locale);
    }
    SDL_free(locales);
    if (code) return code;
    return "en-US";
}

const char *settings_language(void) {
    int i = g_settings.language;
    return i > LANG_AUTO && i < N_LANGUAGES ? g_language_codes[i] : host_language();
}

const char *settings_language_code(void) {
    const char *code = settings_language();
    if (!strcmp(code, "zh-CN") || !strcmp(code, "zh-Hans")) return "zh-Hans";
    if (!strcmp(code, "zh-TW") || !strcmp(code, "zh-Hant")) return "zh-Hant";
    if (!strcmp(code, "en")) return "en-US";
    return code;
}

const char *settings_language_name(void) {
    const char *c = settings_language();
    if (!strcmp(c, "zh-Hans") || !strcmp(c, "zh-CN")) return "Chinese (Simplified)";
    if (!strcmp(c, "zh-Hant") || !strcmp(c, "zh-TW")) return "Chinese (Traditional)";
    for (int i = 0; i < N_LANGUAGES; i++)
        if (!strcmp(c, g_language_codes[i])) return g_language_names[i];
    return "English";
}

static char game_language[8];
RT_ON_RESET(language_reset) { game_language[0] = 0; }
void settings_game_started(void) {
    snprintf(game_language, sizeof game_language, "%s", settings_language_code());
    LOG("[language] game starting in %s\n", game_language);
}
bool settings_language_pending(void) {
    return game_language[0] && strcmp(game_language, settings_language_code()) != 0;
}

static bool s_settings_loaded;
RT_ON_RESET(settings_reset) { s_settings_loaded = false; }

void settings_load(void) {
    if (s_settings_loaded) return;
    s_settings_loaded = true;
    FILE *f = fopen(SETTINGS_FILE, "r");
    if (!f) return;
    char line[2048];
    bool tip_key = false, aspect_key = false;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;  // Windows uses binary stdio, so CRLF must be removed explicitly.
        char key[64];
        char val[1024] = "";
        if (sscanf(line, " %63[^= ] = %1023[^\n]", key, val) < 1) continue;
        if (!strcmp(key, "rando_seed")) {
            if (sscanf(line, " %63[^= ] = %1023[^\n]", key, val) != 2) val[0] = 0;
            strlcpy(g_settings.rando_seed, val, sizeof g_settings.rando_seed);
            continue;
        }
        if (!strcmp(key, "rom_path")) {
            if (sscanf(line, " %63[^= ] = %1023[^\n]", key, val) != 2) val[0] = 0;
            strlcpy(g_settings.rom_path, val, sizeof g_settings.rom_path);
            continue;
        }
        if (!strcmp(key, "keys_path")) {
            if (sscanf(line, " %63[^= ] = %1023[^\n]", key, val) != 2) val[0] = 0;
            strlcpy(g_settings.keys_path, val, sizeof g_settings.keys_path);
            continue;
        }
        if (!strcmp(key, "aspect")) g_settings.aspect = !strcmp(val, "original") ? ASPECT_ORIGINAL : ASPECT_UNLOCKED;
        else if (!strcmp(key, "fullscreen")) g_settings.fullscreen = atoi(val) != 0;
        else if (!strcmp(key, "hd_textures")) g_settings.hd_textures = atoi(val) != 0;
        else if (!strcmp(key, "reshade")) g_settings.reshade = atoi(val) != 0;
        else if (!strcmp(key, "volume")) g_settings.volume = (float)atof(val);
        else if (!strcmp(key, "reverb")) g_settings.reverb = atoi(val) != 0;
        else if (!strcmp(key, "movie_audio")) g_settings.movie_audio = atoi(val) != 0;
        else if (!strcmp(key, "show_fps")) g_settings.show_fps = atoi(val) != 0;
        else if (!strcmp(key, "frame_rate")) g_settings.frame_rate = atoi(val) == 120 ? 120 : 60;
        else if (!strcmp(key, "resolution")) {
            int n = atoi(val);
            g_settings.res_mode = !strcmp(val, "dynamic") ? RES_DYNAMIC : (!strcmp(val, "75%") || !strcmp(val, "67%")) ? RES_Q75 :
                                  !strcmp(val, "50%") ? RES_Q50 : !strcmp(val, "25%") ? RES_Q25 :
                                  (n >= 1 && n <= 4) ? n : n > 4 ? 4 : RES_MATCH;  // (5x-8x were removed)
        }
        else if (!strcmp(key, "upscaler")) g_settings.upscaler = !strcmp(val, "bilinear") ? UPSCALE_BILINEAR : !strcmp(val, "nis") ? UPSCALE_NIS : !strcmp(val, "fsr1") ? UPSCALE_FSR1 : UPSCALE_SHARP;  // (metalfx / fsr1 / nis)
        else if (!strcmp(key, "antialias")) g_settings.antialias = !strcmp(val, "off") ? AA_OFF : AA_SMAA;  // ("fxaa": SMAA now)
        else if (!strcmp(key, "inv_keys")) g_settings.inv_keys = atoi(val) != 0;
        else if (!strcmp(key, "dpad_move")) g_settings.dpad_move = atoi(val) != 0;
        else if (!strcmp(key, "free_cam")) g_settings.free_cam = atoi(val) != 0;
        else if (!strcmp(key, "free_cam_sens")) g_settings.free_cam_sens = (float)atof(val);
        else if (!strcmp(key, "mouse_controls")) g_settings.mouse_controls = atoi(val) != 0;
        else if (!strcmp(key, "mouse_sens")) g_settings.mouse_sens = (float)atof(val);
        else if (!strcmp(key, "move_360")) g_settings.move_360 = atoi(val) != 0;
        else if (!strcmp(key, "disabled_mods") || !strcmp(key, "mod_order")) mods_settings_load(key, val);
        else if (!strcmp(key, "blur_removal")) g_settings.blur_removal = atoi(val) != 0;
        else if (!strcmp(key, "hide_hero_icon")) g_settings.hide_hero_icon = atoi(val) != 0;
        else if (!strcmp(key, "mirror_mode")) g_settings.mirror_mode = atoi(val) != 0;
        else if (!strcmp(key, "prompts")) g_settings.prompts = !strcmp(val, "nintendo") ? PROMPTS_NINTENDO : !strcmp(val, "xbox") ?
                                                              PROMPTS_XBOX : !strcmp(val, "playstation") ? PROMPTS_PLAYSTATION :
                                                              !strcmp(val, "keyboard") ? PROMPTS_KEYBOARD : PROMPTS_AUTO;
        else if (!strcmp(key, "language")) {  // ("auto" or one of the game's codes; anything else: auto)
            g_settings.language = LANG_AUTO;
            const char *code = language_for_locale(val);
            if (!strcmp(val, "zh-Hans") || !strcmp(val, "zh-CN") || (code && !strcmp(code, "zh-Hans")))
                g_settings.language = 10;
            else if (!strcmp(val, "zh-Hant") || !strcmp(val, "zh-TW") || (code && !strcmp(code, "zh-Hant")))
                g_settings.language = 11;
            else {
                for (int i = 1; i < N_LANGUAGES; i++)
                    if (!strcmp(val, g_language_codes[i]) || (code && !strcmp(code, g_language_codes[i]))) g_settings.language = i;
            }
        }
        else if (!strcmp(key, "tracker")) g_settings.tracker = atoi(val) != 0;
        else if (!strcmp(key, "menu_tip_seen")) g_settings.menu_tip_seen = atoi(val) != 0, tip_key = true;
        else if (!strcmp(key, "aspect_asked")) g_settings.aspect_asked = atoi(val) != 0, aspect_key = true;
        else if (!strcmp(key, "chaos_letters"))
            g_settings.chaos_letters = !strcmp(val, "off") ? CHAOS_LETTERS_OFF : !strcmp(val, "explored") ? CHAOS_LETTERS_EXPLORED : CHAOS_LETTERS_MAP;
        else if (!strcmp(key, "cheat_hearts")) g_settings.cheat_hearts = atoi(val) != 0;
        else if (!strcmp(key, "cheat_ammo")) g_settings.cheat_ammo = atoi(val) != 0;
        else if (!strcmp(key, "cheat_rupees")) g_settings.cheat_rupees = atoi(val) != 0;
        else if (!strcmp(key, "cheat_hookshot")) g_settings.cheat_hookshot = atoi(val) != 0;
        else if (!strcmp(key, "no_buff_drops")) g_settings.no_buff_drops = atoi(val) != 0;
        else if (!strcmp(key, "diff_damage")) g_settings.diff_damage = (float)atof(val);
        else if (!strcmp(key, "diff_ohko")) g_settings.diff_ohko = atoi(val) != 0;
        else if (!strcmp(key, "diff_hero")) g_settings.diff_hero = atoi(val) != 0;
        else if (!strcmp(key, "tunic")) { for (int i = 0; i < N_TUNIC_COLOURS; i++) if (!strcmp(val, g_tunic_colours[i])) g_settings.tunic = i; }
        else if (!strcmp(key, "hearts")) { for (int i = 0; i < N_HEART_COLOURS; i++) if (!strcmp(val, g_heart_colours[i])) g_settings.hearts = i; }
        else bind_settings_load(key, val);  // bind_pad_* / bind_key_*: runtime/hle/bindings.c
    }
    fclose(f);
    if (!tip_key) g_settings.menu_tip_seen = true;  // (a settings file from before the note: not a first launch)
    if (!aspect_key) g_settings.aspect_asked = true;  // (likewise: the aspect ratio was chosen already)
    if (g_settings.volume < 0) g_settings.volume = 0;
    if (g_settings.volume > 2) g_settings.volume = 2;
    if (!isfinite(g_settings.free_cam_sens)) g_settings.free_cam_sens = 1.0f;
    if (g_settings.free_cam_sens < 0.25f) g_settings.free_cam_sens = 0.25f;
    if (g_settings.free_cam_sens > 2.0f) g_settings.free_cam_sens = 2.0f;
    if (!isfinite(g_settings.mouse_sens)) g_settings.mouse_sens = 1.0f;
    if (g_settings.mouse_sens < 0.25f) g_settings.mouse_sens = 0.25f;
    if (g_settings.mouse_sens > 4.0f) g_settings.mouse_sens = 4.0f;
    LOG("[settings] loaded %s\n", SETTINGS_FILE);
}

void settings_save(void) {
    FILE *f = fopen(SETTINGS_FILE, "w");
    if (!f) { LOG("[settings] cannot write %s\n", SETTINGS_FILE); return; }
    fprintf(f, "# Sky Recomp settings (edited from the in-game menu: F1, Esc, or Minus+Plus on a controller)\n");
    fprintf(f, "aspect = %s\n", g_settings.aspect == ASPECT_ORIGINAL ? "original" : "unlocked");
    fprintf(f, "fullscreen = %d\n", g_settings.fullscreen);
    fprintf(f, "hd_textures = %d\nreshade = %d\n", g_settings.hd_textures, g_settings.reshade);
    fprintf(f, "volume = %.2f\n", g_settings.volume);
    fprintf(f, "reverb = %d\n", g_settings.reverb);
    fprintf(f, "movie_audio = %d\n", g_settings.movie_audio);
    fprintf(f, "show_fps = %d\n", g_settings.show_fps);
    fprintf(f, "frame_rate = %d\n", g_settings.frame_rate);
    fprintf(f, "upscaler = %s\n", g_settings.upscaler == UPSCALE_METALFX ? "metalfx" : g_settings.upscaler == UPSCALE_NIS ? "nis" : g_settings.upscaler == UPSCALE_FSR1 ? "fsr1" : "bilinear");
    fprintf(f, "antialias = %s\n", g_settings.antialias == AA_SMAA ? "smaa" : "off");
    if (g_settings.res_mode == RES_DYNAMIC) fprintf(f, "resolution = dynamic\n");
    else if (g_settings.res_mode == RES_Q75) fprintf(f, "resolution = 75%%\n");
    else if (g_settings.res_mode == RES_Q25) fprintf(f, "resolution = 25%%\n");
    else if (g_settings.res_mode == RES_Q50) fprintf(f, "resolution = 50%%\n");
    else if (g_settings.res_mode >= 1 && g_settings.res_mode <= 8) fprintf(f, "resolution = %dx\n", g_settings.res_mode);
    else fprintf(f, "resolution = match\n");
    fprintf(f, "inv_keys = %d\n", g_settings.inv_keys);
    fprintf(f, "dpad_move = %d\n", g_settings.dpad_move);
    fprintf(f, "free_cam = %d\nfree_cam_sens = %.2f\n", g_settings.free_cam, g_settings.free_cam_sens);
    fprintf(f, "mouse_controls = %d\nmouse_sens = %.2f\n", g_settings.mouse_controls, g_settings.mouse_sens);
    fprintf(f, "move_360 = %d\nblur_removal = %d\nhide_hero_icon = %d\n", g_settings.move_360, g_settings.blur_removal,
            g_settings.hide_hero_icon);
    mods_settings_save(f);
    static const char *const prompts[] = {"auto", "nintendo", "xbox", "playstation", "keyboard"};
    fprintf(f, "prompts = %s\n", g_settings.prompts >= 0 && g_settings.prompts < 5 ? prompts[g_settings.prompts] : "auto");
    fprintf(f, "mirror_mode = %d\n", g_settings.mirror_mode);
    fprintf(f, "language = %s\n", g_language_codes[g_settings.language > LANG_AUTO && g_settings.language < N_LANGUAGES
                                                     ? g_settings.language : LANG_AUTO]);
    fprintf(f, "tracker = %d\n", g_settings.tracker);
    fprintf(f, "aspect_asked = %d\n", g_settings.aspect_asked);
    fprintf(f, "menu_tip_seen = %d\n", g_settings.menu_tip_seen);
    static const char *const letters[] = {"off", "explored", "map"};
    fprintf(f, "chaos_letters = %s\n", letters[g_settings.chaos_letters]);
    fprintf(f, "cheat_hearts = %d\ncheat_ammo = %d\ncheat_rupees = %d\n", g_settings.cheat_hearts, g_settings.cheat_ammo,
            g_settings.cheat_rupees);
    fprintf(f, "cheat_hookshot = %d\n", g_settings.cheat_hookshot);
    fprintf(f, "no_buff_drops = %d\n", g_settings.no_buff_drops);
    fprintf(f, "diff_damage = %.2f\ndiff_ohko = %d\ndiff_hero = %d\n", g_settings.diff_damage, g_settings.diff_ohko,
            g_settings.diff_hero);
    fprintf(f, "tunic = %s\nhearts = %s\n", g_tunic_colours[g_settings.tunic], g_heart_colours[g_settings.hearts]);
    bind_settings_save(f);
    fprintf(f, "rando_seed = %s\n", g_settings.rando_seed);
    fprintf(f, "rom_path = %s\n", g_settings.rom_path);
    fprintf(f, "keys_path = %s\n", g_settings.keys_path);
    fclose(f);
}
