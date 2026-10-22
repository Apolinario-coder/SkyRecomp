#include <assert.h>
#include "../runtime/nvn/custom_tex.c"
Settings g_settings = {.hd_textures = true};

int main(int argc, char **argv) {
    assert(argc == 2);
    CustomTex parsed;
    assert(parse_name("tex2d_72950631827607e2842812cc870dcae2_2x2x1_0x0.png", &parsed));
    assert(!parse_name("tex2d_1234_2x2x1_0x0.png", &parsed));
    assert(!parse_name("tex2d_72950631827607e2842812cc870dcae2_0x2x1_0x0.png", &parsed));
    assert(!parse_name("tex2d_72950631827607e2842812cc870dcae2_2x2x6_0x0.png", &parsed));
    assert(!parse_name("tex2d_72950631827607e2842812cc870dcae2_2x2x1_0x0.txt", &parsed));
    assert(!parse_name("tex2d_72950631827607e2842812cc870dcae2_2x2x1_0x1.png", &parsed));
    char path[1400];
    snprintf(path, sizeof path, "%s/tex2d_72950631827607e2842812cc870dcae2_2x2x1_0x0.png", argv[1]);
    SDL_Surface *s = SDL_CreateSurface(4, 4, SDL_PIXELFORMAT_RGBA32);
    assert(s);
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
        uint8_t *p = (uint8_t *)s->pixels + y * s->pitch + x * 4;
        p[0] = 17; p[1] = 34; p[2] = 51; p[3] = 255;
    }
    assert(SDL_SavePNG(s, path));
    SDL_DestroySurface(s);
    #ifdef _WIN32
    assert(_putenv_s("SKYRECOMP_TEXTURE_DIR", argv[1]) == 0);
#else
    assert(setenv("SKYRECOMP_TEXTURE_DIR", argv[1], 1) == 0);
#endif
    assert(custom_tex_count() == 1);
    static const FormatInfo fmt = {4, 1, 1, FMT_COLOR, "RGBA8"};
    NvnTexture t = {.width = 2, .height = 2, .target = 1, .flags = 0x10, .stride = 8, .levels = 2, .samples = 1,
                   .layout = {.width = 2, .height = 2, .depth = 1, .layers = 1, .levels = 2, .fmt = &fmt}};
    tegra_layout_init(&t.layout);
    uint8_t *data = calloc(1, tegra_total_size(&t.layout));
    assert(data);
    for (int i = 0; i < 16; i++) data[i] = i;
    t.addr = (uintptr_t)data;
    s = custom_tex_load(&t); // The known XXH3-128 vector matches mip zero, even with a mip chain.
    assert(s && s->w == 4 && s->h == 4);
    assert(!memcmp(s->pixels, (uint8_t[]){17, 34, 51, 255}, 4));
    SDL_DestroySurface(s);
    g_settings.hd_textures = false;
    assert(!custom_tex_load(&t));
    g_settings.hd_textures = true;
    s = custom_tex_load(&t);
    assert(s && s->w == 4);
    SDL_DestroySurface(s);
    data[0] ^= 1;
    assert(!custom_tex_load(&t));
    data[0] ^= 1;
    t.flags |= 8;
    assert(!custom_tex_load(&t));
    t.flags &= ~8;
    FILE *f = fopen(path, "wb");
    assert(f); fputs("invalid PNG", f); fclose(f);
    assert(!custom_tex_load(&t));
    free(data);
    assert(remove(path) == 0);
    puts("custom texture lookup, PNG loading and fallback checks passed");
}
