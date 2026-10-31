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
    const int32_t circle[][2] = {{32767, 32767}, {-32768, 32767}, {32767, -20000}, {0, 32767}, {23170, 23170}};
    for (unsigned i = 0; i < sizeof circle / sizeof circle[0]; i++) {
        int32_t x = circle[i][0], y = circle[i][1];
        input_stick_circle(&x, &y);
        long long d2 = (long long)x * x + (long long)y * y;
        assert(d2 <= 32767ll * 32767 && d2 > 32700ll * 32700);  // (on the rim, same direction)
        assert((x > 0) == (circle[i][0] > 0) && (y > 0) == (circle[i][1] > 0));
    }
}
