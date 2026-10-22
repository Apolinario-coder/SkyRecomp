#include "custom_tex.h"
#include "../settings.h"
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#define XXH_INLINE_ALL
#include "../../third_party/xxhash/xxhash.h"

// Ryujinx replacement names use XXH3-128 of the linear texture data, not MD5.
typedef struct { char hash[33], path[1200]; uint32_t w, h; } CustomTex;
static CustomTex entries[4096];
static int count;
static pthread_once_t once = PTHREAD_ONCE_INIT;

static bool parse_name(const char *s, CustomTex *e) {
    unsigned depth; int end = 0;
    if (sscanf(s, "tex2d_%32[0-9a-fA-F]_%ux%ux%u_%n", e->hash, &e->w, &e->h, &depth, &end) != 4 ||
        !end || strlen(e->hash) != 32 || !e->w || !e->h || depth != 1) return false;
    size_t n = strlen(s);
    if (n < 4 || strcmp(s + end, "0x0.png") || e->w > 16384 || e->h > 16384) return false;
    for (int i = 0; i < 32; i++) if (e->hash[i] >= 'A' && e->hash[i] <= 'F') e->hash[i] += 'a' - 'A';
    return true;
}

static void scan_dir(const char *dir, int depth) {
    if (depth > 12) return;  // Bound traversal through user-created directory links.
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *item;
    while ((item = readdir(d))) {
        if (item->d_name[0] == '.') continue;
        char path[1200]; struct stat st;
        if (snprintf(path, sizeof path, "%s/%s", dir, item->d_name) >= sizeof path || stat(path, &st)) continue;
        if (S_ISDIR(st.st_mode)) { scan_dir(path, depth + 1); continue; }
        CustomTex e = {0};
        if (!S_ISREG(st.st_mode) || !parse_name(item->d_name, &e)) continue;
        if (count == 4096) { fprintf(stderr, "[custom_tex] texture limit reached in %s\n", dir); break; }
        strcpy(e.path, path); entries[count++] = e;
    }
    closedir(d);
}

static void init(void) {
    const char *dir = getenv("SKYRECOMP_TEXTURE_DIR");
    scan_dir(dir && *dir ? dir : "mods/textures", 0);
    fprintf(stderr, "[custom_tex] registered %d replacement textures\n", count);
}
void custom_tex_init(void) { pthread_once(&once, init); }
int custom_tex_count(void) { custom_tex_init(); return count; }

static const CustomTex *find(const NvnTexture *t, const void *data, size_t size) {
    XXH128_hash_t h = XXH3_128bits(data, size);
    char hex[33];
    snprintf(hex, sizeof hex, "%016llx%016llx", (unsigned long long)h.high64, (unsigned long long)h.low64);
    for (int i = 0; i < count; i++)
        if (entries[i].w == t->width && entries[i].h == t->height && !strcmp(hex, entries[i].hash)) return &entries[i];
    return NULL;
}

static SDL_Surface *load(const CustomTex *e) {
    // Check dimensions before the decoder can allocate an unexpectedly large image.
    unsigned char header[24]; FILE *f = fopen(e->path, "rb");
    if (!f) return NULL;
    size_t n = fread(header, 1, sizeof header, f); fclose(f);
    if (n != sizeof header || memcmp(header, "\x89PNG\r\n\x1a\n", 8) || memcmp(header + 12, "IHDR", 4)) return NULL;
    uint32_t w = (uint32_t)header[16] << 24 | header[17] << 16 | header[18] << 8 | header[19];
    uint32_t h = (uint32_t)header[20] << 24 | header[21] << 16 | header[22] << 8 | header[23];
    if (!w || !h || w > 16384 || h > 16384 || (uint64_t)w * h > (64ull << 20)) return NULL;
    SDL_Surface *s = SDL_LoadPNG(e->path);
    if (!s) { fprintf(stderr, "[custom_tex] cannot load %s: %s\n", e->path, SDL_GetError()); return NULL; }
    SDL_Surface *rgba = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(s);
    if (rgba) fprintf(stderr, "[custom_tex] replaced %ux%u -> %dx%d: %s\n", e->w, e->h, rgba->w, rgba->h, e->path);
    return rgba;
}

SDL_Surface *custom_tex_load(const NvnTexture *t) {
    if (!g_settings.hd_textures) return NULL;
    custom_tex_init();
    if (!count || !t || !t->addr || t->target != 1 || t->samples > 1 || (t->flags & 8)) return NULL;
    bool candidate = false;
    for (int i = 0; i < count; i++) if (entries[i].w == t->width && entries[i].h == t->height) { candidate = true; break; }
    if (!candidate || !t->layout.fmt || t->layout.layers > 1 || t->layout.depth > 1) return NULL;
    const FormatInfo *fi = t->layout.fmt;
    size_t total = 0, first = 0;
    for (uint32_t lv = 0; lv < t->layout.levels; lv++) {
        LevelLayout l = tegra_level(&t->layout, lv);
        size_t size = (size_t)l.wb * l.hb * fi->bpb;
        if (!lv) first = size;
        if (size > (64u << 20) || total > (64u << 20) - size) return NULL;
        total += size;
    }
    if (!total) return NULL;
    uint8_t *linear = malloc(total);
    if (!linear) return NULL;
    size_t offset = 0;
    for (uint32_t lv = 0; lv < t->layout.levels; lv++) {
        LevelLayout l = tegra_level(&t->layout, lv);
        const uint8_t *src = (const uint8_t *)(uintptr_t)t->addr + l.offset;
        uint32_t row = l.wb * fi->bpb;
        if (t->flags & 0x10) {
            for (uint32_t y = 0; y < l.hb; y++) memcpy(linear + offset + (size_t)y * row, src + (size_t)y * t->stride, row);
        } else tegra_deswizzle(src, linear + offset, l.wb, l.hb, 1, fi->bpb, l.block_height_log2, l.block_depth_log2);
        offset += (size_t)row * l.hb;
    }
    const CustomTex *e = find(t, linear, total);
    if (!e && first != total) e = find(t, linear, first);
    free(linear);
    return e ? load(e) : NULL;
}
