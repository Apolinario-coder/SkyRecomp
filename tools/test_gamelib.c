// Version checks must rebuild stale game code: test_gamelib <temporary library path>.
#include <assert.h>
#include "../runtime/gamelib.c"

bool rt_resource(const char *name, const char *dev_rel, char *out, size_t n) { return false; }

int main(int argc, char **argv) {
    assert(argc == 2);
    char id[1100];
    snprintf(id, sizeof id, "%s.id", argv[1]);
    FILE *f = fopen(argv[1], "wb");
    assert(f); fclose(f);
#ifdef _WIN32
    assert(_putenv_s("LARECOMP_GAME_LIB", argv[1]) == 0);
#else
    assert(setenv("LARECOMP_GAME_LIB", argv[1], 1) == 0);
#endif
    assert(!id_matches(argv[1]));
    assert(!gamelib_present(NULL));
    f = fopen(id, "wb");
    assert(f); fputs("ccafcd6fe7bda438\n", f); fclose(f);
    assert(!id_matches(argv[1]));
    assert(!gamelib_present(NULL));
    f = fopen(id, "wb");
    assert(f); fputs(GAMELIB_ID "\r\n", f); fclose(f);
    assert(id_matches(argv[1]));
    assert(gamelib_present(NULL));
    assert(remove(id) == 0);
    assert(remove(argv[1]) == 0);
    puts("game library version checks passed");
}
