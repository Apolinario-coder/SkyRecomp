// Performance overlay numbers (runtime/perfmon.h). Frame times come from the game's presents; the machine's numbers
// are sampled twice a second on a thread of their own (some of the calls take milliseconds), only once the overlay
// asks for them. Libraries that only some machines have are loaded at run time: NVML (NVIDIA's driver library, for
// the GPU) and, on Windows, PDH (the thermal zone and GPU engine counters). A missing one leaves its numbers at -1.
// Adapted from Savage Wind Recomp's runtime/perfmon.c (MIT License, Copyright (c) 2026 Revivedx and the Savage Wind
// Recomp contributors); macOS, the Windows GPU engine counter, the 1% low and the renderer's own GPU memory are ours.
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "perfmon.h"
#include "platform.h"
#include "rt.h"

#ifdef _WIN32
#include <windows.h>
#define PSAPI_VERSION 2
#include <psapi.h>
#include <pdh.h>
#else
#include <dirent.h>
#include <dlfcn.h>
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#endif
#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static PerfStats g_stats;
static char g_gpu_hint[96];
static float g_vram_game_mb = -1, g_vram_dev_mb = -1;  // perfmon_set_gpu_mem (the renderer)

// ------------------------------------------------------------------ frames
static uint64_t g_last_frame_ns;
static float g_ring[PERF_HISTORY];
static uint64_t g_ring_at[PERF_HISTORY];  // when each frame ended
static int g_ring_n, g_ring_pos;

void perfmon_frame(void) {
    uint64_t now = plat_now_ns();
    pthread_mutex_lock(&g_lock);
    if (g_last_frame_ns) {
        g_ring[g_ring_pos] = (float)((now - g_last_frame_ns) / 1e6);
        g_ring_at[g_ring_pos] = now;
        g_ring_pos = (g_ring_pos + 1) % PERF_HISTORY;
        if (g_ring_n < PERF_HISTORY) g_ring_n++;
    }
    g_last_frame_ns = now;
    pthread_mutex_unlock(&g_lock);
}

static int cmp_float(const void *a, const void *b) {
    float x = *(const float *)a, y = *(const float *)b;
    return x < y ? -1 : x > y;
}

static void frames_summary(PerfStats *s) {  // (lock held)
    uint64_t now = plat_now_ns();
    double sum = 0, mx = 0;
    float last[PERF_HISTORY];
    int n = 0;
    for (int i = 0; i < g_ring_n; i++) {
        int k = (g_ring_pos - 1 - i + PERF_HISTORY) % PERF_HISTORY;
        if (now - g_ring_at[k] > 1000000000ull) break;  // the last second
        sum += g_ring[k];
        if (g_ring[k] > mx) mx = g_ring[k];
        last[n++] = g_ring[k];
    }
    s->fps = n && sum > 0 ? (float)(n * 1000.0 / sum) : 0;
    s->frame_ms_avg = n ? (float)(sum / n) : 0;
    s->frame_ms_max = (float)mx;
    qsort(last, (size_t)n, sizeof *last, cmp_float);
    s->frame_ms_low1 = n ? last[(int)ceil(n * 0.99) - 1] : 0;
    for (int i = 0; i < PERF_HISTORY; i++) {  // oldest first; frames not seen yet read 0
        int age = PERF_HISTORY - 1 - i;
        s->frame_ms[i] = age < g_ring_n ? g_ring[(g_ring_pos - 1 - age + 2 * PERF_HISTORY) % PERF_HISTORY] : 0;
    }
}

// ------------------------------------------------------------------ CPU name
static void cpu_name(char *out, size_t cap) {
    out[0] = 0;
#if defined(__APPLE__)
    size_t sz = cap;
    if (sysctlbyname("machdep.cpu.brand_string", out, &sz, NULL, 0) != 0) snprintf(out, cap, "CPU");
#elif defined(__x86_64__) || defined(__i386__)
    unsigned r[12];
    if (__get_cpuid_max(0x80000000, NULL) >= 0x80000004) {
        for (unsigned i = 0; i < 3; i++) __get_cpuid(0x80000002 + i, &r[i * 4], &r[i * 4 + 1], &r[i * 4 + 2], &r[i * 4 + 3]);
        char b[49];
        memcpy(b, r, 48);
        b[48] = 0;
        char *p = b;
        while (*p == ' ') p++;
        snprintf(out, cap, "%s", p);
        for (char *e = out + strlen(out); e > out && e[-1] == ' '; e--) e[-1] = 0;
    }
#else
    snprintf(out, cap, "CPU");
#endif
}

// ------------------------------------------------------------------ NVML (NVIDIA)
typedef struct { unsigned gpu, memory; } NvmlUtil;
typedef struct { unsigned long long total, free, used; } NvmlMem;
static struct {
    bool tried, ok;
    void *dev;
    int (*init)(void);
    int (*count)(unsigned *);
    int (*handle)(unsigned, void **);
    int (*name)(void *, char *, unsigned);
    int (*util)(void *, NvmlUtil *);
    int (*temp)(void *, int, unsigned *);
    int (*mem)(void *, NvmlMem *);
    int (*power)(void *, unsigned *);
    int (*clock)(void *, int, unsigned *);
} nv;

static void *lib_open(const char *name) {
#ifdef _WIN32
    return (void *)LoadLibraryA(name);
#else
    return dlopen(name, RTLD_NOW | RTLD_LOCAL);
#endif
}
static void *lib_sym(void *lib, const char *name) {
#ifdef _WIN32
    return (void *)GetProcAddress((HMODULE)lib, name);
#else
    return dlsym(lib, name);
#endif
}

static void nvml_open(PerfStats *s) {
    nv.tried = true;
#if defined(_WIN32)
    void *lib = lib_open("nvml.dll");
    if (!lib) lib = lib_open("C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvml.dll");
#elif defined(__APPLE__)
    void *lib = NULL;  // (no NVIDIA driver on current macOS)
#else
    void *lib = lib_open("libnvidia-ml.so.1");
#endif
    if (!lib) return;
    nv.init = lib_sym(lib, "nvmlInit_v2");
    nv.count = lib_sym(lib, "nvmlDeviceGetCount_v2");
    nv.handle = lib_sym(lib, "nvmlDeviceGetHandleByIndex_v2");
    nv.name = lib_sym(lib, "nvmlDeviceGetName");
    nv.util = lib_sym(lib, "nvmlDeviceGetUtilizationRates");
    nv.temp = lib_sym(lib, "nvmlDeviceGetTemperature");
    nv.mem = lib_sym(lib, "nvmlDeviceGetMemoryInfo");
    nv.power = lib_sym(lib, "nvmlDeviceGetPowerUsage");
    nv.clock = lib_sym(lib, "nvmlDeviceGetClockInfo");
    if (!nv.init || !nv.count || !nv.handle || nv.init() != 0) return;
    unsigned n = 0;
    if (nv.count(&n) != 0 || !n) return;
    for (unsigned i = 0; i < n; i++) {  // the one the renderer uses, else the first
        void *d = NULL;
        char nm[96] = "";
        if (nv.handle(i, &d) != 0) continue;
        if (nv.name) nv.name(d, nm, sizeof nm);
        if (!nv.dev || (g_gpu_hint[0] && strstr(g_gpu_hint, nm) && nm[0])) {
            nv.dev = d;
            snprintf(s->gpu_name, sizeof s->gpu_name, "%s", nm);
        }
    }
    nv.ok = nv.dev != NULL;
    LOG("[perf] NVML: %s\n", nv.ok ? s->gpu_name : "no device");
}

static void nvml_sample(PerfStats *s) {
    if (!nv.tried) nvml_open(s);
    if (!nv.ok) return;
    NvmlUtil u;
    NvmlMem m;
    unsigned v;
    if (nv.util && nv.util(nv.dev, &u) == 0) s->gpu_pct = (float)u.gpu;
    if (nv.temp && nv.temp(nv.dev, 0 /* NVML_TEMPERATURE_GPU */, &v) == 0) s->gpu_temp_c = (float)v;
    if (nv.mem && nv.mem(nv.dev, &m) == 0) s->vram_used_mb = (float)(m.used >> 20), s->vram_total_mb = (float)(m.total >> 20);
    if (nv.power && nv.power(nv.dev, &v) == 0) s->gpu_power_w = v / 1000.0f;
    if (nv.clock && nv.clock(nv.dev, 0 /* NVML_CLOCK_GRAPHICS */, &v) == 0) s->gpu_clock_mhz = (float)v;
}

// ------------------------------------------------------------------ platform: CPU, memory, temperatures
#if defined(_WIN32)
static uint64_t ft64(FILETIME f) { return (uint64_t)f.dwHighDateTime << 32 | f.dwLowDateTime; }

typedef LONG (WINAPI *PdhOpenQueryW_t)(LPCWSTR, DWORD_PTR, PDH_HQUERY *);
typedef LONG (WINAPI *PdhAddEnglishCounterW_t)(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER *);
typedef LONG (WINAPI *PdhCollectQueryData_t)(PDH_HQUERY);
typedef LONG (WINAPI *PdhGetFormattedCounterArrayW_t)(PDH_HCOUNTER, DWORD, LPDWORD, LPDWORD, PPDH_FMT_COUNTERVALUE_ITEM_W);
static struct {
    bool tried, ok, zone_ok, gpu_ok;
    PDH_HQUERY q;
    PDH_HCOUNTER zone, gpu;
    PdhCollectQueryData_t collect;
    PdhGetFormattedCounterArrayW_t get;
} pdh;

static void pdh_open(void) {
    pdh.tried = true;
    HMODULE l = LoadLibraryA("pdh.dll");
    PdhOpenQueryW_t open = l ? (PdhOpenQueryW_t)(void *)GetProcAddress(l, "PdhOpenQueryW") : NULL;
    PdhAddEnglishCounterW_t add = l ? (PdhAddEnglishCounterW_t)(void *)GetProcAddress(l, "PdhAddEnglishCounterW") : NULL;
    pdh.collect = l ? (PdhCollectQueryData_t)(void *)GetProcAddress(l, "PdhCollectQueryData") : NULL;
    pdh.get = l ? (PdhGetFormattedCounterArrayW_t)(void *)GetProcAddress(l, "PdhGetFormattedCounterArrayW") : NULL;
    pdh.ok = open && add && pdh.collect && pdh.get && open(NULL, 0, &pdh.q) == 0;
    if (!pdh.ok) return;
    // The ACPI thermal zones (no driver needed). They are the firmware's, near the CPU on most boards; an exact CPU
    // package temperature on Windows needs a kernel driver (what monitoring tools install), which we don't.
    pdh.zone_ok = add(pdh.q, L"\\Thermal Zone Information(*)\\Temperature", 0, &pdh.zone) == 0;
    // Task Manager's GPU "3D" load: the GPU engine counters, any vendor (NVML is used instead when there)
    pdh.gpu_ok = add(pdh.q, L"\\GPU Engine(*engtype_3D)\\Utilization Percentage", 0, &pdh.gpu) == 0;
}

static PDH_FMT_COUNTERVALUE_ITEM_W *pdh_items(PDH_HCOUNTER c, DWORD *count) {  // (free() the result)
    DWORD size = 0;
    *count = 0;
    pdh.get(c, PDH_FMT_DOUBLE, &size, count, NULL);
    if (!size) return NULL;
    PDH_FMT_COUNTERVALUE_ITEM_W *items = malloc(size);
    if (items && pdh.get(c, PDH_FMT_DOUBLE, &size, count, items) != 0) free(items), items = NULL, *count = 0;
    return items;
}

static void pdh_sample(PerfStats *s) {
    if (!pdh.tried) pdh_open();
    if (!pdh.ok || pdh.collect(pdh.q) != 0) return;
    DWORD n;
    PDH_FMT_COUNTERVALUE_ITEM_W *it;
    if (pdh.zone_ok && (it = pdh_items(pdh.zone, &n))) {
        float best = -1;
        for (DWORD i = 0; i < n; i++) {  // the hottest zone, in kelvin
            double c = it[i].FmtValue.doubleValue - 273.15;
            if (c > best && c < 150) best = (float)c;
        }
        s->cpu_temp_c = best;
        free(it);
    }
    // each process's use of the 3D engine, summed (a rate counter: valid from the second sample on)
    // ponytail: summed over adapters too (an iGPU + dGPU machine adds both); split by the instance's luid if it matters
    if (pdh.gpu_ok && (it = pdh_items(pdh.gpu, &n))) {
        double sum = 0;
        for (DWORD i = 0; i < n; i++)
            if (it[i].FmtValue.CStatus <= 1)  // PDH_CSTATUS_VALID_DATA / NEW_DATA (pdhmsg.h)
                sum += it[i].FmtValue.doubleValue;
        s->gpu_pct = (float)(sum > 100 ? 100 : sum);
        free(it);
    }
}

static void platform_sample(PerfStats *s, double dt) {
    static uint64_t p_idle, p_kern, p_user, p_proc;
    FILETIME idle, kern, user, cr, ex, pk, pu;
    if (GetSystemTimes(&idle, &kern, &user) && GetProcessTimes(GetCurrentProcess(), &cr, &ex, &pk, &pu)) {
        uint64_t i = ft64(idle), k = ft64(kern), u = ft64(user), p = ft64(pk) + ft64(pu);
        uint64_t total = (k - p_kern) + (u - p_user);  // (kernel time includes idle)
        if (p_kern && total) {
            s->cpu_total_pct = 100.0f * (float)(total - (i - p_idle)) / (float)total;
            s->cpu_game_pct = 100.0f * (float)(p - p_proc) / (float)total;
        }
        p_idle = i, p_kern = k, p_user = u, p_proc = p;
    }
    (void)dt;
    MEMORYSTATUSEX ms = {.dwLength = sizeof ms};
    if (GlobalMemoryStatusEx(&ms)) {
        s->ram_total_mb = (float)(ms.ullTotalPhys >> 20);
        s->ram_used_mb = (float)((ms.ullTotalPhys - ms.ullAvailPhys) >> 20);
    }
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc)) s->ram_game_mb = (float)(pmc.WorkingSetSize >> 20);
    pdh_sample(s);
    s->cpu_temp_source = "ACPI thermal zone";
}
#elif defined(__APPLE__)
static void platform_sample(PerfStats *s, double dt) {
    (void)dt;
    // CPU: the machine's ticks, and this process's user + system time over the wall time of all logical cores
    static uint64_t p_busy, p_total, p_proc_us, p_wall_ns;
    static int ncpu;
    if (!ncpu) {
        size_t sz = sizeof ncpu;
        if (sysctlbyname("hw.logicalcpu", &ncpu, &sz, NULL, 0) != 0 || ncpu < 1) ncpu = 1;
    }
    host_cpu_load_info_data_t li;
    mach_msg_type_number_t n = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, (host_info_t)&li, &n) == KERN_SUCCESS) {
        uint64_t busy = (uint64_t)li.cpu_ticks[CPU_STATE_USER] + li.cpu_ticks[CPU_STATE_SYSTEM] + li.cpu_ticks[CPU_STATE_NICE];
        uint64_t total = busy + li.cpu_ticks[CPU_STATE_IDLE];
        if (p_total && total > p_total) s->cpu_total_pct = 100.0f * (float)(busy - p_busy) / (float)(total - p_total);
        p_busy = busy, p_total = total;
    }
    struct rusage ru;
    uint64_t wall = plat_now_ns();
    if (getrusage(RUSAGE_SELF, &ru) == 0) {
        uint64_t us = (uint64_t)ru.ru_utime.tv_sec * 1000000 + (uint64_t)ru.ru_utime.tv_usec +
                      (uint64_t)ru.ru_stime.tv_sec * 1000000 + (uint64_t)ru.ru_stime.tv_usec;
        if (p_wall_ns && wall > p_wall_ns)
            s->cpu_game_pct = (float)(100.0 * (double)(us - p_proc_us) * 1000.0 / ((double)(wall - p_wall_ns) * ncpu));
        p_proc_us = us, p_wall_ns = wall;
    }
    // memory: Activity Monitor's "Memory Used" (app memory + wired + compressed), and this process's footprint
    uint64_t mem = 0;
    size_t sz = sizeof mem;
    if (sysctlbyname("hw.memsize", &mem, &sz, NULL, 0) == 0) s->ram_total_mb = (float)(mem >> 20);
    vm_size_t page = 4096;
    host_page_size(mach_host_self(), &page);
    vm_statistics64_data_t vm;
    n = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&vm, &n) == KERN_SUCCESS) {
        uint64_t pages = (uint64_t)vm.internal_page_count - vm.purgeable_count + vm.wire_count + vm.compressor_page_count;
        s->ram_used_mb = (float)((pages * page) >> 20);
    }
    task_vm_info_data_t ti;
    n = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&ti, &n) == KERN_SUCCESS)
        s->ram_game_mb = (float)(ti.phys_footprint >> 20);
    // temperatures: macOS has no public interface for them (the SMC's sensors need private ones): "-"
    s->cpu_temp_c = -1;
    // GPU: the IOAccelerator's statistics (what Activity Monitor's GPU History reads), no privileges needed
    io_iterator_t it;
    if (IOServiceGetMatchingServices(MACH_PORT_NULL, IOServiceMatching("IOAccelerator"), &it) == KERN_SUCCESS) {
        io_object_t o;
        while ((o = IOIteratorNext(it))) {
            CFTypeRef ps = IORegistryEntryCreateCFProperty(o, CFSTR("PerformanceStatistics"), kCFAllocatorDefault, 0);
            if (ps && CFGetTypeID(ps) == CFDictionaryGetTypeID()) {
                int64_t v;
                CFNumberRef nr = CFDictionaryGetValue((CFDictionaryRef)ps, CFSTR("Device Utilization %"));
                if (nr && CFNumberGetValue(nr, kCFNumberSInt64Type, &v)) s->gpu_pct = (float)v;
                nr = CFDictionaryGetValue((CFDictionaryRef)ps, CFSTR("In use system memory"));
                if (nr && CFNumberGetValue(nr, kCFNumberSInt64Type, &v)) s->vram_used_mb = (float)(v >> 20);
            }
            if (ps) CFRelease(ps);
            IOObjectRelease(o);
        }
        IOObjectRelease(it);
    }
}
#else
static float read_float_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    double v = -1;
    if (fscanf(f, "%lf", &v) != 1) v = -1;
    fclose(f);
    return (float)v;
}

// hwmon: k10temp (AMD Tctl) or coretemp (Intel package); else the x86_pkg_temp thermal zone
static float cpu_temp_linux(const char **src) {
    static char path[300];
    static const char *source;
    static bool found;
    if (!found) {
        found = true;
        DIR *d = opendir("/sys/class/hwmon");
        struct dirent *e;
        while (d && (e = readdir(d)) && !path[0]) {
            char np[300], nm[64] = "";
            snprintf(np, sizeof np, "/sys/class/hwmon/%s/name", e->d_name);
            FILE *f = fopen(np, "r");
            if (!f) continue;
            if (fscanf(f, "%63s", nm) != 1) nm[0] = 0;
            fclose(f);
            if (!strcmp(nm, "k10temp") || !strcmp(nm, "coretemp") || !strcmp(nm, "zenpower")) {
                snprintf(path, sizeof path, "/sys/class/hwmon/%s/temp1_input", e->d_name);
                source = !strcmp(nm, "coretemp") ? "coretemp" : nm[0] == 'k' ? "k10temp" : "zenpower";
            }
        }
        if (d) closedir(d);
    }
    *src = source;
    if (!path[0]) return -1;
    float v = read_float_file(path);
    return v < 0 ? -1 : v / 1000.0f;
}

// AMD GPUs: amdgpu's sysfs (busy percent, VRAM, hwmon temperature)
static void amdgpu_sample(PerfStats *s) {
    static char dev[200];
    static bool tried;
    if (!tried) {
        tried = true;
        for (int i = 0; i < 8 && !dev[0]; i++) {
            char p[200];
            snprintf(p, sizeof p, "/sys/class/drm/card%d/device/gpu_busy_percent", i);
            if (access(p, R_OK) == 0) snprintf(dev, sizeof dev, "/sys/class/drm/card%d/device", i);
        }
    }
    if (!dev[0]) return;
    char p[300];
    snprintf(p, sizeof p, "%s/gpu_busy_percent", dev); s->gpu_pct = read_float_file(p);
    snprintf(p, sizeof p, "%s/mem_info_vram_used", dev); float u = read_float_file(p);
    snprintf(p, sizeof p, "%s/mem_info_vram_total", dev); float t = read_float_file(p);
    if (u >= 0) s->vram_used_mb = u / 1048576.0f;
    if (t >= 0) s->vram_total_mb = t / 1048576.0f;
    DIR *d;
    snprintf(p, sizeof p, "%s/hwmon", dev);
    if ((d = opendir(p))) {
        struct dirent *e;
        while ((e = readdir(d)))
            if (e->d_name[0] != '.') {
                char q[400];
                snprintf(q, sizeof q, "%s/%s/temp1_input", p, e->d_name);
                float v = read_float_file(q);
                if (v >= 0) s->gpu_temp_c = v / 1000.0f;
            }
        closedir(d);
    }
}

static void platform_sample(PerfStats *s, double dt) {
    static unsigned long long p_idle, p_total, p_proc;
    FILE *f = fopen("/proc/stat", "r");
    if (f) {
        unsigned long long v[10] = {0};
        if (fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]) >= 4) {
            unsigned long long idle = v[3] + v[4], total = 0;
            for (int i = 0; i < 8; i++) total += v[i];
            unsigned long long ut = 0, st = 0;
            FILE *g = fopen("/proc/self/stat", "r");
            if (g) {
                char buf[1024];
                if (fgets(buf, sizeof buf, g)) {
                    char *p = strrchr(buf, ')');
                    // fields after the name: state ppid ... utime (14th) stime (15th)
                    if (p) sscanf(p + 2, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %llu %llu", &ut, &st);
                }
                fclose(g);
            }
            unsigned long long proc = ut + st;
            if (p_total && total > p_total) {
                s->cpu_total_pct = 100.0f * (float)((total - p_total) - (idle - p_idle)) / (float)(total - p_total);
                s->cpu_game_pct = 100.0f * (float)(proc - p_proc) / (float)(total - p_total);
            }
            p_idle = idle, p_total = total, p_proc = proc;
        }
        fclose(f);
    }
    (void)dt;
    f = fopen("/proc/meminfo", "r");
    if (f) {
        char k[64];
        unsigned long long val, total = 0, avail = 0;
        while (fscanf(f, "%63s %llu kB\n", k, &val) == 2) {
            if (!strcmp(k, "MemTotal:")) total = val;
            if (!strcmp(k, "MemAvailable:")) avail = val;
        }
        fclose(f);
        if (total) s->ram_total_mb = total / 1024.0f, s->ram_used_mb = (total - avail) / 1024.0f;
    }
    f = fopen("/proc/self/statm", "r");
    if (f) {
        unsigned long long sz, rss;
        if (fscanf(f, "%llu %llu", &sz, &rss) == 2) s->ram_game_mb = (float)(rss * (unsigned long long)sysconf(_SC_PAGESIZE) >> 20);
        fclose(f);
    }
    s->cpu_temp_c = cpu_temp_linux(&s->cpu_temp_source);
    if (!nv.ok) amdgpu_sample(s);
}
#endif

// ------------------------------------------------------------------ sampler thread
static void *sampler(void *arg) {
    (void)arg;
    PerfStats s;
    memset(&s, 0, sizeof s);
    s.cpu_temp_c = s.gpu_pct = s.gpu_temp_c = s.gpu_power_w = s.gpu_clock_mhz = s.vram_used_mb = s.vram_total_mb = -1;
    cpu_name(s.cpu_name, sizeof s.cpu_name);
    uint64_t last = plat_now_ns();
    for (;;) {
        uint64_t now = plat_now_ns();
        platform_sample(&s, (now - last) / 1e9);
        nvml_sample(&s);
        last = now;
        pthread_mutex_lock(&g_lock);
        if (!s.gpu_name[0] && g_gpu_hint[0]) snprintf(s.gpu_name, sizeof s.gpu_name, "%s", g_gpu_hint);
        PerfStats keep = g_stats;
        g_stats = s;
        memcpy(g_stats.frame_ms, keep.frame_ms, sizeof keep.frame_ms);
        pthread_mutex_unlock(&g_lock);
        plat_sleep_until_ns(plat_now_ns() + 500000000ull);
    }
    return NULL;
}

void perfmon_set_gpu_hint(const char *n) {
    pthread_mutex_lock(&g_lock);
    snprintf(g_gpu_hint, sizeof g_gpu_hint, "%s", n ? n : "");
    pthread_mutex_unlock(&g_lock);
}

void perfmon_set_gpu_mem(float game_mb, float total_mb) {
    pthread_mutex_lock(&g_lock);
    g_vram_game_mb = game_mb, g_vram_dev_mb = total_mb;
    pthread_mutex_unlock(&g_lock);
}

void perfmon_snapshot(PerfStats *out) {
    static bool started;
    pthread_mutex_lock(&g_lock);
    if (!started) {
        started = true;
        g_stats.cpu_temp_c = g_stats.gpu_pct = g_stats.gpu_temp_c = g_stats.gpu_power_w = -1;
        g_stats.gpu_clock_mhz = g_stats.vram_used_mb = g_stats.vram_total_mb = -1;
        pthread_t th;
        pthread_create(&th, NULL, sampler, NULL);
        pthread_detach(th);
    }
    frames_summary(&g_stats);
    *out = g_stats;
    out->vram_game_mb = g_vram_game_mb;
    if (out->vram_total_mb < 0) out->vram_total_mb = g_vram_dev_mb;  // (no vendor numbers: the renderer's device)
    pthread_mutex_unlock(&g_lock);
}
