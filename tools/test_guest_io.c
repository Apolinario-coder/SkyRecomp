#include <assert.h>
#include <windows.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <io.h>
#include "../runtime/platform.h"

bool rt_fault_watch(uint64_t addr, uint64_t pc) { return false; }
const char *rt_host_fn_name(uint64_t pc, uint64_t *offset) { return NULL; }
void rt_fault_report(const char *what, uint64_t addr, uint64_t pc, uint64_t lr) { abort(); }
void rt_fatal_trace(void) { abort(); }

int main(int argc, char **argv) {
    assert(argc == 2);
    uint64_t base = 0x7000000000ull;
    assert(plat_reserve_guest(base, base + (16 << 20)));
    MEMORY_BASIC_INFORMATION m;
    assert(VirtualQuery((void *)(uintptr_t)base, &m, sizeof m) && m.State == MEM_RESERVE);
    int fd = _open(argv[1], _O_CREAT | _O_TRUNC | _O_RDWR | _O_BINARY, _S_IREAD | _S_IWRITE);
    assert(fd >= 0);
    const char text[] = "guest file I/O";
    extern ssize_t pread(int, void *, size_t, off_t);
    extern ssize_t pwrite(int, const void *, size_t, off_t);
    assert(pwrite(fd, text, sizeof text, 0) == sizeof text);
    assert(pread(fd, (void *)(uintptr_t)base, sizeof text, 0) == sizeof text);
    assert(!memcmp((void *)(uintptr_t)base, text, sizeof text));
    *(volatile uint32_t *)(uintptr_t)(base + 0x10000) = 123;
    assert(*(volatile uint32_t *)(uintptr_t)(base + 0x10000) == 123);
    assert(pwrite(fd, (void *)(uintptr_t)(base + 0x20000), 16, 0) == 16);
    char zeros[16] = {0}, result[16];
    assert(pread(fd, result, sizeof result, 0) == sizeof result);
    assert(!memcmp(result, zeros, sizeof zeros));
    _close(fd);
    assert(remove(argv[1]) == 0);
    assert(plat_reset_guest(base, base + (16 << 20)));
    puts("lazy guest pages and Windows file I/O checks passed");
}
