// Compile with function/data sections and linker garbage collection, linking SDL3.
#include <assert.h>
#include <SDL3/SDL_hints.h>
#include "../runtime/settings.c"

// The unrelated settings-file code is linked on Windows too.
bool g_is_skyward;
const char *const g_tunic_colours[N_TUNIC_COLOURS] = {0};
const char *const g_heart_colours[N_HEART_COLOURS] = {0};
void mods_settings_load(const char *key, const char *val) { (void)key; (void)val; abort(); }
void mods_settings_save(FILE *file) { (void)file; abort(); }
bool bind_settings_load(const char *key, const char *val) { (void)key; (void)val; abort(); }
void bind_settings_save(FILE *file) { (void)file; abort(); }

static bool s_mock_has_us_en;
static bool s_mock_has_eu_en;
static bool s_mock_has_us_es;
static bool s_mock_has_eu_es;

bool romfs_find_file(const char *rel, int *out_fd, uint64_t *out_off, uint64_t *out_size) {
    (void)out_fd; (void)out_off; (void)out_size;
    if (!strcmp(rel, "US/Object/en_US/0-Common.arc")) return s_mock_has_us_en;
    if (!strcmp(rel, "EU/Object/en_GB/0-Common.arc")) return s_mock_has_eu_en;
    if (!strcmp(rel, "US/Object/es_US/0-Common.arc")) return s_mock_has_us_es;
    if (!strcmp(rel, "EU/Object/es_ES/0-Common.arc")) return s_mock_has_eu_es;
    return false;
}

int main(void) {
    puts("Checking locale parsing"); fflush(stdout);
    const struct { const char *locale, *expected; } cases[] = {
        {"zh-Hans", "zh-Hans"}, {"zh-Hans-CN", "zh-Hans"}, {"zh_CN.UTF-8", "zh-Hans"},
        {"zh-CN", "zh-Hans"}, {"zh-SG", "zh-Hans"}, {"ZH_hAnS", "zh-Hans"},
        {"zh-Hant", "zh-Hant"}, {"zh-Hant-CN", "zh-Hant"}, {"zh-Hans-TW", "zh-Hans"},
        {"zh_TW.UTF-8", "zh-Hant"}, {"zh-HK", "zh-Hant"}, {"zh-MO", "zh-Hant"},
        {"fr-CA", "fr"}, {"de_DE.UTF-8", "de"}, {"ja-JP", "ja"}, {"en-US", "en-US"}
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++)
        assert(!strcmp(language_for_locale(cases[i].locale), cases[i].expected));
    assert(!language_for_locale(NULL));
    assert(!language_for_locale(""));
    assert(!language_for_locale("C.UTF-8"));
    assert(!language_for_locale("zhgarbage-CN"));
    puts("Checking default and manual language preferences"); fflush(stdout);
    g_settings.language = LANG_AUTO;
    // Default must ALWAYS be English, never influenced by system locale or hints
    assert(!strcmp(settings_language(), "default"));
    assert(!strcmp(settings_language_code(), "en-US"));
    assert(SDL_SetHint(SDL_HINT_PREFERRED_LOCALES, "es_ES"));
    assert(!strcmp(settings_language(), "default"));
    assert(!strcmp(settings_language_code(), "en-US"));
    assert(SDL_SetHint(SDL_HINT_PREFERRED_LOCALES, "zh-Hans"));
    assert(!strcmp(settings_language(), "default"));
    assert(!strcmp(settings_language_code(), "en-US"));

    // In-game menu selections
    g_settings.language = 1; // en-US
    assert(!strcmp(settings_language(), "en-US"));
    assert(!strcmp(settings_language_code(), "en-US"));

    g_settings.language = 2; // en-GB
    assert(!strcmp(settings_language(), "en-GB"));
    assert(!strcmp(settings_language_code(), "en-GB"));

    g_settings.language = 3; // es
    assert(!strcmp(settings_language(), "es"));
    assert(!strcmp(settings_language_code(), "es"));

    g_settings.language = 4; // es-419
    assert(!strcmp(settings_language(), "es-419"));
    assert(!strcmp(settings_language_code(), "es-419"));

    g_settings.language = 11; // ja
    assert(!strcmp(settings_language(), "ja"));
    assert(!strcmp(settings_language_code(), "ja"));

    g_settings.language = 13; // zh-CN
    assert(!strcmp(settings_language_code(), "zh-Hans"));

    g_settings.language = 14; // zh-TW
    assert(!strcmp(settings_language(), "zh-TW"));
    assert(!strcmp(settings_language_code(), "zh-Hant"));

    g_settings.language = LANG_AUTO;
    assert(!strcmp(settings_language_code(), "en-US"));

    // European dump test: only EU/Object/en_GB exists
    s_mock_has_us_en = false;
    s_mock_has_eu_en = true;
    g_settings.language = LANG_AUTO;
    assert(!strcmp(settings_language_code(), "en-GB"));
    g_settings.language = 1; // en-US requested -> auto fallback to en-GB
    assert(!strcmp(settings_language_code(), "en-GB"));
    g_settings.language = 2; // en-GB requested
    assert(!strcmp(settings_language_code(), "en-GB"));

    // US dump test: only US/Object/en_US exists
    s_mock_has_us_en = true;
    s_mock_has_eu_en = false;
    g_settings.language = LANG_AUTO;
    assert(!strcmp(settings_language_code(), "en-US"));
    g_settings.language = 1; // en-US requested
    assert(!strcmp(settings_language_code(), "en-US"));
    g_settings.language = 2; // en-GB requested -> auto fallback to en-US
    assert(!strcmp(settings_language_code(), "en-US"));

    // Spanish regional fallback test
    s_mock_has_eu_es = false;
    s_mock_has_us_es = true; // only Latin American Spanish exists
    g_settings.language = 3; // es (Europe) requested -> auto fallback to es-419
    assert(!strcmp(settings_language_code(), "es-419"));

    puts("Default English and manual language selection: PASS");
}
