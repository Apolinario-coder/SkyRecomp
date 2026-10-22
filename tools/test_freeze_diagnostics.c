#define main skyrecomp_embedded_main
#include "../runtime/main.c"
#undef main
#include <assert.h>

int main(void) {
    diagnostic_progress_ns = 1000000000ull; diagnostic_report_ns = 0;
    assert(!diagnostic_stalled(15999999999ull));
    assert(diagnostic_stalled(16000000000ull));
    assert(!diagnostic_stalled(17000000000ull));
    assert(diagnostic_stalled(46000000000ull));
    diagnostic_progress_ns = 47000000000ull;
    assert(!diagnostic_stalled(48000000000ull));
    assert(diagnostic_stalled(62000000000ull));
    assert(!diagnostic_stalled(61000000000ull));
    pthread_mutex_lock(&g_klock);
    assert(!kernel_try_dump_threads());
    diagnostics_start(); assert(diagnostic_running);
    diagnostic_progress_ns = plat_now_ns() - 16000000000ull;
    uint64_t end = plat_now_ns() + 4000000000ull;
    while (!diagnostic_report_ns && plat_now_ns() < end) plat_sleep_until_ns(plat_now_ns() + 100000000ull);
    assert(diagnostic_report_ns);
    diagnostic_running = false;
    pthread_join(diagnostic_thread, NULL);
    pthread_mutex_unlock(&g_klock);
    puts("freeze diagnostics: automatic watchdog, reporting intervals, recovery and busy-lock handling passed");
}
