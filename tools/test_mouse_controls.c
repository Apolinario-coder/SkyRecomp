#include <assert.h>
#include <stdio.h>
#include "../runtime/hle/input.c"

static uint64_t sample(uint64_t b, int32_t *x, int32_t *y, uint64_t now, bool captured) {
    bool sword = mouse_input(&b, x, y, now, captured);
    return free_camera_input(b, x, y, now, captured || sword);
}

static void click(int button, bool down) {
    SDL_Event e = {0};
    e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.button = button;
    input_handle_event(&e);
}

int main(void) {
    assert(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD));
    g_is_skyward = true; g_settings.free_cam = true; g_settings.mouse_controls = true;
    g_settings.mouse_sens = g_settings.free_cam_sens = 1;
    free_camera_reset(); input_reset(); mouse_captured = true;
    mouse_frame_ns = 1000000000;
    mouse_add_motion(&mouse_dx, 10); mouse_add_motion(&mouse_dy, -4);
    mouse_frame(1016666667);
    int32_t at60 = mouse_rx;
    assert(abs(at60 - 14000) <= 1 && mouse_ry > 5500);
    mouse_add_motion(&mouse_dx, 5); mouse_frame(1025000000);
    assert(abs(mouse_rx - at60) <= 2); // same physical speed at 120 FPS
    mouse_add_motion(&mouse_dx, NAN); mouse_add_motion(&mouse_dy, INFINITY);
    mouse_frame(1041666667); assert(mouse_rx == 0 && mouse_ry == 0);
    for (int i = 0; i < 100; i++) mouse_add_motion(&mouse_dx, 1e30f);
    assert(mouse_dx == 8388352); mouse_frame(1058333334); assert(mouse_rx == 32767);
    SDL_Event motion = {0}; motion.type = SDL_EVENT_MOUSE_MOTION; motion.motion.xrel = 10;
    mouse_skip_motion = true;
    input_handle_event(&motion); assert(!mouse_dx);
    input_handle_event(&motion); assert(mouse_dx == 2560); mouse_dx = 0;
    mouse_rx = 12000; mouse_ry = 0;
    int32_t x = 0, y = 0;
    uint64_t b = sample(0, &x, &y, 100, false);
    assert(x == 12000 && (b & (1ull << BTN_L)));
    click(SDL_BUTTON_LEFT, true);
    uint64_t until = mouse_slash_until;
    assert(until > 0);
    x = y = 0; b = sample(0, &x, &y, until - 90, false);
    assert(!x && !y && !(b & (1ull << BTN_L)));
    x = y = 0; b = sample(0, &x, &y, until - 50, false);
    assert(x == 32767 && !(b & (1ull << BTN_L)));
    click(SDL_BUTTON_LEFT, false);
    input_reset(); mouse_rx = 12000;
    click(SDL_BUTTON_RIGHT, true);
    x = y = 0; b = sample(0, &x, &y, SDL_GetTicks(), false);
    assert((b & (1ull << BTN_ZL)) && (b & (1ull << BTN_LSTICK)) && !x);
    click(SDL_BUTTON_RIGHT, false); input_reset();
    click(SDL_BUTTON_X1, true); click(SDL_BUTTON_LEFT, true);
    assert(!mouse_slash_until); mouse_rx = 9000;
    x = y = 0; b = sample(0, &x, &y, 100, false);
    assert((b & (1ull << BTN_ZR)) && !(b & (1ull << BTN_L)) && !(b & (1ull << BTN_A)) && x == 9000);
    click(SDL_BUTTON_LEFT, false);
    x = y = 0; b = sample(0, &x, &y, 101, false);
    assert(!(b & (1ull << BTN_ZR)) && !(b & (1ull << BTN_L)) && x == 9000);
    input_reset(); keys_down[SDL_SCANCODE_LALT] = 1;
    click(SDL_BUTTON_LEFT, true);
    x = y = 0; b = sample(0, &x, &y, 102, false);
    assert((b & (1ull << BTN_ZR)) && !mouse_slash_until);
    input_reset(); click(SDL_BUTTON_MIDDLE, true);
    x = y = 0; b = sample(0, &x, &y, 103, false);
    assert((b & (1ull << BTN_RSTICK)) && !(b & (1ull << BTN_L)));
    input_reset(); g_bind_key[BIND_A] = 512 + SDL_BUTTON_LEFT;
    click(SDL_BUTTON_LEFT, true); assert(key_buttons & (1ull << BTN_A)); assert(!mouse_slash_until);
    x = y = 0; b = sample(0, &x, &y, 104, true); assert(!b && !x && !y);
    input_reset(); assert(!mouse_btn_left && !mouse_slash_until && !mouse_rx);
    g_settings.mouse_controls = false; mouse_rx = 10000;
    x = y = 0; b = sample(0, &x, &y, 105, false); assert(!b && !x && !y);
    SDL_Window *window = SDL_CreateWindow("Mouse input regression", 320, 240, SDL_WINDOW_HIDDEN);
    assert(window);
    input_mouse_update(window, false);
    assert(!mouse_captured && !SDL_GetWindowRelativeMouseMode(window) && !mouse_rx);
    g_settings.mouse_controls = true;
    input_mouse_update(window, true);
    assert(!mouse_captured); // a hidden/unfocused window cannot trap the cursor
    SDL_DestroyWindow(window);
    SDL_Quit();
    puts("mouse controls: motion, 60/120 FPS, attack pulses, shield, thrust, item press/release, remapping and reset passed");
}
