#include "mods.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

static ModEntry s_mods[MAX_MODS];
static int s_mod_count = 0;
static bool s_inited = false;

// Saved settings before rescan (or loaded from file)
static char s_disabled_mods_str[2048] = {0};
static char s_mod_order_str[2048] = {0};

static bool file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static bool dir_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int count_files_recursive(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    int count = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        if (!strcmp(e->d_name, "textures")) continue;
        if (!strcmp(e->d_name, "romfs_metadata.bin")) continue;
        char sub[1200];
        snprintf(sub, sizeof sub, "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(sub, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                count += count_files_recursive(sub);
            } else if (S_ISREG(st.st_mode)) {
                count++;
            }
        }
    }
    closedir(d);
    return count;
}

static bool is_mod_disabled(const char *name) {
    if (!s_disabled_mods_str[0]) return false;
    char buf[2048];
    strncpy(buf, s_disabled_mods_str, sizeof buf - 1);
    buf[sizeof buf - 1] = 0;
    char *tok = strtok(buf, ",;\n\r");
    while (tok) {
        // trim whitespace
        while (*tok == ' ') tok++;
        char *end = tok + strlen(tok) - 1;
        while (end > tok && (*end == ' ' || *end == '\t')) *end-- = 0;
        if (!strcmp(tok, name)) return true;
        tok = strtok(NULL, ",;\n\r");
    }
    return false;
}

void mods_open_folder(void) {
#ifdef _WIN32
    CreateDirectoryA("mods", NULL);
    ShellExecuteA(NULL, "open", "mods", NULL, NULL, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    mkdir("mods", 0755);
    system("open mods &");
#else
    mkdir("mods", 0755);
    system("xdg-open mods & >/dev/null 2>&1");
#endif
}

void mods_rescan(void) {
    // Preserve current disabled states into s_disabled_mods_str
    if (s_mod_count > 0) {
        char buf[2048] = {0};
        int pos = 0;
        for (int i = 0; i < s_mod_count; i++) {
            if (!s_mods[i].enabled) {
                int n = snprintf(buf + pos, sizeof buf - pos, "%s%s", pos > 0 ? "," : "", s_mods[i].name);
                if (n > 0) pos += n;
            }
        }
        strncpy(s_disabled_mods_str, buf, sizeof s_disabled_mods_str - 1);
    }

    s_mod_count = 0;

    // Ensure mods/ directory exists
#ifdef _WIN32
    CreateDirectoryA("mods", NULL);
#else
    mkdir("mods", 0755);
#endif

    DIR *d = opendir("mods");
    if (!d) return;

    struct dirent *e;
    while ((e = readdir(d)) && s_mod_count < MAX_MODS) {
        if (e->d_name[0] == '.') continue;
        if (!strcmp(e->d_name, "textures")) continue;
        char mod_dir[1024];
        snprintf(mod_dir, sizeof mod_dir, "mods/%s", e->d_name);
        if (!dir_exists(mod_dir)) continue;

        ModEntry entry;
        memset(&entry, 0, sizeof entry);
        strncpy(entry.name, e->d_name, sizeof entry.name - 1);
        strncpy(entry.path, mod_dir, sizeof entry.path - 1);

        char romfs_dir[1024];
        snprintf(romfs_dir, sizeof romfs_dir, "%s/romfs", mod_dir);

        char meta_path1[1024], meta_path2[1024];
        snprintf(meta_path1, sizeof meta_path1, "%s/romfs_metadata.bin", mod_dir);
        snprintf(meta_path2, sizeof meta_path2, "%s/romfs_metadata.bin", romfs_dir);

        entry.has_metadata = file_exists(meta_path1) || file_exists(meta_path2);

        if (dir_exists(romfs_dir)) {
            strncpy(entry.romfs_path, romfs_dir, sizeof entry.romfs_path - 1);
        } else {
            // Direct loose mod folder structure
            strncpy(entry.romfs_path, mod_dir, sizeof entry.romfs_path - 1);
        }

        entry.file_count = count_files_recursive(entry.romfs_path);
        entry.enabled = !is_mod_disabled(entry.name);

        s_mods[s_mod_count++] = entry;
    }
    closedir(d);

    // Apply saved mod order if any
    if (s_mod_order_str[0] && s_mod_count > 1) {
        ModEntry ordered[MAX_MODS];
        int ordered_count = 0;
        bool taken[MAX_MODS] = {0};

        char buf[2048];
        strncpy(buf, s_mod_order_str, sizeof buf - 1);
        buf[sizeof buf - 1] = 0;
        char *tok = strtok(buf, ",;\n\r");
        while (tok && ordered_count < s_mod_count) {
            while (*tok == ' ') tok++;
            char *end = tok + strlen(tok) - 1;
            while (end > tok && (*end == ' ' || *end == '\t')) *end-- = 0;
            for (int i = 0; i < s_mod_count; i++) {
                if (!taken[i] && !strcmp(s_mods[i].name, tok)) {
                    ordered[ordered_count++] = s_mods[i];
                    taken[i] = true;
                    break;
                }
            }
            tok = strtok(NULL, ",;\n\r");
        }

        // Add remaining mods not listed in order string
        for (int i = 0; i < s_mod_count; i++) {
            if (!taken[i]) {
                ordered[ordered_count++] = s_mods[i];
            }
        }
        memcpy(s_mods, ordered, sizeof(ModEntry) * s_mod_count);
    }

    fprintf(stderr, "[mods] scanned %d mods in mods/ directory\n", s_mod_count);
    for (int i = 0; i < s_mod_count; i++) {
        fprintf(stderr, "[mods] #%d: '%s' [%s] (%d files%s)\n",
                i + 1, s_mods[i].name, s_mods[i].enabled ? "ENABLED" : "DISABLED",
                s_mods[i].file_count, s_mods[i].has_metadata ? ", romfs_metadata.bin" : "");
    }
}

void mods_init(void) {
    if (s_inited) return;
    s_inited = true;
    mods_rescan();
}

int mods_count(void) {
    return s_mod_count;
}

ModEntry *mods_get(int idx) {
    if (idx < 0 || idx >= s_mod_count) return NULL;
    return &s_mods[idx];
}

void mods_set_enabled(int idx, bool enabled) {
    if (idx < 0 || idx >= s_mod_count) return;
    s_mods[idx].enabled = enabled;
}

void mods_move_up(int idx) {
    if (idx <= 0 || idx >= s_mod_count) return;
    ModEntry temp = s_mods[idx];
    s_mods[idx] = s_mods[idx - 1];
    s_mods[idx - 1] = temp;
}

void mods_move_down(int idx) {
    if (idx < 0 || idx >= s_mod_count - 1) return;
    ModEntry temp = s_mods[idx];
    s_mods[idx] = s_mods[idx + 1];
    s_mods[idx + 1] = temp;
}

bool mods_romfs_override(const char *rel_path, char *out_host, size_t cap) {
    for (int i = 0; i < s_mod_count; i++) {
        if (!s_mods[i].enabled) continue;
        char candidate[1200];
        snprintf(candidate, sizeof candidate, "%s/%s", s_mods[i].romfs_path, rel_path);
        if (file_exists(candidate)) {
            strncpy(out_host, candidate, cap - 1);
            out_host[cap - 1] = 0;
            return true;
        }
    }
    return false;
}

int mods_get_active_romfs_dirs(char out_dirs[][1100], int max_dirs) {
    int count = 0;
    for (int i = 0; i < s_mod_count && count < max_dirs; i++) {
        if (s_mods[i].enabled && s_mods[i].file_count > 0) {
            strncpy(out_dirs[count], s_mods[i].romfs_path, 1100 - 1);
            out_dirs[count][1100 - 1] = 0;
            count++;
        }
    }
    return count;
}

// The game's modules that a mod can replace (their files: exefs/<name>).
static const char *const GAME_MODULES[] = {"main", "sdk", "subsdk0", "subsdk1", "subsdk8"};

bool mods_exefs_override(const char *module, char *out_host, size_t cap) {
    mods_init();
    for (int i = 0; i < s_mod_count; i++) {
        if (!s_mods[i].enabled) continue;
        char candidate[1200];
        snprintf(candidate, sizeof candidate, "%s/exefs/%s", s_mods[i].path, module);
        if (file_exists(candidate)) {
            strncpy(out_host, candidate, cap - 1);
            out_host[cap - 1] = 0;
            return true;
        }
    }
    return false;
}

void mods_build_id(const char *base_id, char *out, size_t cap) {
    uint64_t h = 0xcbf29ce484222325ull;
    bool any = false;
    for (size_t m = 0; m < sizeof GAME_MODULES / sizeof GAME_MODULES[0]; m++) {
        char path[1200];
        struct stat st;
        if (!mods_exefs_override(GAME_MODULES[m], path, sizeof path) || stat(path, &st) != 0) continue;
        any = true;
        for (const char *p = GAME_MODULES[m]; *p; p++) h = (h ^ (unsigned char)*p) * 0x100000001b3ull;
        for (const char *p = path; *p; p++) h = (h ^ (unsigned char)*p) * 0x100000001b3ull;
        uint64_t v[2] = {(uint64_t)st.st_size, (uint64_t)st.st_mtime};
        for (size_t k = 0; k < sizeof v; k++) h = (h ^ ((unsigned char *)v)[k]) * 0x100000001b3ull;
    }
    if (!any) snprintf(out, cap, "%s", base_id);
    else snprintf(out, cap, "%s-%016llx", base_id, (unsigned long long)h);
}

void mods_settings_load(const char *key, const char *val) {
    if (!strcmp(key, "disabled_mods")) {
        strncpy(s_disabled_mods_str, val, sizeof s_disabled_mods_str - 1);
        s_disabled_mods_str[sizeof s_disabled_mods_str - 1] = 0;
        for (int i = 0; i < s_mod_count; i++) {
            s_mods[i].enabled = !is_mod_disabled(s_mods[i].name);
        }
    } else if (!strcmp(key, "mod_order")) {
        strncpy(s_mod_order_str, val, sizeof s_mod_order_str - 1);
        s_mod_order_str[sizeof s_mod_order_str - 1] = 0;
    }
}

void mods_settings_save(FILE *f) {
    if (!f) return;
    // Save disabled mods
    char disabled_buf[2048] = {0};
    int dpos = 0;
    for (int i = 0; i < s_mod_count; i++) {
        if (!s_mods[i].enabled) {
            int n = snprintf(disabled_buf + dpos, sizeof disabled_buf - dpos, "%s%s",
                             dpos > 0 ? "," : "", s_mods[i].name);
            if (n > 0) dpos += n;
        }
    }
    if (dpos > 0) {
        fprintf(f, "disabled_mods = %s\n", disabled_buf);
    }

    // Save mod order
    char order_buf[2048] = {0};
    int opos = 0;
    for (int i = 0; i < s_mod_count; i++) {
        int n = snprintf(order_buf + opos, sizeof order_buf - opos, "%s%s",
                         opos > 0 ? "," : "", s_mods[i].name);
        if (n > 0) opos += n;
    }
    if (opos > 0) {
        fprintf(f, "mod_order = %s\n", order_buf);
    }
}
