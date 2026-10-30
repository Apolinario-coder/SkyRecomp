#include <assert.h>
#include "../runtime/gamebuild.c"

static void put(const char *path, const char *s) {
    FILE *f = fopen(path, "wb"); assert(f);
    assert(fwrite(s, 1, strlen(s), f) == strlen(s)); assert(!fclose(f));
}

int main(void) {
    Tools t = {0};
    strcpy(t.cc, "compiler.fixture"); strcpy(t.inc, ".");
    put(t.cc, "compiler1");
    const char *headers[] = {"cpu.h", "simd.h", "simd_fp.h", "gamelib.h"};
    for (int i = 0; i < 4; i++) put(headers[i], "header1");
    assert(build_stamp(&t));
    Unit u = {0};
    strcpy(u.src, "unit.c"); strcpy(u.obj, "unit.o"); strcpy(u.stamp, "unit.stamp");
    put(u.src, "source1"); put(u.obj, "object");
    assert(!unit_done(&t, &u)); unit_mark(&t, &u); assert(unit_done(&t, &u));
    put(u.src, "source2"); assert(!unit_done(&t, &u)); unit_mark(&t, &u);
    for (int i = 0; i < 4; i++) {
        put(headers[i], "header2"); assert(build_stamp(&t));
        assert(!unit_done(&t, &u)); unit_mark(&t, &u); assert(unit_done(&t, &u));
    }
    put(t.cc, "compiler2"); assert(build_stamp(&t)); assert(!unit_done(&t, &u));
    unit_mark(&t, &u); assert(unit_done(&t, &u));
    put(u.obj, ""); assert(!unit_done(&t, &u)); put(u.obj, "object");
    remove(u.stamp); assert(!unit_done(&t, &u));
    remove(u.src); assert(!unit_done(&t, &u));
    remove(headers[0]); assert(!build_stamp(&t));
    for (int i = 0; i < 4; i++) remove(headers[i]);
    remove(t.cc); remove(u.obj);
    puts("build resume: source, compiler, every header, incomplete objects and missing stamps passed");
}
