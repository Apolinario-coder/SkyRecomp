// Softlock rescues: places Link can reach early in a randomizer seed but can't leave.
//
// The hole below Eagle's Tower (Field_02O, next to the owl statue) drops one-way into the swim-in cave EagleTowerExit.
// Without the Flippers Link drowns there and respawns where he came in - in the water - and drowns again, forever
// (bug report 2026-10-11; vanilla can't reach the hole without them). Without the Flippers, a moment after landing
// (the fall and the first drown play out) he's taken back up to the cave door at the top of the stairs beside the hole
// (the nearest spawn point; moving him onto the ground by the hole instead shows a jump - tried, user 2026-10-11).
#include <stdint.h>
#include <string.h>

#include "../hle.h"

#define PLAYER_STATUS_PTR (MAIN_BASE + 0x1CC1368)  // PlayerStatus*: level name at +0
#define KEY_ITEMS (MAIN_BASE + 0x1CBB220 + 0x556C)  // live save key items: bit 0 = Flippers
extern int game_in_play(void);
extern int warp_busy(void);
extern void warp_request(const char *level, const char *locator);

void rescue_tick(uint64_t frame) {
    static uint64_t since;  // frame Link turned up in the cave (0: not there)
    uint64_t ps = LD64(PLAYER_STATUS_PTR);
    bool there = game_in_play() && ps && !strcmp((const char *)(uintptr_t)ps, "EagleTowerExit") && !(LD32(KEY_ITEMS) & 1);
    if (!there) { since = 0; return; }
    if (!since) since = frame;
    if (frame - since == 150 && !warp_busy()) {
        LOG("[rescue] drowning in EagleTowerExit without the Flippers: back up beside the hole\n");
        warp_request("Field", "Field_02O_a");
    }
}
