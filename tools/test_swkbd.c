// Regression coverage for the actual HLE entry point, with only the UI mocked.
// clang -Iruntime tools/test_swkbd.c -lSDL3 -o build/test_swkbd.exe
#include <assert.h>
#include "../runtime/hle/swkbd.c"

static const char *answer, *expected_initial;
static int confirm;
int menu_keyboard_run(const char *initial, int max_len, char *out, int cap) {
    (void)max_len;
    assert(strcmp(initial, expected_initial) == 0);
    SDL_utf8strlcpy(out, answer, cap);
    return confirm;
}

static void run(bool utf8, size_t size, int max_len, const uint16_t *initial, size_t n, unsigned char *result) {
    unsigned char arg[0x500] = {0};
    uint64_t out[2] = {(uintptr_t)result, size};
    *(int32_t *)(arg + 0x3ac) = max_len;
    arg[0x3bd] = utf8;
    *(uint32_t *)(arg + 0x3c4) = (uint32_t)n;
    *(uint64_t *)(arg + 0x4d0) = (uintptr_t)initial;
    Cpu c = {0};
    c.x[0] = (uintptr_t)out;
    c.x[1] = (uintptr_t)arg;
    swkbd_show(&c);
    assert(c.x[0] == 0);
}

int main(void) {
    unsigned char result[32];
    uint16_t initial[] = {'L', 0xed, 'n', 'k', 0};
    expected_initial = "L\xc3\xadnk";
    answer = "Link"; confirm = 1;
    memset(result, 0xa5, sizeof result);
    run(false, 10, 8, initial, 4, result);
    assert(memcmp(result, "L\0i\0n\0k\0\0\0", 10) == 0 && result[10] == 0xa5);
    run(true, 10, 8, initial, 4, result);
    assert(strcmp((char *)result, "Link") == 0);
    answer = "Jo\xc3\xa3o";
    run(false, 10, 8, initial, 4, result);
    const uint16_t joao[] = {'J', 'o', 0xe3, 'o', 0};
    assert(memcmp(result, joao, sizeof joao) == 0);
    confirm = 0;
    run(false, 10, 8, initial, 4, result);
    assert(memcmp(result, initial, sizeof initial) == 0);
    confirm = 1; answer = "A\xf0\x9f\x98\x80Z";
    run(false, 6, 8, initial, 4, result); // no room for the surrogate pair + terminator
    assert(result[0] == 'A' && result[2] == 0 && result[4] == 0);
    run(false, 8, 8, initial, 4, result);
    const uint16_t emoji[] = {'A', 0xd83d, 0xde00, 0};
    assert(memcmp(result, emoji, sizeof emoji) == 0);
    answer = "Link";
    run(false, 10, 2, initial, 4, result);
    assert(memcmp(result, "L\0i\0\0\0", 6) == 0);
    memset(result, 0xa5, sizeof result);
    run(false, 1, 8, initial, 4, result);
    assert(result[0] == 0 && result[1] == 0xa5);
    run(false, 0, 8, initial, 4, result);
    assert(result[1] == 0xa5);
    puts("swkbd: UTF-8/UTF-16, accents, cancellation, bounds and surrogate pairs passed");
}
