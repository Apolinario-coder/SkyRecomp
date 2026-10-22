#include <assert.h>
#include <stdint.h>
static uint64_t test_time;
uint64_t test_now_ns(void) { return test_time; }
#define plat_now_ns test_now_ns
#include "../runtime/hle/input.c"
#undef plat_now_ns

int main(void) {
    uint64_t state[5] = {0}; uint32_t id = 0;
    Cpu c = {0}; c.x[0] = (uintptr_t)state; c.x[1] = (uintptr_t)&id;
    g_is_skyward = true; g_settings.free_cam = false;
    test_time = 400000000; script_buttons = 1;
    fill_state(&c); assert(state[0] == 101 && state[1] == 1);
    fill_state(&c); assert(state[0] == 101 && state[1] == 1);
    test_time += 3999999; fill_state(&c); assert(state[0] == 101 && state[1] == 1);
    test_time++; fill_state(&c); assert(state[0] == 102 && state[1] == 1);
    script_buttons = 0; test_time += 4000000;
    fill_state(&c); assert(state[0] == 103 && !state[1]);
    puts("HID sampling: steady clock preserves repeated samples and advances without a rendered frame");
}
