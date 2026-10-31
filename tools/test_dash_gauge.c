// The dash gauge layout patch (runtime/hle/dash_gauge.c) on the player's own Layout/DoButton.arc (skips without it):
// the patched archive still holds a valid layout with 3..7 wings, re-centred, names and the lit material in place.
// clang -O1 -Iruntime tools/test_dash_gauge.c -o build/test_dash_gauge && build/test_dash_gauge [extracted/romfs]
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include "../runtime/hle/dash_gauge.c"

bool g_is_skyward = true;
static uint32_t dash_max = 3;
uint32_t rt_dash_max(void) { return dash_max; }
static char root[1024];
const char *rt_data_root(void) { return root; }

static float getf(const uint8_t *p) { uint32_t v = be32(p); float f; memcpy(&f, &v, 4); return f; }

// the brlyt inside a U8 archive
static const uint8_t *find_lyt(const uint8_t *arc, uint32_t *size) {
    uint32_t r = be32(arc + 4), count = be32(arc + r + 8), strtab = r + count * 12;
    for (uint32_t i = 1; i < count; i++) {
        const uint8_t *node = arc + r + i * 12;
        if (!node[0] && !strcmp((const char *)arc + strtab + (be32(node) & 0xFFFFFF), "birdGauge_00.brlyt"))
            return *size = be32(node + 8), arc + be32(node + 4);
    }
    return NULL;
}

int main(int argc, char **argv) {
    char path[1200];
    snprintf(path, sizeof path, "%s/Layout/DoButton.arc", argc > 1 ? argv[1] : "extracted/romfs");
    int fd = open(path, O_RDONLY);
    if (fd < 0) { printf("skipped: no %s\n", path); return 0; }
    off_t size = lseek(fd, 0, SEEK_END);
    char tmp[] = "/tmp/test_dash_gauge_XXXXXX";
    snprintf(root, sizeof root, "%s", mkdtemp(tmp));
    char out[1200];
    assert(!dash_gauge_override(fd, 0, (uint64_t)size, "Layout/DoButton.arc", out, sizeof out));  // 3: the game's own
    for (dash_max = 4; dash_max <= 7; dash_max++) {
        assert(!dash_gauge_override(fd, 0, (uint64_t)size, "Layout/Other.arc", out, sizeof out));
        assert(dash_gauge_override(fd, 0, (uint64_t)size, "Layout/DoButton.arc", out, sizeof out));
        FILE *f = fopen(out, "rb");
        fseek(f, 0, SEEK_END);
        size_t n = (size_t)ftell(f);
        uint8_t *arc = malloc(n);
        fseek(f, 0, SEEK_SET);
        assert(fread(arc, 1, n, f) == n);
        fclose(f);
        uint32_t ln;
        const uint8_t *l = find_lyt(arc, &ln);
        assert(l && n > (size_t)size && !memcmp(l, "RLYT", 4) && be32(l + 8) == ln);
        int wings = 0, lit = -1, nsec = be16(l + 0xE);
        float xs[8];
        uint32_t o = be16(l + 0xC);
        for (int i = 0; i < nsec; o += be32(l + o + 4), i++) {
            assert(o + 8 <= ln);
            const char *name = (const char *)l + o + 0xC;
            if (!memcmp(l + o, "mat1", 4)) {  // the lit copy is the last material
                int nm = be16(l + o + 8);
                const uint8_t *m = l + o + be32(l + o + 0xC + 4 * (nm - 1));
                assert(!strcmp((const char *)m, "P_wing_lit") && be16(m + 0x1C) == 255);
                lit = nm - 1;
            }
            if (!memcmp(l + o, "pan1", 4) && !strncmp(name, "N_wing_0", 8)) xs[wings++] = getf(l + o + 0x24);
            if (!memcmp(l + o, "pic1", 4) && !strncmp(name, "P_wing_0", 8) && name[8] >= '3')
                assert(be16(l + o + 0x5C) == lit);
            if (!memcmp(l + o, "pic1", 4) && !strcmp(name, "P_base_00")) assert(getf(l + o + 0x44) == 256 + 50.0f * (dash_max - 3));
        }
        assert(o == ln && wings == (int)dash_max);
        for (int w = 0; w < wings; w++) assert(xs[w] == (w - (wings - 1) / 2.0f) * 50);
        free(arc);
    }
    close(fd);
    puts("dash gauge: layouts with 4..7 wings parse, centred, lit copies; 3 left alone");
}
