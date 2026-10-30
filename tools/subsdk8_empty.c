// The subsdk8 function table when no mod supplies subsdk8 (only the Archipelago mod's files have it): empty, so the
// library and its entry (tools/gamelib_entry.c) link the same way either way. See runtime/gamebuild.c.
#include "gamelib.h"

const uint64_t subsdk8_fn_addrs[1] = {0};
const GuestFn subsdk8_fn_ptrs[1] = {0};
const uint64_t subsdk8_fn_count = 0;
const uint32_t subsdk8_interior_flag = 0;
