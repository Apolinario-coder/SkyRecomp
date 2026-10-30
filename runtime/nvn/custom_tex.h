#pragma once
#include <SDL3/SDL.h>
#include "nvn.h"

void custom_tex_init(void);
int custom_tex_count(void);
// Caller owns the returned RGBA32 surface; NULL leaves the original texture intact.
SDL_Surface *custom_tex_load(const NvnTexture *t);
