// The virtual Zelda & Loftwing amiibo (runtime/services/nfp.c) against a synthetic dump (never a real one): import
// checks, the copy in the data folder, the game's call sequence, and the ModelInfo bytes the game compares.
// clang -O1 -Iruntime tools/test_amiibo.c -o build/test_amiibo && build/test_amiibo
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include "../runtime/services/nfp.c"

Settings g_settings;
pthread_mutex_t g_klock = PTHREAD_MUTEX_INITIALIZER;
static KEvent events[64];
static int nevents;
KEvent *kernel_create_event_pair(uint32_t *w, uint32_t *r) {
    // (the real one takes g_klock to make the handles: called with it held, the game's thread deadlocked)
    assert(pthread_mutex_trylock(&g_klock) == 0);
    pthread_mutex_unlock(&g_klock);
    assert(nevents < 64);
    *w = 0x100 + nevents, *r = 0x200 + nevents;
    return &events[nevents++];
}
void kernel_signal(KObject *o) { o->signaled = true; }
Service *service_new(const char *name, ServiceHandler h, void *state) { (void)name; (void)h; (void)state; return NULL; }
void ipc_return_object(IpcResp *resp, Service *s) { (void)resp; (void)s; }

static uint8_t buf[0x100];
static IpcResp call(uint32_t cmd, uint64_t handle) {
    IpcReq req = {.cmd = cmd, .data = (const uint8_t *)&handle, .data_size = 8, .nc = 1};
    req.c[0] = (IpcBuf){(uintptr_t)buf, sizeof buf};
    IpcResp resp = {0};
    memset(buf, 0xAA, sizeof buf);
    nfp_user(NULL, &req, &resp);
    return resp;
}

static void write_file(const char *path, const uint8_t *d, size_t n) {
    FILE *f = fopen(path, "wb");
    assert(f && fwrite(d, 1, n, f) == n);
    fclose(f);
}

int main(void) {
    char dir[] = "/tmp/test_amiibo_XXXXXX", msg[160];
    assert(mkdtemp(dir) && chdir(dir) == 0);
    uint8_t d[540] = {0x04, 0x11, 0x22, 0x99, 0x33, 0x44, 0x55, 0x66};
    memcpy(d + 0x0C, (uint8_t[]){0xF1, 0x10, 0xFF, 0xEE}, 4);
    memcpy(d + 0x54, ZELDA_LOFTWING, 8);
    // import: wrong size, another figure, then the right one
    assert(!amiibo_loaded());
    write_file("short.bin", d, 100);
    assert(!amiibo_import("short.bin", msg, sizeof msg) && strstr(msg, "100 bytes"));
    uint8_t other[540];
    memcpy(other, d, sizeof d);
    other[0x56] = 0x00;  // (a different Zelda)
    write_file("other.bin", other, sizeof other);
    assert(!amiibo_import("other.bin", msg, sizeof msg) && strstr(msg, "isn't Zelda"));
    assert(!amiibo_import("missing.bin", msg, sizeof msg));
    write_file("mine.bin", d, sizeof d);
    assert(amiibo_import("mine.bin", msg, sizeof msg) && amiibo_loaded());
    assert(access("amiibo/zelda_loftwing.bin", R_OK) == 0);
    g_loaded = -1;  // (a later start: found in the data folder again)
    assert(amiibo_loaded());

    // the game's sequence, setting off: detection never finds a tag
    call(0, 0);
    IpcResp r = call(2, 0);
    assert(*(uint32_t *)r.data == NDEV && ((uint64_t *)buf)[8] == 0x20);
    r = call(21, 0x20);
    assert(*(uint32_t *)r.data == 0x20);
    r = call(17, 0x20);
    assert(r.ncopy == 1);
    KEvent *act = dev[8].act;
    call(3, 0x20);
    usleep(900 * 1000);
    assert(!act->signaled && dev[8].state == DEV_SEARCHING);
    call(4, 0x20);
    call(1, 0);

    // setting on: tapped about 0.7 s after StartDetection
    g_settings.amiibo = true;
    call(0, 0);
    call(17, 0x20);
    act = dev[8].act;
    call(3, 0x20);
    assert(!act->signaled);
    usleep(900 * 1000);
    assert(act->signaled && dev[8].state == DEV_TAG_FOUND);
    r = call(13, 0x20);  // GetTagInfo: the dump's 7-byte serial
    assert(!memcmp(buf, (uint8_t[]){0x04, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66}, 7) && buf[10] == 7);
    call(5, 0x20);
    assert(dev[8].state == DEV_TAG_MOUNTED);
    call(16, 0x20);  // GetModelInfo: the game compares bytes 0..2 with 01 01 03
    assert(!memcmp(buf, (uint8_t[]){0x01, 0x01, 0x03, 0x09, 0x14, 0x04, 0x00}, 7) && buf[7] == 0 && buf[0x3F] == 0);
    call(6, 0x20);
    call(4, 0x20);
    call(1, 0);

    // a StopDetection before the tap cancels it
    call(0, 0);
    call(17, 0x20);
    act = dev[8].act;
    call(3, 0x20);
    call(4, 0x20);
    usleep(900 * 1000);
    assert(!act->signaled);
    puts("amiibo: dump checks, data-folder copy, tap timing and ModelInfo passed");
}
