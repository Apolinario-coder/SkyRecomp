// amiibo (nfp:user): a virtual Zelda & Loftwing amiibo for Fi's "Use amiibo", Skyward Sword HD's only amiibo (it takes
// Link from the surface to the sky and back to the same spot). The player loads their own figure's dump once
// (Settings > Video, amiibo_import); we keep a copy and only ever read its plain bytes - the figure ID and the tag's
// serial - never its encrypted data. With the setting on, the tag is "tapped" a moment after the game starts looking
// for one; off (or no dump), detection never finds a tag, as on a Switch with no figure.
//
// The game's side (main 0x1056fe0.. state machine, Fi's amiibo page 0xdb3800): Initialize, ListDevices, per device
// GetNpadId + the activate / deactivate events, StartDetection on the device of its active controller, then on the
// activate event GetTagInfo, Mount, GetModelInfo, Unmount, StopDetection, Finalize - every attempt from Initialize on.
// It recognises the figure by ModelInfo bytes 0..2 (01 01 03) and never uses the application area. Command numbers and
// structures as Ryujinx's INfp / VirtualAmiibo.
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../hle.h"
#include "../ipc.h"
#include "../settings.h"

#define NDEV 9
static const uint32_t npad_ids[NDEV] = {0, 1, 2, 3, 4, 5, 6, 7, 0x20};  // every controller: the game picks its own
enum { DEV_INITIALIZED = 0, DEV_SEARCHING = 1, DEV_TAG_FOUND = 2, DEV_TAG_MOUNTED = 4 };

static struct { KEvent *act, *deact; uint32_t state; } dev[NDEV];
static bool initialized;
static uint32_t gen;  // bumped by Initialize / Finalize / StopDetection: a pending tap from before is dropped

// ------------------------------------------------------------------ the player's dump
#define AMIIBO_PATH "amiibo/zelda_loftwing.bin"
static const uint8_t ZELDA_LOFTWING[8] = {0x01, 0x01, 0x03, 0x00, 0x04, 0x14, 0x09, 0x02};
static uint8_t g_id[8], g_uid[7];
static int g_loaded = -1;  // -1: not checked yet

// An NTAG215 amiibo image (532, 540 or 572 bytes): capability container F1 10 FF EE at 0x0C, the figure ID at 0x54
// (character, variant, type, model number, series, 02 - stored in the clear), the 7-byte serial in bytes 0-2, 4-7.
static bool dump_ok(const uint8_t *d, long n) {
    static const uint8_t cc[4] = {0xF1, 0x10, 0xFF, 0xEE};
    return (n == 532 || n == 540 || n == 572) && !memcmp(d + 0x0C, cc, 4) && !memcmp(d + 0x54, ZELDA_LOFTWING, 8);
}

static long read_file(const char *path, uint8_t *buf, long cap) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    long n = (long)fread(buf, 1, (size_t)cap, f);
    if (fgetc(f) != EOF) n = cap + 1;  // (too big)
    fclose(f);
    return n;
}

static void take(const uint8_t *d) {
    memcpy(g_id, d + 0x54, 8);
    memcpy(g_uid, d, 3);
    memcpy(g_uid + 3, d + 4, 4);
    g_loaded = 1;
}

bool amiibo_loaded(void) {
    if (g_loaded < 0) {
        uint8_t d[600];
        long n = read_file(AMIIBO_PATH, d, sizeof d);
        g_loaded = 0;
        if (dump_ok(d, n)) take(d);
    }
    return g_loaded == 1;
}

// Settings > Video > "Load amiibo dump": check the player's file and keep a copy under the data folder. msg: what to
// tell them.
bool amiibo_import(const char *path, char *msg, size_t cap) {
    uint8_t d[600];
    long n = read_file(path, d, sizeof d);
    if (n < 0) { snprintf(msg, cap, "Couldn't open that file."); return false; }
    if (n != 532 && n != 540 && n != 572) {
        snprintf(msg, cap, "That isn't an amiibo dump (%ld bytes; a dump is 532, 540 or 572).", n);
        return false;
    }
    if (!dump_ok(d, n)) {
        snprintf(msg, cap, "That amiibo isn't Zelda & Loftwing (figure %02X%02X%02X%02X %02X%02X%02X%02X).", d[0x54], d[0x55],
                 d[0x56], d[0x57], d[0x58], d[0x59], d[0x5A], d[0x5B]);
        return false;
    }
    mkdir("amiibo", 0755);
    FILE *f = fopen(AMIIBO_PATH ".tmp", "wb");
    bool ok = f && fwrite(d, 1, (size_t)n, f) == (size_t)n;
    if (f) fclose(f);
    if (!ok || rename(AMIIBO_PATH ".tmp", AMIIBO_PATH) != 0) {
        remove(AMIIBO_PATH ".tmp");
        snprintf(msg, cap, "Couldn't save a copy in the data folder.");
        return false;
    }
    take(d);
    snprintf(msg, cap, "Zelda & Loftwing loaded.");
    return true;
}

// ------------------------------------------------------------------ nfp:user
static int dev_of(uint64_t handle) {
    for (int i = 0; i < NDEV; i++) if (npad_ids[i] == (uint32_t)handle) return i;
    return -1;
}

static uint32_t new_event(KEvent **ev) {
    uint32_t w, r;
    *ev = kernel_create_event_pair(&w, &r);
    return r;
}

// The tap: a moment after detection starts (Fi's "please tap an amiibo accessory" shows first), if nothing has
// stopped it since
typedef struct { int d; uint32_t gen; } Tap;
static void *tap_later(void *arg) {
    Tap t = *(Tap *)arg;
    free(arg);
    usleep(700 * 1000);
    pthread_mutex_lock(&g_klock);
    if (t.gen == gen && dev[t.d].state == DEV_SEARCHING && dev[t.d].act) {
        dev[t.d].state = DEV_TAG_FOUND;
        kernel_signal(dev[t.d].act);
    }
    pthread_mutex_unlock(&g_klock);
    return NULL;
}

static void put_out(IpcReq *req, const void *src, uint64_t n) {
    IpcBuf *b = out_buf(req, 0);
    if (b && b->addr) memcpy((void *)(uintptr_t)b->addr, src, b->size && b->size < n ? b->size : n);
}

static void nfp_user(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    int d = req->data_size >= 8 ? dev_of(IN(uint64_t, 0)) : -1;
    switch (req->cmd) {
    case 0:  // Initialize(aruid, mcu version, version data)
    case 1:  // Finalize
        pthread_mutex_lock(&g_klock);
        initialized = req->cmd == 0;
        gen++;
        for (int i = 0; i < NDEV; i++) dev[i].state = DEV_INITIALIZED;
        pthread_mutex_unlock(&g_klock);
        break;
    case 2: {  // ListDevices -> DeviceHandle[], count
        IpcBuf *b = out_buf(req, 0);
        uint32_t n = b && b->size ? (uint32_t)(b->size / 8) : NDEV;
        if (n > NDEV) n = NDEV;
        for (uint32_t i = 0; b && b->addr && i < n; i++) ((uint64_t *)(uintptr_t)b->addr)[i] = npad_ids[i];
        OUT(uint32_t, 0) = n;
        resp_size(resp, 4);
        break;
    }
    case 3: {  // StartDetection(handle)
        if (d < 0) break;
        pthread_mutex_lock(&g_klock);
        dev[d].state = DEV_SEARCHING;
        uint32_t g = gen;
        pthread_mutex_unlock(&g_klock);
        if (g_settings.amiibo && amiibo_loaded()) {
            Tap *t = malloc(sizeof *t);
            t->d = d;
            t->gen = g;
            pthread_t th;
            if (pthread_create(&th, NULL, tap_later, t) == 0) pthread_detach(th);
            else free(t);
        }
        break;
    }
    case 4:  // StopDetection(handle)
        if (d < 0) break;
        pthread_mutex_lock(&g_klock);
        gen++;
        dev[d].state = DEV_INITIALIZED;
        pthread_mutex_unlock(&g_klock);
        break;
    case 5:  // Mount(handle, model type, mount target)
        if (d >= 0 && dev[d].state == DEV_TAG_FOUND) dev[d].state = DEV_TAG_MOUNTED;
        break;
    case 6:  // Unmount(handle)
        if (d >= 0 && dev[d].state == DEV_TAG_MOUNTED) dev[d].state = DEV_TAG_FOUND;
        break;
    case 13: {  // GetTagInfo(handle) -> TagInfo: uuid[10], uuid length, 21 reserved, protocol, tag type, 48 reserved
        uint8_t ti[0x58] = {0};
        memcpy(ti, g_uid, 7);
        ti[10] = 7;
        ti[0x20] = 1;  // protocol: type A
        ti[0x24] = 2;  // tag type: type 2
        put_out(req, ti, sizeof ti);
        break;
    }
    case 14: {  // GetRegisterInfo(handle): owner Mii, nickname (unused by the game)
        static const uint8_t ri[0x100];
        put_out(req, ri, sizeof ri);
        break;
    }
    case 15: {  // GetCommonInfo(handle): write date / counter / version, application area size (unused by the game)
        uint8_t ci[0x40] = {0};
        ci[0x0C] = 0xD8;  // application area size
        put_out(req, ci, sizeof ci);
        break;
    }
    case 16: {  // GetModelInfo(handle) -> character id (big-endian), variant, series, model number, figure type
        uint8_t mi[0x40] = {0};
        mi[0] = g_id[0], mi[1] = g_id[1], mi[2] = g_id[2];
        mi[3] = g_id[6];
        mi[4] = g_id[5], mi[5] = g_id[4];
        mi[6] = g_id[3];
        put_out(req, mi, sizeof mi);
        break;
    }
    case 17:    // AttachActivateEvent(handle) -> event (a new one per attach: the game closes it at Finalize)
    case 18: {  // AttachDeactivateEvent(handle)
        KEvent *ev;
        resp->copy_handles[resp->ncopy++] = new_event(&ev);  // (not under g_klock: making handles takes it)
        if (d < 0) break;
        pthread_mutex_lock(&g_klock);
        *(req->cmd == 17 ? &dev[d].act : &dev[d].deact) = ev;
        pthread_mutex_unlock(&g_klock);
        break;
    }
    case 19:  // GetState
        OUT(uint32_t, 0) = initialized;
        resp_size(resp, 4);
        break;
    case 20:  // GetDeviceState(handle)
        OUT(uint32_t, 0) = d >= 0 ? dev[d].state : DEV_INITIALIZED;
        resp_size(resp, 4);
        break;
    case 21:  // GetNpadId(handle)
        OUT(uint32_t, 0) = d >= 0 ? npad_ids[d] : 0;
        resp_size(resp, 4);
        break;
    case 22:  // GetApplicationAreaSize
        OUT(uint32_t, 0) = 0xD8;
        resp_size(resp, 4);
        break;
    case 23: {  // AttachAvailabilityChangeEvent (never signalled: the reader is always there)
        KEvent *ev;
        resp->copy_handles[resp->ncopy++] = new_event(&ev);
        break;
    }
    default:  // application area (7-12, 24): the game never uses it
        LOG("[nfp] IUser cmd %u (unhandled, ok)\n", req->cmd);
    }
}

SERVICE("nfp:user", nfp_user_manager) {
    (void)s;
    if (req->cmd == 0) ipc_return_object(resp, service_new("nfp:IUser", nfp_user, NULL));  // CreateUserInterface
}

RT_ON_RESET(nfp_reset) {  // (reboot: guest handles and events are gone)
    memset(dev, 0, sizeof dev);
    initialized = false;
    gen++;
}
