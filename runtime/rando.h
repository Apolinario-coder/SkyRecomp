#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void rando_init(void);
const char *rando_seed_dir(void);  // "rando/<seed>" when a seed is active, else NULL
uint32_t rando_word(uint64_t addr);  // patched instruction at addr for the active seed, 0 = original
bool rando_romfs_override(const char *path, char *out, size_t cap);
void rando_boot(void);               // each boot after the modules load: (re)read the chosen seed, apply its data patches
void rando_select(const char *seed); // seed for the next boot ("" = vanilla), saved; then rt_request_reboot
