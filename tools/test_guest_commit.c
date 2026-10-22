#include <assert.h>
#include "../runtime/platform/windows.c"

int main(void) {
    void *p = VirtualAlloc(NULL, 131072, MEM_RESERVE, PAGE_READWRITE); assert(p);
    g_lo = (uintptr_t)p; g_hi = g_lo + 131072;
    assert(lazy_commit(g_lo));
    assert(lazy_commit(g_lo)); // page committed by a racing thread before its fault handler runs
    EXCEPTION_RECORD er = {.ExceptionCode = EXCEPTION_ACCESS_VIOLATION, .NumberParameters = 2};
    CONTEXT context = {0};
    EXCEPTION_POINTERS ep = {.ExceptionRecord = &er, .ContextRecord = &context};
    er.ExceptionInformation[1] = g_lo;
    er.ExceptionInformation[0] = 0;
    assert(vectored_handler(&ep) == EXCEPTION_CONTINUE_EXECUTION);
    er.ExceptionInformation[0] = 1;
    assert(vectored_handler(&ep) == EXCEPTION_CONTINUE_EXECUTION);
    er.ExceptionInformation[0] = 8;
    assert(vectored_handler(&ep) == EXCEPTION_CONTINUE_SEARCH);
    DWORD old;
    assert(VirtualProtect(p, 65536, PAGE_READONLY, &old));
    er.ExceptionInformation[0] = 1;
    assert(!lazy_commit(g_lo) && vectored_handler(&ep) == EXCEPTION_CONTINUE_SEARCH);
    assert(!lazy_commit(g_hi) && !lazy_commit(g_lo - 1));
    assert(VirtualFree(p, 0, MEM_RELEASE));
    puts("guest commit: first touch and racing commits recover; protected/outside/execute faults remain faults");
}
