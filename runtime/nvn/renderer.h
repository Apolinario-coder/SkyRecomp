// Interface between the NVN executor (renderer.m) and the Metal plumbing (metal.m).
#pragma once
#ifdef __OBJC__
#import <Metal/Metal.h>
void renderer_init(id<MTLDevice> dev);
id<MTLCommandQueue> renderer_queue(void);
id<MTLTexture> renderer_texture(NvnTexture *t, id<MTLBlitCommandEncoder> blit, bool for_render);
MTLPixelFormat renderer_pixel_format(uint32_t nvn_format);
bool renderer_format_alpha_one(uint32_t nvn_format);
uint8_t renderer_format_remap(uint32_t nvn_format);  // REMAP_* below
extern id<MTLBuffer> g_guest_buffer;
extern uint64_t g_guest_buffer_base;
#endif
#include <SDL3/SDL_events.h>

bool renderer_is_gpu_owned(NvnTexture *t);
void renderer_flush(void);  // finish all recorded GPU work
bool renderer_origin_lower_left(void);
const char *renderer_stats(void);
bool renderer_format_is_depth(uint32_t f);
bool renderer_format_has_stencil(uint32_t f);

// A host image whose channels sit in another order than its NVN format's (the 16-bit formats: metal.m, vulkan/textures.c
// fmt_of), put back in the sampled view's swizzle. Channels as NVN swizzles: 0 zero, 1 one, 2..5 r,g,b,a (the
// numbering of MTLTextureSwizzle; VkComponentSwizzle - 1). c: the format's channel -> the host image's.
enum { REMAP_NONE = 0, REMAP_SWAP_RB = 1, REMAP_REVERSE = 2 };
static inline uint8_t renderer_remap_channel(uint8_t remap, uint8_t c) {
    if (c < 2 || c > 5) return c;
    int i = c - 2;
    if (remap == REMAP_SWAP_RB && (i == 0 || i == 2)) i = 2 - i;
    else if (remap == REMAP_REVERSE) i = 3 - i;
    return (uint8_t)(2 + i);
}
void renderer_mark_gpu_owned(NvnTexture *t);
void input_handle_event(const SDL_Event *e);
bool input_user_blocked(const SDL_Event *e);
