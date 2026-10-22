#include <assert.h>
#include <errno.h>
#include "../runtime/kernel.h"

static int failed_create(pthread_t *thread, const pthread_attr_t *attr, void *(*entry)(void *), void *arg) {
    (void)thread; (void)attr; (void)entry; (void)arg;
    return EAGAIN;
}
#define pthread_create failed_create
#include "../runtime/kernel.c"
#undef pthread_create

int main(void) {
    KThread t = {0}; t.obj.type = KOBJ_THREAD; t.handle = 0x101;
    assert(!thread_start(&t) && !t.started);
    handles[1] = &t.obj;
    Cpu c = {0}; c.x[0] = 0x101;
    svc_impl(&c, 9, 0);
    assert(c.x[0] == R_OUT_OF_RESOURCE && !t.started);
    puts("guest thread start: host creation failure is returned to the game, never a phantom running thread");
}
