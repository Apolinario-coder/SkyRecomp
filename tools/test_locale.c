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
    assert(!strcmp(settings_language(), "en-US"));
    assert(!strcmp(settings_language_code(), "en-US"));
    assert(SDL_SetHint(SDL_HINT_PREFERRED_LOCALES, "es_ES"));
    assert(!strcmp(settings_language(), "en-US"));
    assert(SDL_SetHint(SDL_HINT_PREFERRED_LOCALES, "zh-Hans"));
    assert(!strcmp(settings_language(), "en-US"));

    // In-game menu selections
    g_settings.language = 1; // ja
    assert(!strcmp(settings_language(), "ja"));
    assert(!strcmp(settings_language_code(), "ja"));

    g_settings.language = 3; // es
    assert(!strcmp(settings_language(), "es"));
    assert(!strcmp(settings_language_code(), "es"));

    g_settings.language = 2; // en-US
    assert(!strcmp(settings_language(), "en-US"));
    assert(!strcmp(settings_language_code(), "en-US"));

    g_settings.language = 10;
    assert(!strcmp(settings_language_code(), "zh-Hans"));

    g_settings.language = 11;
    assert(!strcmp(settings_language(), "zh-TW"));
    assert(!strcmp(settings_language_code(), "zh-Hant"));

    g_settings.language = LANG_AUTO;
    assert(!strcmp(settings_language(), "en-US"));
    puts("Default English and manual language selection: PASS");
}
