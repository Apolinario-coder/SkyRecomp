// Small services: parental control, add-on content, and other mostly-stubbed interfaces.
#include <string.h>

#include "../hle.h"
#include "../ipc.h"

static void ok_logger(Service *s, IpcReq *req, IpcResp *resp) {
    LOG("[svc] %s cmd %u (ok)\n", s->name, req->cmd);
    resp_size(resp, 0x10);
}

static uint32_t new_event_handle(void) {
    uint32_t w, r;
    kernel_create_event_pair(&w, &r);
    return r;
}

// ------------------------------------------------------------------ pctl
static void pctl_service(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    switch (req->cmd) {
    case 1031: case 1035: case 1018: OUT(uint8_t, 0) = 0; resp_size(resp, 1); break;  // restrictions off
    case 1032: OUT(uint32_t, 0) = 0; resp_size(resp, 4); break;
    default: LOG("[pctl] IParentalControlService cmd %u (ok)\n", req->cmd); resp_size(resp, 0x10);
    }
}

static void pctl_root(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    if (req->cmd == 0 || req->cmd == 1)
        ipc_return_object(resp, service_new("IParentalControlService", pctl_service, NULL));
}
SERVICE("pctl", pctl) { pctl_root(s, req, resp); }
SERVICE("pctl:s", pctl_s) { pctl_root(s, req, resp); }
SERVICE("pctl:a", pctl_a) { pctl_root(s, req, resp); }
SERVICE("pctl:r", pctl_r) { pctl_root(s, req, resp); }

// ------------------------------------------------------------------ aoc:u (no DLC)
SERVICE("aoc:u", aoc_u) {
    (void)s;
    switch (req->cmd) {
    case 2: OUT(uint32_t, 0) = 0; resp_size(resp, 4); break;   // CountAddOnContent
    case 3: OUT(uint32_t, 0) = 0; resp_size(resp, 4); break;   // ListAddOnContent
    case 5: OUT(uint64_t, 0) = 0x01006BB00C6F1000ull; resp_size(resp, 8); break;  // GetAddOnContentBaseId
    default: ok_logger(s, req, resp);
    }
}

// ------------------------------------------------------------------ ectx:aw & prepo:u stubs
SERVICE("ectx:aw", ectx_aw) { ok_logger(s, req, resp); }
SERVICE("prepo:u", prepo_u) { ok_logger(s, req, resp); }

// ------------------------------------------------------------------ standard memory / string
HLE_FUNC("memcpy", hle_memcpy) {
    if (c->x[2]) memcpy((void *)c->x[0], (const void *)c->x[1], (size_t)c->x[2]);
}

HLE_FUNC("__memcpy_device", hle_memcpy_dev) {
    if (c->x[2]) memcpy((void *)c->x[0], (const void *)c->x[1], (size_t)c->x[2]);
}

HLE_FUNC("memmove", hle_memmove) {
    if (c->x[2]) memmove((void *)c->x[0], (const void *)c->x[1], (size_t)c->x[2]);
}

HLE_FUNC("memset", hle_memset) {
    if (c->x[2]) memset((void *)c->x[0], (int)c->x[1], (size_t)c->x[2]);
}

HLE_FUNC("__memset_device", hle_memset_dev) {
    if (c->x[2]) memset((void *)c->x[0], (int)c->x[1], (size_t)c->x[2]);
}

HLE_FUNC("memcmp", hle_memcmp) {
    c->x[0] = c->x[2] ? (uint64_t)(int64_t)memcmp((const void *)c->x[0], (const void *)c->x[1], (size_t)c->x[2]) : 0;
}
