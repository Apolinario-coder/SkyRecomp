#include <assert.h>
#include <stdio.h>
#include "../runtime/nvn/cmdbuf.c"

int main(void) {
    unsigned char memory[4096];
    NvnCmdBuf cb = {.mem = (uintptr_t)memory, .size = sizeof memory};
    uint64_t handle = (uintptr_t)&cb;
    uint32_t color[4] = {1, 2, 3, 4};
    void (*calls[])(Cpu *) = {hle_nvnCommandBufferClearColor, hle_nvnCommandBufferClearColori, hle_nvnCommandBufferClearColorui};
    for (int kind = 0; kind < 3; kind++) {
        cb.used = 0;
        Cpu c = {0}; c.x[0] = (uintptr_t)&handle; c.x[2] = (uintptr_t)color;
        c.x[6] = UINT64_MAX;
        calls[kind](&c);
        CmdRec *r = (CmdRec *)memory;
        assert(r->op == CMD_CLEAR_COLOR && r->x[5] == (uint64_t)kind);
        assert(!memcmp(r->extra, color, sizeof color));
    }
    puts("clear color: float, signed and unsigned kinds ignore stale guest registers; payload preserved");
}
