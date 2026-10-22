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
        {"zh-Hans", "zh-CN"}, {"zh-Hans-CN", "zh-CN"}, {"zh_CN.UTF-8", "zh-CN"},
        {"zh-CN", "zh-CN"}, {"zh-SG", "zh-CN"}, {"ZH_hAnS", "zh-CN"},
        {"zh-Hant", "zh-TW"}, {"zh-Hant-CN", "zh-TW"}, {"zh-Hans-TW", "zh-CN"},
        {"zh_TW.UTF-8", "zh-TW"}, {"zh-HK", "zh-TW"}, {"zh-MO", "zh-TW"},
        {"fr-CA", "fr"}, {"de_DE.UTF-8", "de"}, {"ja-JP", "ja"}, {"en-US", "en-US"}
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++)
        assert(!strcmp(language_for_locale(cases[i].locale), cases[i].expected));
    assert(!language_for_locale(NULL));
    assert(!language_for_locale(""));
    assert(!language_for_locale("C.UTF-8"));
    assert(!language_for_locale("zhgarbage-CN"));
    puts("Checking system locale preferences"); fflush(stdout);
#ifdef __linux__
    assert(setenv("LC_ALL", "zh-Hans", 1) == 0);
    assert(!strcmp(settings_language(), "zh-CN"));
    unsetenv("LC_ALL"); unsetenv("LC_MESSAGES"); unsetenv("LANG");
#endif
    assert(SDL_SetHint(SDL_HINT_PREFERRED_LOCALES, "zh-Hans"));
    assert(!strcmp(settings_language(), "zh-CN"));
    assert(SDL_SetHint(SDL_HINT_PREFERRED_LOCALES, "zh_Hant"));
    assert(!strcmp(settings_language(), "zh-TW"));
    assert(SDL_SetHint(SDL_HINT_PREFERRED_LOCALES, "xx_YY,zh_Hans"));
    assert(!strcmp(settings_language(), "zh-CN"));
    g_settings.language = 2; // Explicit English must override Chinese system settings.
    assert(!strcmp(settings_language(), "en-US"));
    g_is_skyward = true;
    g_settings.language = 10;
    assert(!strcmp(settings_language_code(), "zh-Hans"));
    g_settings.language = 11;
    assert(!strcmp(settings_language_code(), "zh-Hant"));
    g_is_skyward = false;
    assert(!strcmp(settings_language_code(), "zh-TW"));
    g_settings.language = LANG_AUTO;
    g_settings.language = LANG_AUTO;
    assert(SDL_SetHint(SDL_HINT_PREFERRED_LOCALES, "xx_YY"));
    assert(!strcmp(settings_language(), "en-US"));
    puts("Locale normalization, system preferences and manual override: PASS");
}
