// NVN entry points: every NVN function is looked up by name and bound to a host stub.
#include <string.h>

#include "../hle.h"

static uint64_t nvn_proc(const char *name) {
    if (!name || !*name) return 0;
    return hle_register(name, hle_lookup_impl(name));
}

HLE_FUNC("nvnBootstrapLoader", nvn_bootstrap_loader) {
    const char *name = (const char *)(uintptr_t)c->x[0];
    c->x[0] = nvn_proc(name);
}

// void* nvnDeviceGetProcAddress(const NVNdevice*, const char*)
HLE_FUNC("nvnDeviceGetProcAddress", nvn_device_get_proc_address) {
    const char *name = (const char *)(uintptr_t)c->x[1];
    c->x[0] = nvn_proc(name);
}
