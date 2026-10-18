#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_MODS 64

typedef struct {
    char name[128];        // Folder name (e.g. "HD_Textures")
    char path[1024];       // Base mod folder (e.g. "mods/HD_Textures")
    char romfs_path[1024]; // Path to romfs root (e.g. "mods/HD_Textures/romfs")
    bool has_metadata;     // True if romfs_metadata.bin was found
    bool enabled;          // True if enabled
    int file_count;        // Count of files in the mod's romfs
} ModEntry;

void mods_init(void);
void mods_rescan(void);
int mods_count(void);
ModEntry *mods_get(int idx);
void mods_set_enabled(int idx, bool enabled);
void mods_move_up(int idx);
void mods_move_down(int idx);
void mods_open_folder(void);

// LayeredFS override: checks enabled mods in priority order
bool mods_romfs_override(const char *rel_path, char *out_host, size_t cap);
// Returns paths of active mod romfs directories for new-file injection
int mods_get_active_romfs_dirs(char out_dirs[][1100], int max_dirs);

void mods_settings_load(const char *key, const char *val);
void mods_settings_save(FILE *f);

#ifdef __cplusplus
}
#endif
