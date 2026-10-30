// Builds the game code library on the player's machine (runtime/gamebuild.h).
#include "gamebuild.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "gamelib.h"
#include "paths.h"
#include "platform.h"
#include "rt.h"

// From the Makefile: the flags the game code is compiled and linked with (as `make` builds it in a source checkout)
#ifndef GAMELIB_CFLAGS
#define GAMELIB_CFLAGS "-O0 -g0 -DNDEBUG -fno-strict-aliasing -fvisibility=hidden -fno-sanitize=all"
#endif
#ifndef GAMELIB_SHARED
#define GAMELIB_SHARED "-shared"
#endif

#ifdef _WIN32
#define SEP "\\"
#else
#define SEP "/"
#endif

// ------------------------------------------------------------------ child processes
// Every one running, so that quitting mid-build ends them too (atexit) instead of leaving compilers behind.
#define MAX_PROCS 80
static pthread_mutex_t g_plock = PTHREAD_MUTEX_INITIALIZER;
static PlatProc g_procs[MAX_PROCS];
static int g_nprocs;
static bool g_quitting;

static void end_all(void) {
    pthread_mutex_lock(&g_plock);
    g_quitting = true;
    for (int i = 0; i < g_nprocs; i++) plat_proc_kill(g_procs[i]);
    g_nprocs = 0;
    pthread_mutex_unlock(&g_plock);
}

static PlatProc spawn(char **argv, const char *log) {
    pthread_mutex_lock(&g_plock);
    PlatProc p = g_quitting ? 0 : plat_spawn((const char *const *)argv, log, false);
    if (p && g_nprocs < MAX_PROCS) g_procs[g_nprocs++] = p;
    pthread_mutex_unlock(&g_plock);
    return p;
}

// plat_proc_wait without blocking, then forgotten once it has ended
static int finished(PlatProc p) {
    pthread_mutex_lock(&g_plock);
    int r = plat_proc_wait(p, false);
    if (r >= 0)
        for (int i = 0; i < g_nprocs; i++)
            if (g_procs[i] == p) { g_procs[i] = g_procs[--g_nprocs]; break; }
    pthread_mutex_unlock(&g_plock);
    return r;
}

static void stop(PlatProc p) {  // ends one that is still running, then forgotten
    pthread_mutex_lock(&g_plock);
    bool ours = false;
    for (int i = 0; i < g_nprocs; i++)
        if (g_procs[i] == p) { g_procs[i] = g_procs[--g_nprocs]; ours = true; break; }
    pthread_mutex_unlock(&g_plock);
    if (ours) plat_proc_kill(p);
}

static void sleep_ms(int ms) { plat_sleep_until_ns(plat_now_ns() + (uint64_t)ms * 1000000ull); }

static int wait_for(PlatProc p) {
    int r;
    while ((r = finished(p)) < 0) sleep_ms(20);
    return r;
}

// ------------------------------------------------------------------ argument lists
typedef struct { char **v; int n, cap; } Args;
static void add(Args *a, const char *s) {
    if (a->n + 2 > a->cap) {
        a->cap = a->cap ? a->cap * 2 : 32;
        a->v = (char **)realloc(a->v, sizeof *a->v * a->cap);
    }
    a->v[a->n++] = strdup(s);
    a->v[a->n] = NULL;
}
static void add_split(Args *a, const char *flags) {  // (space-separated, no quoting)
    char buf[1024];
    snprintf(buf, sizeof buf, "%s", flags);
    for (char *t = strtok(buf, " "); t; t = strtok(NULL, " ")) add(a, t);
}
static void args_free(Args *a) {
    for (int i = 0; i < a->n; i++) free(a->v[i]);
    free(a->v);
    memset(a, 0, sizeof *a);
}

// ------------------------------------------------------------------ the tools
typedef struct {
    char gen[1100], gen_script[1100];  // generator: skygen, or (source checkout) the venv's python + tools/gen.py
    char cc[1100];                     // compiler: zig (as `zig cc`), or LARECOMP_CC / clang
    bool zig;
    uint64_t build_stamp;
    char inc[1100], entry[1100], hle[1100];  // headers the game code includes, tools/gamelib_entry.c, HLE names
#ifdef _WIN32
    char implib[1100];  // the .exe's import library (the runtime functions the game code calls)
    char zig_rt[1100];  // the release's zig: its runtime built ahead of time (zig/rt, tools/zig_slim.sh), "" = none
#endif
} Tools;

static bool file_exists(const char *p) {
    struct stat st;
    return stat(p, &st) == 0;
}

static bool find_tools(Tools *t) {
    memset(t, 0, sizeof *t);
    const char *e = getenv("LARECOMP_GEN");
    if (e && *e) snprintf(t->gen, sizeof t->gen, "%s", e);
    else if (!rt_resource("skygen" SEP "skygen" PLAT_EXE_SUFFIX, "build" SEP "helper" SEP "skygen" SEP "skygen" PLAT_EXE_SUFFIX, t->gen, sizeof t->gen) &&
             !rt_resource("lasgen" SEP "lasgen" PLAT_EXE_SUFFIX, "build" SEP "helper" SEP "lasgen" SEP "lasgen" PLAT_EXE_SUFFIX, t->gen, sizeof t->gen) &&
             !rt_resource("build" SEP "helper" SEP "skygen" SEP "skygen" PLAT_EXE_SUFFIX, NULL, t->gen, sizeof t->gen) &&
             !rt_resource("build" SEP "helper" SEP "lasgen" SEP "lasgen" PLAT_EXE_SUFFIX, NULL, t->gen, sizeof t->gen) &&
             !(rt_resource(NULL, "tools/" PLAT_VENV_PYTHON, t->gen, sizeof t->gen) &&
               rt_resource(NULL, "tools/gen.py", t->gen_script, sizeof t->gen_script)))
        return false;
    e = getenv("LARECOMP_CC");
    if (e && *e) snprintf(t->cc, sizeof t->cc, "%s", e);
#ifdef _WIN32
    else if (file_exists("F:\\msys64\\ucrt64\\bin\\clang.exe")) snprintf(t->cc, sizeof t->cc, "F:\\msys64\\ucrt64\\bin\\clang.exe");
    else if (file_exists("C:\\msys64\\ucrt64\\bin\\clang.exe")) snprintf(t->cc, sizeof t->cc, "C:\\msys64\\ucrt64\\bin\\clang.exe");
#endif
    else if (rt_resource("zig" SEP "zig" PLAT_EXE_SUFFIX, "build" SEP "zig" SEP "zig" PLAT_EXE_SUFFIX, t->cc, sizeof t->cc)) t->zig = true;
    else if (rt_resource("build" SEP "zig" SEP "zig" PLAT_EXE_SUFFIX, NULL, t->cc, sizeof t->cc)) t->zig = true;
#ifndef _WIN32
    else if (rt_dev_mode() && file_exists("/usr/bin/clang")) snprintf(t->cc, sizeof t->cc, "/usr/bin/clang");
#endif
    else return false;
    if (!rt_resource("gamelib-src" SEP "cpu.h", "runtime/cpu.h", t->inc, sizeof t->inc) ||
        !rt_resource("gamelib-src" SEP "gamelib_entry.c", "tools/gamelib_entry.c", t->entry, sizeof t->entry) ||
        !rt_resource("gamelib-src" SEP "hle_symbols.txt", "runtime/hle/hle_symbols.txt", t->hle, sizeof t->hle))
        return false;
    char *s = strrchr(t->inc, '/'), *b = strrchr(t->inc, '\\');
    if (b > s) s = b;
    if (s) *s = 0;
#ifdef _WIN32
    if (!rt_resource("skyrecomp.dll.a", "build/skyrecomp.dll.a", t->implib, sizeof t->implib)) return false;
    t->zig_rt[0] = 0;
    if (t->zig && rt_resource("zig" SEP "rt" SEP "link.rsp", NULL, t->zig_rt, sizeof t->zig_rt)) {
        char *s = strrchr(t->zig_rt, '/'), *b = strrchr(t->zig_rt, '\\');
        if (b > s) s = b;
        if (s) *s = 0;
        else t->zig_rt[0] = 0;
    }
#endif
    return true;
}

bool gamebuild_available(void) {
    Tools t;
    return find_tools(&t);
}

static void compiler(Args *a, const Tools *t) {
    add(a, t->cc);
    if (t->zig) add(a, "cc");
}

// ------------------------------------------------------------------ generator progress (its log)
typedef struct {
    const char *name;
    char log[1100], out[1100];
    PlatProc p;
    int exit;                // -1 while running
    int files;               // chunk files written so far (complete)
    unsigned long long ci, n;  // functions written / all
} Gen;

static void read_gen_log(Gen *g) {
    FILE *f = fopen(g->log, "rb");
    if (!f) return;
    static char buf[1 << 16];
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    long from = size > (long)sizeof buf - 1 ? size - (long)sizeof buf + 1 : 0;
    fseek(f, from, SEEK_SET);
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    const char *last = NULL;
    for (const char *p = strstr(buf, ": progress "); p; p = strstr(p + 1, ": progress ")) last = p;
    unsigned long long ci, all;
    int files;
    if (last && sscanf(last, ": progress %llu/%llu files %d", &ci, &all, &files) == 3) {
        g->ci = ci, g->n = all;
        if (files > g->files) g->files = files;
    }
}

static void log_tail(const char *path, char *out, size_t n) {  // the last line of a log (an error message)
    out[0] = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return;
    char buf[4096];
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, size > (long)sizeof buf - 1 ? size - (long)sizeof buf + 1 : 0, SEEK_SET);
    size_t k = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[k] = 0;
    while (k && (buf[k - 1] == '\n' || buf[k - 1] == '\r')) buf[--k] = 0;
    char *nl = strrchr(buf, '\n');
    snprintf(out, n, "%s", nl ? nl + 1 : buf);
}

static void log_error(const char *path, char *out, size_t n) {  // first error line, or the last line
    out[0] = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        if (strstr(line, "error:") || strstr(line, "undefined symbol")) {
            size_t k = strlen(line);
            while (k && (line[k - 1] == '\n' || line[k - 1] == '\r')) line[--k] = 0;
            snprintf(out, n, "%s", line);
            fclose(f);
            return;
        }
    }
    fclose(f);
    log_tail(path, out, n);
}

// ------------------------------------------------------------------ compiling
typedef struct {
    char src[1100], obj[1100], log[1100], stamp[1100];  // stamp: what obj was compiled from (resuming, below)
    unsigned long long bytes;
} Unit;

typedef struct {
    Unit *u;
    int n, cap;
} Units;

static void add_unit(Units *us, const char *src, const char *work, const char *stem) {
    if (us->n == us->cap) {
        us->cap = us->cap ? us->cap * 2 : 64;
        us->u = (Unit *)realloc(us->u, sizeof *us->u * us->cap);
    }
    Unit *u = &us->u[us->n++];
    snprintf(u->src, sizeof u->src, "%s", src);
    snprintf(u->obj, sizeof u->obj, "%s" SEP "%s.o", work, stem);
    snprintf(u->log, sizeof u->log, "%s" SEP "%s.log", work, stem);
    snprintf(u->stamp, sizeof u->stamp, "%s" SEP "%s.stamp", work, stem);
    struct stat st;
    u->bytes = stat(src, &st) == 0 ? (unsigned long long)st.st_size : 0;
}

// The flags the game code was built with (GAMELIB_CFLAGS, part of its id). zig cc reads -march=<cpu> with its own CPU
// names (x86_64_v3) and takes "-<feature>" off the end, so x86-64-v3 would be CPU "x86": the same CPU as -mcpu there.
static void add_cflags(Args *a, const Tools *t) {
    char buf[1024];
    snprintf(buf, sizeof buf, "%s", GAMELIB_CFLAGS);
    for (char *f = strtok(buf, " "); f; f = strtok(NULL, " ")) {
        if (t->zig && !strncmp(f, "-march=", 7)) {
            char cpu[128];
            snprintf(cpu, sizeof cpu, "-mcpu=%s", f + 7);
            for (char *c = cpu + 6; *c; c++) if (*c == '-') *c = '_';
            add(a, cpu);
        } else {
            add(a, f);
        }
    }
    if (t->zig) add(a, "-fno-sanitize=all");
}

// ------------------------------------------------------------------ resuming
// A stopped or failed build keeps its work folder. Each object gets a stamp: FNV-1a of the compiler, its flags, the
// header contents, GAMELIB_ID and the C it was compiled from. The generator writes the same C again on the next run,
// so a unit whose stamp still matches (and whose object is there) isn't compiled again.
#include "mods.h"

// GAMELIB_ID with the mods' fingerprint (set at the start of gamebuild_run): the stamps, the compile flags and the .id
// file all use it, so a library built from other mod files is rebuilt.
#include <dirent.h>

static uint64_t fnv(uint64_t h, const void *p, size_t n);

// The identity of the generator the game code is built with: the prebuilt helper when there is one (it is what runs),
// else tools/gen.py with every .py beside it (the modules it imports). Changing the generator changes it, so a library
// built by the old one is rebuilt. Read once per run.
static uint64_t file_fingerprint(uint64_t h, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return h;
    static char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) h = fnv(h, buf, n);
    fclose(f);
    return h;
}

static uint64_t generator_hash(void) {
    static uint64_t value;
    static bool done;
    if (done) return value;
    done = true;
    uint64_t total = 0xcbf29ce484222325ull;
    char path[1100];
    if (rt_resource("skygen" SEP "skygen" PLAT_EXE_SUFFIX, "build" SEP "helper" SEP "skygen" SEP "skygen" PLAT_EXE_SUFFIX, path, sizeof path)) {
        total = file_fingerprint(total, path);
    } else if (rt_resource(NULL, "tools/gen.py", path, sizeof path)) {
        char dir[1100];
        snprintf(dir, sizeof dir, "%s", path);
        char *s1 = strrchr(dir, '/'), *s2 = strrchr(dir, '\\');
        char *slash = s1 > s2 ? s1 : s2;
        DIR *d = NULL;
        if (slash) {
            slash[1] = 0;
            d = opendir(dir);
        }
        if (d) {
            struct dirent *e;
            while ((e = readdir(d))) {
                size_t len = strlen(e->d_name);
                if (len < 4 || strcmp(e->d_name + len - 3, ".py") != 0) continue;
                char file[1200];
                snprintf(file, sizeof file, "%s%s", dir, e->d_name);
                uint64_t fh = file_fingerprint(fnv(0xcbf29ce484222325ull, e->d_name, len), file);
                total += fh;  // (a sum: the order the files are listed in doesn't matter)
            }
            closedir(d);
        }
    }
    value = total;
    return value;
}

// GAMELIB_ID as this app's library has to be built and checked: base, with the mod files that replace game modules
// (runtime/mods.c) and the generator (above) in it.
void gamebuild_expected_id(const char *base, char *out, size_t cap) {
    char with_mods[256];
    mods_build_id(base, with_mods, sizeof with_mods);
    snprintf(out, cap, "%s-g%016llx", with_mods, (unsigned long long)generator_hash());
}

static char g_lib_id[256];

static uint64_t fnv(uint64_t h, const void *p, size_t n) {
    for (const unsigned char *b = (const unsigned char *)p; n--; b++) h = (h ^ *b) * 0x100000001b3ull;
    return h;
}

static bool hash_file(uint64_t *hash, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) != 0) *hash = fnv(*hash, buf, n);
    bool ok = !ferror(f);
    fclose(f);
    return ok;
}

static bool build_stamp(Tools *t) {
    uint64_t h = 0xcbf29ce484222325ull;
    const char *seed[] = {t->cc, GAMELIB_CFLAGS, t->inc, g_lib_id};
    for (size_t i = 0; i < sizeof seed / sizeof *seed; i++) h = fnv(h, seed[i], strlen(seed[i]) + 1);
    if (!hash_file(&h, t->cc)) return false;
    const char *headers[] = {"cpu.h", "simd.h", "simd_fp.h", "gamelib.h"};
    for (size_t i = 0; i < sizeof headers / sizeof *headers; i++) {
        char path[1200];
        snprintf(path, sizeof path, "%s" SEP "%s", t->inc, headers[i]);
        if (!hash_file(&h, path)) return false;
    }
    t->build_stamp = h;
    return true;
}

static bool unit_stamp(const Tools *t, const Unit *u, char *out, size_t n) {
    uint64_t h = t->build_stamp;
    if (!h || !hash_file(&h, u->src)) return false;
    snprintf(out, n, "%016llx", (unsigned long long)h);
    return true;
}

static bool unit_done(const Tools *t, const Unit *u) {
    struct stat st;
    char want[32], have[32] = "";
    if (stat(u->obj, &st) != 0 || st.st_size == 0 || !unit_stamp(t, u, want, sizeof want)) return false;
    FILE *f = fopen(u->stamp, "rb");
    if (!f) return false;
    size_t k = fread(have, 1, sizeof have - 1, f);
    fclose(f);
    have[k] = 0;
    return !strcmp(want, have);
}

static void unit_mark(const Tools *t, const Unit *u) {
    char s[32];
    if (!unit_stamp(t, u, s, sizeof s)) return;
    FILE *f = fopen(u->stamp, "wb");
    if (f) fputs(s, f), fclose(f);
}

static PlatProc start_compile(const Tools *t, const Unit *u) {
    remove(u->stamp);  // (until it compiles: a stopped compile can leave part of an object)
    Args a = {0};
    compiler(&a, t);
    add_cflags(&a, t);
    char inc[1200];
    snprintf(inc, sizeof inc, "-I%s", t->inc);
    add(&a, inc);
    char def[400];
    snprintf(def, sizeof def, "-DGAMELIB_ID=\"%s\"", g_lib_id);
    add(&a, def);
    add(&a, "-c");
    add(&a, u->src);
    add(&a, "-o");
    add(&a, u->obj);
    PlatProc p = spawn(a.v, u->log);
    args_free(&a);
    return p;
}

// ------------------------------------------------------------------ the build
// The subsdk8 table when the game files have no subsdk8 (the same as tools/subsdk8_empty.c): empty.
static const char STUB_SUBSDK8_TABLE[] =
    "#include \"gamelib.h\"\n"
    "const uint64_t subsdk8_fn_addrs[1] = {0};\n"
    "const GuestFn subsdk8_fn_ptrs[1] = {0};\n"
    "const uint64_t subsdk8_fn_count = 0;\n"
    "const uint32_t subsdk8_interior_flag = 0;\n";

static bool gb_file_exists(const char *p) {
    FILE *f = fopen(p, "rb");
    if (f) fclose(f);
    return f != NULL;
}

bool gamebuild_run(const char *game_dir, const GameBuildOpts *o, char *why, size_t why_size) {
    uint64_t t0 = plat_now_ns();
    static bool registered;
    if (!registered) registered = true, atexit(end_all);
    gamebuild_expected_id(GAMELIB_ID, g_lib_id, sizeof g_lib_id);
    Tools t;
    if (!find_tools(&t)) {
        snprintf(why, why_size, "the game code builder is missing from this copy of Sky Recomp");
        return false;
    }
    if (!build_stamp(&t)) {
        snprintf(why, why_size, "the compiler or game code headers could not be read");
        return false;
    }
    char work[1100], lib[1100], tmp[1100];
    snprintf(work, sizeof work, "%s" SEP ".gamebuild", game_dir);
    snprintf(lib, sizeof lib, "%s" SEP "%s", game_dir, GAMELIB_NAME);
    snprintf(tmp, sizeof tmp, "%s" SEP "%s.tmp", work, GAMELIB_NAME);
    // (.gamebuild stays: a stopped or failed build's objects are reused where their stamps match - resuming, above)
    rt_remove_game_file(game_dir, GAMELIB_NAME);
    rt_remove_game_file(game_dir, GAMELIB_NAME ".id");
    mkdir(work, 0755);
    if (t.zig) {  // its caches in the work folder too (deleted below), not ~/.cache: hundreds of MB, used once
        char zc[1200];
        snprintf(zc, sizeof zc, "%s" SEP "zig-cache", work);
#ifdef _WIN32
        _putenv_s("ZIG_GLOBAL_CACHE_DIR", zc);
        _putenv_s("ZIG_LOCAL_CACHE_DIR", zc);
#else
        setenv("ZIG_GLOBAL_CACHE_DIR", zc, 1);
        setenv("ZIG_LOCAL_CACHE_DIR", zc, 1);
#endif
    }
    LOG("[gamebuild] generator %s%s%s, compiler %s%s, %d jobs\n", t.gen, t.gen_script[0] ? " " : "", t.gen_script,
        t.cc, t.zig ? " cc" : "", o->jobs);

    // 1. Generate all four Skyward Sword modules, compiling finished files as they arrive.
    enum { NMODULES = 5 };
    Gen gens[NMODULES] = {{.name = "main"}, {.name = "sdk"}, {.name = "subsdk0"}, {.name = "subsdk1"}, {.name = "subsdk8"}};
    static const char *const base[NMODULES] = {"0x7100004000", "0x712E74B000", "0x712D037000", "0x712D9FF000", "0x712E0A5000"};
    bool ok = true;
    for (int i = 0; i < NMODULES && ok; i++) {
        Gen *g = &gens[i];
        snprintf(g->log, sizeof g->log, "%s" SEP "gen_%s.log", work, g->name);
        snprintf(g->out, sizeof g->out, "%s" SEP "%s", work, g->name);
        mkdir(g->out, 0755);
        char nso[1100];
        // a mod's file replaces the game's (runtime/mods.c); the game's files are the fallback
        if (!mods_exefs_override(g->name, nso, sizeof nso)) snprintf(nso, sizeof nso, "%s" SEP "exefs" SEP "%s", game_dir, g->name);
        if (strcmp(g->name, "subsdk8") == 0 && !gb_file_exists(nso)) {  // (only the mods have subsdk8: an empty table stands in)
            g->exit = 0;
            char path[1200];
            snprintf(path, sizeof path, "%s" SEP "subsdk8_table.c", g->out);
            FILE *f = fopen(path, "wb");
            if (!f || fputs(STUB_SUBSDK8_TABLE, f) < 0) {
                snprintf(why, why_size, "the game code couldn't be prepared");
                ok = false;
            }
            if (f) fclose(f);
            continue;
        }
        g->exit = -1;
        Args a = {0};
        add(&a, t.gen);
        if (t.gen_script[0]) add(&a, t.gen_script);
        add(&a, g->name);
        add(&a, nso);
        add(&a, base[i]);
        add(&a, g->out);
        if (i == 1) { add(&a, "--hle"); add(&a, t.hle); }
        if (!strcmp(g->name, "subsdk8")) add(&a, "--interior");  // (the mod's code: every instruction can be a target)
        g->p = spawn(a.v, g->log);
        args_free(&a);
        if (!g->p) { snprintf(why, why_size, "the game code builder didn't start"); ok = false; }
    }

    Units units = {0};
    int queued[NMODULES] = {0};  // chunk files of each module put in units so far
    bool tables[NMODULES] = {false};
    int next = 0, jobs = o->jobs < 1 ? 1 : o->jobs > 64 ? 64 : o->jobs;
    struct { PlatProc p; int unit; } run[64];
    int nrun = 0;
    unsigned long long done_bytes = 0;
    int resumed = 0;  // units an earlier run compiled already
    double shown = 0;
    {
        add_unit(&units, t.entry, work, "gamelib_entry");
    }
    while (ok) {
        // generator progress, newly finished files
        bool gens_done = true;
        double gen_frac = 0;
        unsigned long long written = 0;
        for (int i = 0; i < NMODULES; i++) {
            Gen *g = &gens[i];
            if (g->exit < 0) {
                read_gen_log(g);
                int r = finished(g->p);
                if (r >= 0) {
                    g->exit = r;
                    read_gen_log(g);
                    if (r != 0) {
                        char last[300];
                        log_tail(g->log, last, sizeof last);
                        LOG("[gamebuild] generator (%s) failed (exit 0x%08x, log %s): %s\n",
                            g->name, (unsigned)r, g->log, last[0] ? last : "no output was written");
                        snprintf(why, why_size, "the game code couldn't be prepared");
                        ok = false;
                    }
                }
            }
            if (g->exit != 0) gens_done = false;
            for (; queued[i] < g->files; queued[i]++) {
                char src[1100], stem[64];
                snprintf(stem, sizeof stem, "%s_%03d", g->name, queued[i]);
                snprintf(src, sizeof src, "%s" SEP "%s.c", g->out, stem);
                add_unit(&units, src, work, stem);
            }
            if (g->exit == 0 && !tables[i]) {
                char src[1100], stem[64];
                snprintf(stem, sizeof stem, "%s_table", g->name);
                snprintf(src, sizeof src, "%s" SEP "%s.c", g->out, stem);
                add_unit(&units, src, work, stem);
                tables[i] = true;
            }
            gen_frac += g->exit == 0 ? 1.0 / NMODULES : g->n ? (double)g->ci / ((double)g->n * NMODULES) : 0;
        }
        if (!ok) break;
        for (int k = 0; k < units.n; k++) written += units.u[k].bytes;

        // compiles
        while (nrun < jobs && next < units.n) {
            if (unit_done(&t, &units.u[next])) {  // (compiled by an earlier, stopped run)
                Unit *u = &units.u[next++];
                done_bytes += u->bytes, resumed++;
                if (next - 1 > 0) remove(u->src);
                continue;
            }
            PlatProc p = start_compile(&t, &units.u[next]);
            if (!p) { snprintf(why, why_size, "the compiler didn't start"); ok = false; break; }
            run[nrun].p = p, run[nrun].unit = next++;
            nrun++;
        }
        for (int k = 0; k < nrun; k++) {
            int r = finished(run[k].p);
            if (r < 0) continue;
            Unit *u = &units.u[run[k].unit];
            if (r != 0) {
                char last[300];
                log_tail(u->log, last, sizeof last);
                LOG("[gamebuild] compiling %s failed: %s\n", u->src, last);
                snprintf(why, why_size, "the game code didn't compile");
                ok = false;
            } else {
                unit_mark(&t, u);
            }
            done_bytes += u->bytes;
            if (run[k].unit > 0) remove(u->src);  // (generated C, done with: not kept. Unit 0 is the shipped entry file)
            run[k--] = run[--nrun];
        }
        if (!ok) break;
        if (gens_done && next == units.n && nrun == 0) break;

        // progress: the generator's share (its functions written), the compiler's (bytes of C compiled, of an
        // estimate of all of it while the generator still writes)
        double est_total = gens_done ? (double)written : gen_frac > 0.02 ? (double)written / gen_frac : 0;
        double comp_frac = est_total > 0 ? (double)done_bytes / est_total : 0;
        if (comp_frac > 1) comp_frac = 1;
        double f = 0.2 * gen_frac + 0.75 * comp_frac;
        double secs = (double)(plat_now_ns() - t0) / 1e9;
        if (gen_frac == 0) f = 0.05 * (secs / (secs + 20));  // (the generator's first pass: no count yet)
        if (f > shown) shown = f;
        if (o->progress) o->progress(shown, gen_frac < 1 && !gens_done ? "Reading the game code..." : "Recompiling...");
        sleep_ms(50);
    }
    for (int k = 0; k < nrun; k++) stop(run[k].p);  // (a failure: the rest is of no use - the failure shows at once)
    for (int i = 0; i < NMODULES; i++)
        if (gens[i].p && gens[i].exit < 0) stop(gens[i].p);

    // 2. link
    if (ok) {
        if (o->progress) o->progress(shown > 0.95 ? shown : 0.95, "Finishing...");
        char rsp[1100], log[1100];
        snprintf(rsp, sizeof rsp, "%s" SEP "link.rsp", work);
        snprintf(log, sizeof log, "%s" SEP "link.log", work);
        FILE *f = fopen(rsp, "wb");
        if (f) {
            for (int k = 0; k < units.n; k++) {  // (forward slashes: the response file's backslashes are escapes)
                fputc('"', f);
                for (const char *c = units.u[k].obj; *c; c++) fputc(*c == '\\' ? '/' : *c, f);
                fputs("\"\n", f);
            }
            fclose(f);
        }
        Args a = {0};
        char at[1200];
        snprintf(at, sizeof at, "@%s", rsp);
#ifdef _WIN32
        if (t.zig_rt[0]) {  // the linker itself, with the runtime built ahead of time (its flags and libraries: link.rsp)
            char opt[1200];
            add(&a, t.cc);
            add(&a, "lld-link");
            snprintf(opt, sizeof opt, "@%s" SEP "link.rsp", t.zig_rt);
            add(&a, opt);
            snprintf(opt, sizeof opt, "-LIBPATH:%s", t.zig_rt);
            add(&a, opt);
            snprintf(opt, sizeof opt, "-OUT:%s", tmp);
            add(&a, opt);
            add(&a, at);
            add(&a, t.implib);
        } else
#endif
        {
            compiler(&a, &t);
            add_split(&a, GAMELIB_SHARED);
            add(&a, "-o");
            add(&a, tmp);
            add(&a, at);
#ifdef _WIN32
            add(&a, t.implib);
#endif
        }
        PlatProc p = f ? spawn(a.v, log) : 0;
        args_free(&a);
        int r = p ? wait_for(p) : 255;
        if (r != 0) {
            char last[300];
            log_error(log, last, sizeof last);
            LOG("[gamebuild] linking failed: %s\n", last);
            snprintf(why, why_size, "the game code didn't link");
            ok = false;
        }
    }
    if (ok) {
        remove(lib);
        char idp[1100];
        snprintf(idp, sizeof idp, "%s.id", lib);
        FILE *f = NULL;
        if (rename(tmp, lib) != 0 || !(f = fopen(idp, "wb")) || fprintf(f, "%s\n", g_lib_id) < 0) {
            snprintf(why, why_size, "the game code couldn't be saved");
            ok = false;
        }
        if (f) fclose(f);
    }
    free(units.u);
    if (ok) rt_remove_game_file(game_dir, ".gamebuild");  // the C, the objects, the logs (kept to resume a failure)
    LOG("[gamebuild] %s in %.0fs (%d units reused from an earlier run)\n", ok ? "done" : "failed",
        (double)(plat_now_ns() - t0) / 1e9, resumed);
    if (ok && o->progress) o->progress(1, "Finishing...");
    return ok;
}
