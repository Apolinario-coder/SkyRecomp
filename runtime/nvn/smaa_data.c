// SMAA 1x (Jimenez et al., MIT: third_party/smaa/LICENSE.txt) for the Video tab's "Anti-aliasing": the reference
// shader source (built into a C string by the Makefile) and its two precomputed lookup textures, shared by the Vulkan
// passes (runtime/nvn/vulkan/shaders.c, exec.c) and the Metal ones (runtime/nvn/metal.m).
#include "AreaTex.h"    // 160 x 560, R8G8
#include "SearchTex.h"  // 64 x 16, R8

const char smaa_hlsl_src[] =
#include "smaa_hlsl.inc"
    ;

const unsigned char *smaa_area_tex(void) { return areaTexBytes; }      // AREATEX_WIDTH x AREATEX_HEIGHT, 2 bytes each
const unsigned char *smaa_search_tex(void) { return searchTexBytes; }  // SEARCHTEX_WIDTH x SEARCHTEX_HEIGHT, 1 byte
