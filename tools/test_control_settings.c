#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#endif
#include "../runtime/settings.h"

int main(int argc, char **argv) {
    const char *value = argc > 1 ? argv[1] : "nan";
#ifdef _WIN32
    _fmode = _O_BINARY;
    _putenv_s("SKYRECOMP_SETTINGS", "control-settings.fixture");
#else
    setenv("SKYRECOMP_SETTINGS", "control-settings.fixture", 1);
#endif
    FILE *f = fopen("control-settings.fixture", "wb"); assert(f);
    fprintf(f, "mouse_controls = 0\r\nmouse_sens = %s\r\nfree_cam_sens = nan\r\ngyro_sens = 0.01\r\nlanguage = es\r\n", value);
    fclose(f);
    settings_load();
    float want = !strcmp(value, "nan") ? 1 : atof(value) < .25 ? .25f : 4;
    assert(!g_settings.mouse_controls && g_settings.mouse_sens == want && g_settings.free_cam_sens == 1 &&
           g_settings.gyro_sens == 0.1f);
    assert(!settings_language_pending());
    settings_game_started(); assert(!settings_language_pending());
    assert(!strcmp(settings_language(), "es"));
    g_settings.language = 1; assert(settings_language_pending());
    g_settings.language = 3; assert(!settings_language_pending());
    g_settings.language = 1; settings_game_started(); assert(!settings_language_pending());
    extern bool g_is_skyward;
    g_is_skyward = true;
    g_settings.language = 10; assert(!strcmp(settings_language_code(), "zh-Hans"));
    g_settings.language = 11; assert(!strcmp(settings_language_code(), "zh-Hant"));
    g_is_skyward = false; assert(!strcmp(settings_language_code(), "zh-TW"));
    g_settings.language = 1;
    settings_save();
    f = fopen("control-settings.fixture", "r"); assert(f);
    char text[8192] = {0}; assert(fread(text, 1, sizeof text - 1, f) > 0); fclose(f);
    assert(strstr(text, "mouse_controls = 0") && strstr(text, "mouse_sens = ") && strstr(text, "gyro_sens = 0.10") &&
           strstr(text, "language = ja"));
    remove("control-settings.fixture");
    puts("control settings: sensitivity bounds, persistence, pending language and restart acknowledgement passed");
}
