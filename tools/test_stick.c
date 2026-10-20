#include <assert.h>
#include "../runtime/hle/stick.h"

int main(void) {
    const int32_t rest[][2] = {{0, 0}, {400, -600}, {-1000, 2000}, {8000, 0}, {0, -8000}};
    for (unsigned i = 0; i < sizeof rest / sizeof rest[0]; i++) {
        int32_t x = rest[i][0], y = rest[i][1];
        input_stick_deadzone(&x, &y);
        assert(x == 0 && y == 0);
    }
    const int32_t move[][2] = {{8001, 0}, {6000, 6000}, {32767, -32768}, {-32768, 32768}};
    for (unsigned i = 0; i < sizeof move / sizeof move[0]; i++) {
        int32_t x = move[i][0], y = move[i][1];
        input_stick_deadzone(&x, &y);
        assert(x == move[i][0] && y == move[i][1]);
    }
}
