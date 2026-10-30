// Link with the runtime objects, renaming their main symbol for this standalone Vulkan check.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL_timer.h>
#include "../runtime/settings.h"
#include "../runtime/paths.h"
#include "../runtime/nvn/custom_tex.h"
#include "../runtime/nvn/vulkan/vk.h"

void ui_window_init(void);
void renderer_submit(void);
bool vk_frame_begin(const float clear[4]);
void vk_frame_end(const char *shot);

int main(void) {
    rt_paths_init();
    uint8_t data[16];
    for (int i = 0; i < 16; i++) data[i] = i;
    SDL_Surface *surface = SDL_CreateSurface(4, 4, SDL_PIXELFORMAT_RGBA32);
    assert(surface);
    memset(surface->pixels, 127, surface->pitch * surface->h);
    const char *png = "tex2d_72950631827607e2842812cc870dcae2_2x2x1_0x0.png";
    assert(SDL_SavePNG(surface, png));
    SDL_DestroySurface(surface);
    ui_window_init();
    if (getenv("SKYRECOMP_TEST_RESHADE")) {
        const float clear[4] = {0.2f, 0.4f, 0.6f, 1};
        for (int i = 0; i < 180; i++) {
            if (i == 60) g_settings.reshade = false;
            if (i == 120) g_settings.reshade = true;
            if (vk_frame_begin(clear)) vk_frame_end(NULL);
            SDL_Delay(16);
        }
    }
    static const FormatInfo fmt = {4, 1, 1, FMT_COLOR, "RGBA8"};
    NvnTexture texture = {.addr = (uintptr_t)data, .width = 2, .height = 2, .depth = 1, .target = 1,
        .format = 0x25, .flags = 0x10, .stride = 8, .levels = 1, .samples = 1,
        .layout = {.width = 2, .height = 2, .depth = 1, .layers = 1, .levels = 1, .fmt = &fmt}};
    tegra_layout_init(&texture.layout);
    g_settings.hd_textures = true;
    TexBackend *backend = vk_tex(&texture, false);
    assert(backend->custom && backend->width == 4 && backend->height == 4);
    // No new frame or guest writes: the settings change must bypass both upload caches.
    g_settings.hd_textures = false;
    assert(vk_tex(&texture, false) == backend);
    assert(!backend->custom && backend->width == 2 && backend->height == 2);
    g_settings.hd_textures = true;
    assert(vk_tex(&texture, false) == backend);
    assert(backend->custom && backend->width == 4 && backend->height == 4);
    renderer_submit();
    assert(remove(png) == 0);
    puts("Live HD texture toggle: PASS");
    return 0;
}
