// Runtime edits of a few of the game's UI textures, made on upload (runtime/nvn/texpatch.c): Xbox / PlayStation
// button prompts, the hidden Hero Mode icon, the tunic colour; also identifies textures for per-draw tints (gold
// Escape Bottle, heart colour). Shared by the Metal and Vulkan backends.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "nvn.h"

#define TEXPATCH_UNKNOWN (-2)  // (backend's cached id before the first identification)

// Which of the known textures `t` is (by its contents: the user's own game files are read once to know them), or -1.
int texpatch_identify(const NvnTexture *t);
// Known texture `id` is the empty bottle's item icon (1), its 3D model's colour map (2), the fairy's model map (3:
// boss / mini-boss souls drawn red, runtime/hle/souls.c), else 0 (the gold Escape Bottle: runtime/hle/escape.c).
int texpatch_bottle_kind(int id);
// Known texture `id` is one of the Items page's cursor brackets (hidden while the companion card has the cursor)
bool texpatch_is_item_cursor(int id);
// Changes whenever the wanted look of the known textures changes (prompt style, controller, Hero icon setting):
// backends re-upload identified textures when it differs from the value they uploaded with.
uint32_t texpatch_generation(void);
// Rewrites level `lv` of known texture `id` in place: `blocks` = its linear data in the texture's own format
// (rows of 4x4 blocks, `row` bytes apart; RGBA8 rows for uncompressed). No-op for the Nintendo look. Only the
// tunic (Cosmetics tab colour) changes levels other than 0.
void texpatch_apply(int id, uint32_t lv, uint8_t *blocks, uint32_t w, uint32_t h, uint32_t row);
// Known texture `id` is a heart of the HUD / file select (full, empty, frame, life-up flash): 1, else 0.
int texpatch_heart_kind(int id);
// The per-draw tint (renderer `ltint`) for a heart draw (`kind` = texpatch_heart_kind) in the Cosmetics tab's heart colour;
// left untouched for the game's own red.
void texpatch_heart_tint(int kind, float tint[8]);
