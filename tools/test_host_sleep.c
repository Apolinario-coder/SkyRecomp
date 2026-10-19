// Link the real platform implementation and exercise short waits and wakeups.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../runtime/platform.h"
#ifdef _WIN32
#include <windows.h>
#include <timeapi.h>
#include <pthread.h>
#endif

// The standalone timing test never installs or invokes the guest crash handlers.
bool rt_fault_watch(uint64_t addr, uint64_t pc) { abort(); }
const char *rt_host_fn_name(uint64_t pc, uint64_t *offset) { abort(); }
void rt_fault_report(const char *what, uint64_t addr, uint64_t pc, uint64_t lr) { abort(); }

#ifdef _WIN32
static void *signal_waiter(void *waiter) {
    plat_sleep_until_ns(plat_now_ns() + 2000000);
    plat_waiter_signal(waiter);
    return NULL;
}
#endif

int main(void) {
#ifdef _WIN32
    timeBeginPeriod(1);
#endif
    for (int ms = 1; ms <= 5; ms += 2) {
        const int count = 40;
        uint64_t start = plat_now_ns();
        for (int i = 0; i < count; i++) {
            uint64_t until = plat_now_ns() + (uint64_t)ms * 1000000;
            plat_sleep_until_ns(until);
            assert(plat_now_ns() >= until);
        }
        double average = (plat_now_ns() - start) / (1e6 * count);
        printf("%d ms guest sleep: %.3f ms average\n", ms, average);
        assert(average < ms + 3.0);
    }
#ifdef _WIN32
    void *waiter = plat_waiter_create();
    assert(waiter);
    uint64_t start = plat_now_ns();
    for (int i = 0; i < 40; i++) {
        plat_waiter_reset(waiter);
        uint64_t before = plat_now_ns();
        assert(!plat_waiter_wait(waiter, 3000000));
        assert(plat_now_ns() - before >= 3000000);
    }
    double average = (plat_now_ns() - start) / 40e6;
    printf("3 ms event timeout: %.3f ms average\n", average);
    assert(average < 6.0);
    // Signal between releasing the kernel lock and entering the wait.
    plat_waiter_reset(waiter);
    plat_waiter_signal(waiter);
    assert(plat_waiter_wait(waiter, 1000000000));
    // Wake an indefinite and a finite wait; a cancelled timer must not leak
    // into the next wait on this thread.
    for (int i = 0; i < 40; i++) {
        pthread_t thread;
        plat_waiter_reset(waiter);
        assert(!pthread_create(&thread, NULL, signal_waiter, waiter));
        assert(plat_waiter_wait(waiter, i & 1 ? -1 : 1000000000));
        pthread_join(thread, NULL);
        assert(!plat_waiter_wait(waiter, 0));
    }
    plat_waiter_destroy(waiter);
    puts("event signal, timeout, reuse and indefinite waits passed");
#endif
}
