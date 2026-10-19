// Software keyboard (naming Link on a new file, ...). On the Switch nn::swkbd::ShowKeyboard launches the keyboard
// library applet; our am service has no applets, so the game crashed. ShowKeyboard is replaced here: the overlay
// (runtime/ui/menu.mm menu_keyboard_run) asks for the text - typed directly with a keyboard, an on-screen keyboard
// with a controller - while this (game) thread keeps re-presenting the last frame.
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL_stdinc.h>

#include "../hle.h"

int menu_keyboard_run(const char *initial, int max_len, char *out, int cap);  // runtime/ui/menu.mm

// nn::swkbd::ShowKeyboard(nn::swkbd::String *out, const nn::swkbd::ShowKeyboardArg &arg) -> nn::Result
// String = {void *ptr; size_t bufSize}
// ShowKeyboardArg (probed 2026-10-08, naming Link): +0x3ac text max length, +0x3b0 min length, +0x3c0 / +0x3c4
// initial text offset / length (UTF-16) in the work buffer at +0x4d0.
// KeyboardConfig::IsUseUtf8 (+0x3bd) selects the output encoding; otherwise it is UTF-16LE.
HLE_FUNC("_ZN2nn5swkbd12ShowKeyboardEPNS0_6StringERKNS0_15ShowKeyboardArgE", swkbd_show) {
    uint64_t out = c->x[0], arg = c->x[1];
    uint64_t ptr = LD64(out), size = LD64(out + 8);
    int max_len = (int)*(const int32_t *)(uintptr_t)(arg + 0x3ac);
    uint32_t off = *(const uint32_t *)(uintptr_t)(arg + 0x3c0), n = *(const uint32_t *)(uintptr_t)(arg + 0x3c4);
    uint64_t wb = LD64(arg + 0x4d0);
    char initial[96] = "";
    if (wb && n) {
        // Bound the read and supply our own terminator (the length excludes NUL).
        uint16_t u[96] = {0};
        size_t count = n < 95 ? n : 95;
        memcpy(u, (const void *)(uintptr_t)(wb + off), count * 2);
        if (count && u[count - 1] >= 0xd800 && u[count - 1] <= 0xdbff) u[--count] = 0;
        char *s = SDL_iconv_string("UTF-8", "UTF-16LE", (const char *)u, (count + 1) * 2);
        if (s) { SDL_utf8strlcpy(initial, s, sizeof initial); SDL_free(s); }
    }
    char text[96] = {0};
    int ok = menu_keyboard_run(initial, max_len, text, sizeof text);
    if (!ok) snprintf(text, sizeof text, "%s", initial);  // cancelled: keep what was there
    LOG("[swkbd] name entry (max %d, initial \"%s\"): %s \"%s\"\n", max_len, initial, ok ? "entered" : "cancelled", text);
    if (ptr && size) {
        memset((void *)(uintptr_t)ptr, 0, size);
        if (*(const uint8_t *)(uintptr_t)(arg + 0x3bd)) {
            SDL_utf8strlcpy((char *)(uintptr_t)ptr, text, size);
        } else if (size >= 2) {
            uint16_t *s = (uint16_t *)SDL_iconv_string("UTF-16LE", "UTF-8", text, strlen(text) + 1);
            if (s) {
                size_t count = 0, cap = size / 2 - 1;
                if (max_len > 0 && cap > (size_t)max_len) cap = (size_t)max_len;
                while (count < cap && s[count]) count++;
                if (count && s[count - 1] >= 0xd800 && s[count - 1] <= 0xdbff) count--;
                memcpy((void *)(uintptr_t)ptr, s, count * 2);
                SDL_free(s);
            }
        }
    }
    c->x[0] = 0;
}
