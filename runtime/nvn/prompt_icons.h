// Button prompt icons drawn from scratch: runtime/nvn/texpatch.c puts them in place of the game's glyphs as those
// are uploaded, tools/test_prompt_icons.c previews every one. No font files: letters come from a built-in stroke font.
#pragma once
#include <stdbool.h>

enum { STYLE_NINTENDO, STYLE_XBOX, STYLE_PS, STYLE_KEYBOARD };
// What an icon shows: R_A..R_MINUS = the button at that Switch position (R_A east, R_B south, R_X north, R_Y west; a
// controller style names its own button there), or a stick (its click too), a d-pad direction (the d-pad with that
// arm lit), the whole d-pad, another button, or one d-pad button by itself (L_ARM_*: the HUD draws its d-pad as four
// of these, one per direction)
enum { R_A = 1, R_B, R_X, R_Y, R_L, R_R, R_ZL, R_ZR, R_PLUS, R_MINUS };
enum { L_LS = 32, L_RS, L_DUP, L_DDOWN, L_DLEFT, L_DRIGHT, L_OTHER, L_DPAD, L_ARM_UP, L_ARM_DOWN, L_ARM_LEFT, L_ARM_RIGHT };

// Draws `label` in `style` over px (w x h, premultiplied RGBA 0..1), fitted into the box x, y, bw x bh.
// Keyboard: keys = the bound SDL scancodes (512 + an SDL mouse button: a click): keys[0] for one key; up, left, down,
// right for L_LS / L_RS / L_DPAD (a stick or the d-pad: four keys). dark: dark buttons, else light ones like the
// game's own. mono: for intensity-only textures (no colours: the layout's colour tints those).
void prompt_icon(float *px, int w, int h, float x, float y, float bw, float bh, int style, int label, const int keys[4],
                 bool mono, bool dark);
