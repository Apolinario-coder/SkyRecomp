"""Per-instruction replacements applied by gen.py (guest address -> function(gen, addr) returning C code).

Widescreen: the game's camera code loads a 16:9 aspect-ratio literal (0x70016afba4) as the default
projection aspect. These loads are redirected to rt_aspect_site(), which returns the aspect for the
current output shape (or 16:9) per site, so only the 3D cameras widen.
"""
import struct

ASPECT_LITERAL = 0x70016afba4

# address of `ldr sN, [xM, #0xba4]` -> site id (0x7001023c70 is UI layout math: left alone)
ASPECT_SITES = {
    0x700001092c: 0,
    0x7000010b58: 1,
    0x70000c1c84: 2,
    0x70000c3228: 3,
    0x70000c3edc: 4,
    0x700085f824: 5,
}


def _aspect_load(gen, a):
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    assert (w & 0xffc00000) == 0xbd400000 and ((w >> 10) & 0xfff) * 4 == 0xba4, hex(w)
    rt = w & 31
    return (f'{{ extern float rt_aspect_site(int); float f = rt_aspect_site({ASPECT_SITES[a]}); '
            f'c->v[{rt}] = 0; memcpy(&c->v[{rt}], &f, 4); }}  /* {a:#x}: ldr s{rt}, =16/9 */')


PATCHES = {a: _aspect_load for a in ASPECT_SITES}


# Off-screen culling: the scene's visibility pass loads the camera's aspect from [x27, #0x570] into s9
# (`ldr s9, [x27, #0x570]` at 0x7000852ad4). The community 32:9 patch (Jaddey / KeatonTheBot, build
# AE16F71E...) forces s9 here; we load the runtime aspect instead (site 6).
CULL_ASPECT = 0x7000852ad4


def _cull_aspect(gen, a):
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    assert w == 0xbd457369, hex(w)  # ldr s9, [x27, #0x570]
    return ('{ extern float rt_cull_aspect(float); float o; memcpy(&o, (const void *)(uintptr_t)(c->x[27] + 0x570), 4); '
            'float f = rt_cull_aspect(o); c->v[9] = 0; memcpy(&c->v[9], &f, 4); }'
            f'  /* {a:#x}: ldr s9, [x27, #0x570] (culling aspect) */')


PATCHES[CULL_ASPECT] = _cull_aspect


# Vertical field of view (camera field +0x564): taller-than-16:9 outputs keep the designed horizontal view
# and widen the vertical one ("Vert+"). Loaded by the main camera and by the culling pass.
FOVY_SITES = {0x7000010914: (0xbd4566e0, 23), 0x7000852ad0: (0xbd456768, 27)}


def _fovy_load(gen, a):
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    want, rn = FOVY_SITES[a]
    assert w == want, hex(w)
    rt = w & 31
    return ('{ extern float rt_fovy(float); float o; '
            f'memcpy(&o, (const void *)(uintptr_t)(c->x[{rn}] + 0x564), 4); float f = rt_fovy(o); '
            f'c->v[{rt}] = 0; memcpy(&c->v[{rt}], &f, 4); }}  /* {a:#x}: ldr s{rt}, [x{rn}, #0x564] (fovy) */')


for _a in FOVY_SITES:
    PATCHES[_a] = _fovy_load


# Indoor detection: 0x7001023c70 (`ldr s1, =16/9`) is in the indoor shadow setup (community patch notes:
# "Indoor Shadow AR"). It keeps loading 16:9 and tells the runtime an interior is being rendered.
INDOOR_SITE = 0x7001023c70


def _indoor(gen, a):
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    assert (w & 0xffc00000) == 0xbd400000 and ((w >> 10) & 0xfff) * 4 == 0xba4, hex(w)
    rt = w & 31
    return ('{ extern float rt_indoor_aspect(void); float f = rt_indoor_aspect(); '
            f'c->v[{rt}] = 0; memcpy(&c->v[{rt}], &f, 4); }}  /* {a:#x}: ldr s{rt}, =16/9 (indoor shadow) */')


PATCHES[INDOOR_SITE] = _indoor


# Warp support: the level-jump request object (filled by GameControl.RequestLevelJump via main+0x81b950)
# registers itself in its parent while a level loads: `str xR, [x8, #0x1c0]` at these two sites (two scene
# kinds). Record it so runtime/hle/warp.c can fill the request itself.
WARP_REQUEST_SITES = {0x700080c464: 20, 0x700080d718: 19}


def _warp_request(gen, a):
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    rt = WARP_REQUEST_SITES[a]
    assert w == (0xf900e100 | rt), hex(w)  # str xRT, [x8, #0x1c0]
    return ('{ extern void rt_warp_request_obj(uint64_t); '
            f'*(uint64_t *)(uintptr_t)(c->x[8] + 0x1c0) = c->x[{rt}]; rt_warp_request_obj(c->x[{rt}]); }}'
            f'  /* {a:#x}: str x{rt}, [x8, #0x1c0] (warp: level-jump request) */')


for _a in WARP_REQUEST_SITES:
    PATCHES[_a] = _warp_request

# Blaino's knock-out: Link flies up and a countdown (main+0xd97df0, +0xb8) starts MightPunchCrashOut - fade, then a
# restart warp to his ring's start. On the Field there's no ring: no event (runtime/hle/miniboss.c), Link falls back.
CRASHOUT_SITE = 0x7000d97e2c


def _crashout(gen, a):
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    assert w == 0xb900b81f, hex(w)  # str wzr, [x0, #0xb8]
    return ('{ extern int rt_crashout_skip(void); ST32(c->x[0] + 0xb8, 0); if (rt_crashout_skip()) goto L_7000d97ea4; }'
            f'  /* {a:#x}: str wzr, [x0, #0xb8] (Blaino knock-out: no crash-out event on the Field) */')


PATCHES[CRASHOUT_SITE] = _crashout

# Gohma on the Field (seeds): its moves walk / charge along a track - a heading of +-90 degrees (+0x15ec / +0x15f0,
# the shared movement controller at +0x15c0 turns to it and the mover moves it, walls and all) toward a track end,
# snapping onto the end when there (main+0x399550). The Field has no track: the heading points at Link and there is
# no end - each move runs until its own timer (runtime/hle/miniboss.c).
GOHMA_HEADING_SITES = (0x70003996e4, 0x700039a2c4)  # str w8, [x19, #0x15ec] (then the same to +0x15f0)
GOHMA_ARRIVED_SITE = 0x700039957c  # ldr q0, [x19, #0x1600] (the track end, against Gohma's spot)


def _gohma_heading(gen, a):
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    assert w == 0xb915ee68, hex(w)  # str w8, [x19, #0x15ec]
    return ('{ extern uint32_t rt_gohma_heading(uint64_t, uint32_t); '
            'c->x[8] = rt_gohma_heading(c->x[19], (uint32_t)c->x[8]); ST32(c->x[19] + 0x15ec, (uint32_t)c->x[8]); }'
            f'  /* {a:#x}: str w8, [x19, #0x15ec] (Gohma: heading at Link on the Field) */')


def _gohma_arrived(gen, a):
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    assert w == 0x3dc58260, hex(w)  # ldr q0, [x19, #0x1600]
    return ('{ extern int rt_gohma_free(void); if (rt_gohma_free()) { c->x[0] = 0; goto L_70003995c0; } '
            'memcpy(&c->v[0], (const void *)(uintptr_t)(c->x[19] + 0x1600), 16); }'
            f'  /* {a:#x}: ldr q0, [x19, #0x1600] (Gohma: no track end on the Field) */')


# Master Stalfos' body collision (CharaBodyCollision, main+0x1841c0 from his constructor) blocks Link - in his arena,
# fine; on the Field Link couldn't damage-boost past him as past the other mini-bosses. Its flag (x6 -> sp+0x328) off
# there (seeds, level Field).
MSTALFOS_BODY_SITE = 0x70004b47c8  # mov w8, #1


def _mstalfos_body(gen, a):
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    assert w == 0x320003e8, hex(w)  # mov w8, #1
    return ('{ extern int rt_gohma_free(void); c->x[8] = rt_gohma_free() ? 0 : 1; }'
            f'  /* {a:#x}: mov w8, #1 (Master Stalfos body collision flag: off on the Field) */')


PATCHES[MSTALFOS_BODY_SITE] = _mstalfos_body

# Master Stalfos' jump (state 4 exec) sets its vertical speed to 6 * sin(phase) - height: an arc from his arena's floor
# (height 0). On the Field the ground is higher - he dropped through it. There the arc starts from his takeoff height
# (his movement component [+0x1160] + 0x28c, stored as the jump starts); in the arena that is 0 anyway.
MSTALFOS_JUMP_SITE = 0x70004bde08  # fsub s0, s0, s1


def _mstalfos_jump(gen, a):
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    assert w == 0x1e213800, hex(w)  # fsub s0, s0, s1
    return ('{ extern int rt_gohma_free(void); float a_, b_; memcpy(&a_, &c->v[0], 4); memcpy(&b_, &c->v[1], 4); '
            'if (rt_gohma_free()) a_ += *(const float *)(uintptr_t)(LD64(c->x[19] + 0x1160) + 0x28c); '
            '__uint128_t r_ = 0; a_ -= b_; memcpy(&r_, &a_, 4); c->v[0] = r_; }'
            f'  /* {a:#x}: fsub s0, s0, s1 (Master Stalfos jump: arc from his takeoff height on the Field) */')


PATCHES[MSTALFOS_JUMP_SITE] = _mstalfos_jump

# Moblin Chief deletes himself in his init (main+0x1f8af0) unless a story bit is set (player status +0xac bit 7 - his
# hideout's time); on the Field (seeds) that check is skipped. The defeated check before it stays.
MCHIEF_STORY_SITE = 0x70001f8b30  # tbnz w8, #7, 0x70001f8b58


def _mchief_story(gen, a):
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    assert w == 0x37380148, hex(w)  # tbnz w8, #7, +0x28
    return ('{ extern int rt_gohma_free(void); if (rt_gohma_free() || ((c->x[8] >> 7) & 1)) goto L_70001f8b58; }'
            f'  /* {a:#x}: tbnz w8, #7 (Moblin Chief: no story check on the Field) */')


PATCHES[MCHIEF_STORY_SITE] = _mchief_story
for _a in GOHMA_HEADING_SITES:
    PATCHES[_a] = _gohma_heading
PATCHES[GOHMA_ARRIVED_SITE] = _gohma_arrived


# Generated dungeons (runtime/hle/chamber.c): every level jump goes through the request processor main+0x81ba80
# (x0 = request); its prologue reports the request so jumps into Tail Cave can be redirected.
LEVEL_REQUEST_SITE = 0x700081ba80


def _level_request(gen, a):
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    assert w == 0xd10203ff, hex(w)  # sub sp, sp, #0x80
    return ('{ extern void rt_level_request(Cpu *); rt_level_request(c); c->sp -= 0x80; }'
            f'  /* {a:#x}: sub sp, sp, #0x80 (level-jump request) */')


PATCHES[LEVEL_REQUEST_SITE] = _level_request


# Chamber staircase exit prompt ("Return to Arranging/List" vs "Back to Adventuring"): main+0x7f1540 opens it with
# `bl main+0xf05280` at 0x7f15e0; the callback only stores the chosen option at [main+0x1cb9fb8]+0x18f8 (0 = leave).
# In a generated dungeon the dialog is skipped and 0 stored, so the staircase leaves directly (runtime/hle/chamber.c).
ADV_EXIT_DIALOG_SITE = 0x70007f15e0


def _adv_exit_dialog(gen, a):
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    assert w == 0x9402_5d28 or (w >> 26) == 0x25, hex(w)  # bl
    return ('{ extern int rt_adv_exit_choice(void); extern void f_7000f05280(Cpu *); '
            'if (!rt_adv_exit_choice()) { c->x[30] = 0x70007f15e4ull; f_7000f05280(c); } }'
            f'  /* {a:#x}: bl 0xf05280 (chamber exit prompt) */')


PATCHES[ADV_EXIT_DIALOG_SITE] = _adv_exit_dialog


# Difficulty (runtime/ui/difficulty.mm): Link's damage is computed as `damage << hero` (Hero Mode doubles it;
# hero = PlayerStatus+0x120 == 1) at these `lsl Wd, Wn, Wm` sites, then subtracted from health (+0x84).
# Each is redirected to rt_damage(damage, shift) so the difficulty multiplier / one-hit KO applies.
DAMAGE_LSL_SITES = (0x7000d7c8c0, 0x7000d9694c, 0x7000d4c738)
# Fixed damage (-2 quarter hearts, -4 in Hero Mode: `csel w20, w10, w9, eq` with w10=-4, w9=-2).
DAMAGE_FIXED_SITE = 0x7000db1f6c


def _damage_lsl(gen, a):
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    assert (w & 0xffe0fc00) == 0x1ac02000, hex(w)  # lslv (32-bit)
    rd, rn, rm = w & 31, (w >> 5) & 31, (w >> 16) & 31
    return (f'{{ extern int32_t rt_damage(int32_t, int32_t); c->x[{rd}] = (uint32_t)rt_damage((int32_t)c->x[{rn}], '
            f'(int32_t)(c->x[{rm}] & 31)); }}  /* {a:#x}: lsl w{rd}, w{rn}, w{rm} (damage) */')


def _damage_fixed(gen, a):
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    assert (w & 0xffe00c00) == 0x1a800000 and (w & 31) == 20, hex(w)  # csel w20, ...
    return ('{ extern int32_t rt_damage(int32_t, int32_t); extern int rt_hero_mode(void); '
            'c->x[20] = (uint32_t)(-rt_damage(2, rt_hero_mode())); }'
            f'  /* {a:#x}: csel w20, -4, -2 (fixed damage) */')


for _a in DAMAGE_LSL_SITES:
    PATCHES[_a] = _damage_lsl
PATCHES[DAMAGE_FIXED_SITE] = _damage_fixed


# Items page description ("UI:ItemExplanation_{n}"): main+0xeb6604.. builds the item name then looks the explanation
# message up (bl 0x903250 -> [sp+0xf0]); a null result takes the no-text path (buffer already cleared). In randomizer
# seeds the explanation is hidden (it overlaps the key / trade-item rows of the inventory overlay; player knows).
ITEM_EXPLANATION_SITE = 0x7000eb66c0


def _item_explanation(gen, a):
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    assert w == 0xf9407bf6, hex(w)  # ldr x22, [sp, #0xf0]
    return ('{ extern int rt_hide_item_explanation(void); '
            'c->x[22] = rt_hide_item_explanation() ? 0 : LD64(c->sp + 0xf0); }'
            f'  /* {a:#x}: ldr x22, [sp, #0xf0] (item explanation message) */')


PATCHES[ITEM_EXPLANATION_SITE] = _item_explanation

# ... and the item NAME ("UI:ItemName_{0}"): main+0xeb6658 `bl 0xf8c8b0` sets the name pane from that message; in
# randomizer seeds it is skipped too (user 2026-10-08: the name showed under the key rows of the inventory overlay).
ITEM_NAME_SITE = 0x7000eb6658


def _item_name(gen, a):
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    assert w == 0x94035896, hex(w)  # bl 0x7000f8c8b0
    return ('c->x[30] = 0x7000eb665cull; { extern int rt_hide_item_explanation(void); '
            'if (!rt_hide_item_explanation()) f_7000f8c8b0(c); }'
            f'  /* {a:#x}: bl 0xf8c8b0 (set the item name pane) */')


PATCHES[ITEM_NAME_SITE] = _item_name


# Keysanity (runtime/hle/keysanity.c): AddItem (main+0x7e18b0, x0 item / x1 count / x2 index) only adds dungeon
# items to the current dungeon; with an explicit dungeon index the hook updates that dungeon's save record instead.
ADD_ITEM_SITE = 0x70008d08e0  # inner add (34 callers: events, chests, drops); AddItem 0x7e18b0 wraps it


def _add_item(gen, a):
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    assert w == 0xd10143ff, hex(w)  # sub sp, sp, #0x50
    return ('{ extern int rt_add_item(Cpu *); if (rt_add_item(c)) return; c->sp -= 0x50; }'
            f'  /* {a:#x}: sub sp, sp, #0x50 (AddItem: keysanity) */')


PATCHES[ADD_ITEM_SITE] = _add_item


# Trendsanity (runtime/hle/shopsanity.c): AddPrizeByKey (main+0x7e1c70) looked the prize row up (x0) and is about to
# call AddPrize(row, count, index) (main+0x1489c0, rows 0-20 only): a randomized prize row sets its check's flags.
PRIZE_ADD_SITE = 0x70007e1d10


def _prize_add(gen, a):
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    assert w == 0x2a1403e2, hex(w)  # mov w2, w20
    return ('{ c->x[2] = (uint32_t)c->x[20]; extern void rt_add_prize(Cpu *); rt_add_prize(c); }'
            f'  /* {a:#x}: mov w2, w20 (AddPrize index; trendsanity) */')


PATCHES[PRIZE_ADD_SITE] = _prize_add


# Inventory overlay (runtime/ui/inventory_overlay.mm): main+0xeb7cd0 runs once per frame only while the pause
# menu's Items page is shown (found by profiling Items vs Map/System pages). Its prologue reports that.
ITEMS_PAGE_SITE = 0x7000eb7cd0


def _items_page(gen, a):
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    assert w == 0xd10243ff, hex(w)  # sub sp, sp, #0x90
    return ('{ extern void rt_items_page_tick(void); rt_items_page_tick(); c->sp -= 0x90; }'
            f'  /* {a:#x}: sub sp, sp, #0x90 (items page shown) */')


PATCHES[ITEMS_PAGE_SITE] = _items_page


# Check tracker (runtime/ui/tracker_overlay.mm): main+0xe93800 runs once per frame only while the pause menu's
# Map page is shown (fnprof diff of Map vs Items/System/closed). Its prologue reports that.
MAP_PAGE_SITE = 0x7000e93800


def _map_page(gen, a):
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    assert w == 0xd10203ff, hex(w)  # sub sp, sp, #0x80
    return ('{ extern void rt_map_page_tick(Cpu *); rt_map_page_tick(c); c->sp -= 0x80; }'
            f'  /* {a:#x}: sub sp, sp, #0x80 (map page shown) */')


PATCHES[MAP_PAGE_SITE] = _map_page


# Chaos map letters (runtime/ui/chaos_overlay.mm): main+0xe78740 runs once per frame while the DUNGEON map page is
# shown (fnprof: map open vs closed in Face Shrine); x0 = the dungeon map page.
DGN_MAP_SITE = 0x7000e78740


def _dgn_map(gen, a):
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    assert w == 0xd10243ff, hex(w)  # sub sp, sp, #0x90
    return ('{ extern void rt_dgnmap_tick(Cpu *); rt_dgnmap_tick(c); c->sp -= 0x90; }'
            f'  /* {a:#x}: sub sp, sp, #0x90 (dungeon map page shown) */')


PATCHES[DGN_MAP_SITE] = _dgn_map


# Debug (LARECOMP_LOG_RESNAME=1): main+0x10def30 builds a resource handle from a name (x2 -> {char *name, ...})
# and looks it up; the generated-dungeon map crash dereferences a handle whose resource wasn't found.
RES_NAME_SITE = 0x70010def30


def _res_name(gen, a):
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    assert w == 0xd10503ff, hex(w)  # sub sp, sp, #0x140
    return ('{ extern void rt_log_resname(uint64_t); rt_log_resname(c->x[2]); c->sp -= 0x140; }'
            f'  /* {a:#x}: sub sp, sp, #0x140 (resource-by-name debug log) */')


PATCHES[RES_NAME_SITE] = _res_name


# Field mini-bosses (enemy randomizer, runtime/hle/miniboss.c): a mini-boss waits in its idle state until its dungeon
# room's start event runs, which never happens in the Field. f_70000ec310 (an actor component's per-frame update)
# asks its actor a type question through vtable +0x68 every frame: the runtime sees each actor there, picks out the
# mini-boss classes by vtable and moves them into their fight state itself.
MB_SEEN_SITE = 0x70000ec354


def _mb_seen(gen, a):
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    assert w == 0xf94002a8, hex(w)  # ldr x8, [x21]
    return ('{ extern void rt_mb_seen(uint64_t); rt_mb_seen(c->x[21]); c->x[8] = LD64(c->x[21]); }'
            f'  /* {a:#x}: ldr x8, [x21] (actor seen: field mini-bosses) */')


PATCHES[MB_SEEN_SITE] = _mb_seen


# Frame pacing (runtime/hle/framepace.c): the game's frame timer (f_700000c910) advances by its frame period times
# the swap interval it reads from the system object (+0x812c, written only by the interval setter f_7000017800). It
# reads it here and nowhere else: we hand it the number of vsyncs that really passed since the last frame, so a
# late frame advances the world by the time that passed (no slow motion below 60 fps).
FRAME_STEPS_SITE = 0x700000c954


def _frame_steps(gen, a):
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    assert w == 0xb8696908, hex(w)  # ldr w8, [x8, x9]
    return ('{ extern uint32_t rt_frame_steps(uint32_t interval); '
            'c->x[8] = rt_frame_steps(LD32(c->x[8] + c->x[9])); }'
            f'  /* {a:#x}: ldr w8, [x8, x9] (swap interval -> vsyncs elapsed: runtime/hle/framepace.c) */')


PATCHES[FRAME_STEPS_SITE] = _frame_steps


# Digsanity (runtime/hle/digs.c): the shovel's dig commit (f_7000ddc530) spawns the hole actor (ObjHolesDug 0x1cd)
# through f_7000d228d0(&out, transform x21, zone, &room, ...); right after it the hole (out at [sp+0x10]) and its
# snapped tile-centre position (x21) tell the runtime which tile was dug - the first dig of a tile gives its item.
DIG_HOLE_SITE = 0x7000ddc5cc


def _dig_hole(gen, a):
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    assert w == 0xf94007f6, hex(w)  # ldr x22, [sp, #8]
    return ('{ extern void rt_dig_hole(Cpu *); rt_dig_hole(c); c->x[22] = LD64(c->sp + 8); }'
            f'  /* {a:#x}: ldr x22, [sp, #8] (a hole was dug: runtime/hle/digs.c) */')


PATCHES[DIG_HOLE_SITE] = _dig_hole

# The game keeps its dug holes in a list (main+0x1cbae80, count at +0x10) and fills in the oldest ones once there are
# more than 80 (f_7000d22d10: while count > 0x50). Digsanity keeps holes open: the limit comes from the runtime.
HOLE_CAP_SITES = {0x7000d22d30: (0xf101451f, 0, 'cmp x8, #0x51'), 0x7000d22dc0: (0xf101411f, 1, 'cmp x8, #0x50')}


def _hole_cap(gen, a):
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    want, minus, text = HOLE_CAP_SITES[a]
    assert w == want, hex(w)
    return ('{ extern uint64_t rt_hole_cap(void); '
            f'(void)(subs64(c, c->x[8], rt_hole_cap() - {minus}ull)); }}'
            f'  /* {a:#x}: {text} (dug-hole limit: runtime/hle/digs.c) */')


for _a in HOLE_CAP_SITES:
    PATCHES[_a] = _hole_cap
# Escape Bottle (runtime/hle/escape.c): the player's per-item "use it now?" predicates (one per item slot, main+0xd50e60
# .. 0xd52f00; each: X / Y newly pressed with that slot equipped on it) only run while Link may use items (no cutscene,
# dialog, menu or transition). main+0xd50e60 is the third bottle's (slot 0xf): owned (PlayerStatus+0xa9 bit 7) and
# empty (+0xc0 bit 2 clear) -> `cset w0, eq` = yes, and the bottle swing starts. In randomizer seeds that bottle is the
# Escape Bottle: the runtime warps instead and the answer is no.
BOTTLE3_USE_SITE = 0x7000d50f0c


def _bottle3_use(gen, a):
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    assert w == 0x1a9f17e0, hex(w)  # cset w0, eq
    return ('{ extern int rt_bottle3_use(void); c->x[0] = COND_NE(c) ? 0 : 1; if (c->x[0] && rt_bottle3_use()) c->x[0] = 0; }'
            f'  /* {a:#x}: cset w0, eq (third bottle used: Escape Bottle) */')


PATCHES[BOTTLE3_USE_SITE] = _bottle3_use


# Boss souls (runtime/hle/souls.c): the actor factory f_70008f5ea0 (x2 = spawn info, type u16 at +8) takes the type's
# create function from the actor table (main+0x1bcc898, 0x58 bytes per type, +0 create fn, +0x50 name); a null fn is
# its "no such actor" path (the caller gets an empty handle). A dungeon boss whose soul isn't found yet gets that path.
BOSS_SPAWN_SITE = 0x70008f5ed4


def _boss_spawn(gen, a):
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    assert w == 0xf9400118, hex(w)  # ldr x24, [x8]
    return ('{ extern uint64_t rt_actor_create_fn(uint64_t fn, uint32_t type, uint64_t info); '
            'c->x[24] = rt_actor_create_fn(LD64(c->x[8]), (uint32_t)c->x[23], c->x[2]); }'
            f'  /* {a:#x}: ldr x24, [x8] (actor create fn: boss souls) */')


PATCHES[BOSS_SPAWN_SITE] = _boss_spawn


# Hookshot anything (runtime/hle/hookshot.c): the hookshot's Shot update (main+0xd7ecc0) loads the attribute word of
# the surface its ray hit; bit 0x4000 (hookshot target) makes it latch (Adhere) instead of bouncing off (Return).
HOOKSHOT_ATTR_SITE = 0x7000d7ee9c


def _hookshot_attr(gen, a):
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    assert w == 0xb9403bf7, hex(w)  # ldr w23, [sp, #0x38]
    return ('{ extern uint32_t rt_hookshot_attr(Cpu *, uint32_t); '
            'c->x[23] = rt_hookshot_attr(c, LD32(c->sp + 0x38)); }'
            f'  /* {a:#x}: ldr w23, [sp, #0x38] (hookshot hit attribute) */')


PATCHES[HOOKSHOT_ATTR_SITE] = _hookshot_attr


# 360-degree movement (runtime/hle/qol.c, setting move_360): main+0xded9f0 snaps Link's stick vector (s1 = x, s2 = y)
# to one of 8 directions and returns it as a 32-bit angle (full turn = 2^32, 0 = down). With the setting on, the
# runtime returns the stick's own angle instead (as the community "360 Controls" mod does).
SNAP_DIR_SITE = 0x7000ded9f0


def _snap_dir(gen, a):
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    assert w == 0xd10103ff, hex(w)  # sub sp, sp, #0x40
    return ('{ extern int rt_snap_dir(Cpu *); if (rt_snap_dir(c)) return; c->sp -= 0x40; }'
            f'  /* {a:#x}: sub sp, sp, #0x40 (8-way stick snap: 360-degree movement) */')


PATCHES[SNAP_DIR_SITE] = _snap_dir


# No Guardian Acorns / Pieces of Power (randomizer no-buff-drops, runtime/hle/qol.c): the drop roll main+0x89b960 reads
# its ItemDrop row's mAllowBuffReplace (+0x38); set, the kill counters may turn the drop into a Piece of Power / Guardian
# Acorn. Read as false for such seeds.
BUFF_REPLACE_SITE = 0x700089ba34


def _buff_replace(gen, a):
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    assert w == 0x3940e308, hex(w)  # ldrb w8, [x24, #0x38]
    return ('{ extern int rt_buff_replace(int); c->x[8] = (uint32_t)rt_buff_replace(LD8(c->x[24] + 0x38)); }'
            f'  /* {a:#x}: ldrb w8, [x24, #0x38] (mAllowBuffReplace: no-buff-drops) */')


PATCHES[BUFF_REPLACE_SITE] = _buff_replace


# Blur removal (runtime/hle/qol.c, setting blur_removal): main+0x1071220 sets up the post-process's tilt-shift object
# (x0) once, looking its shader parameters up by name; qol.c keeps the object and takes the parameter handle out / puts
# it back live.
TILT_INIT_SITE = 0x7001071220


def _tilt_init(gen, a):
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    assert w == 0xd10243ff, hex(w)  # sub sp, sp, #0x90
    return ('{ extern void rt_tilt_obj(uint64_t); rt_tilt_obj(c->x[0]); c->sp -= 0x90; }'
            f'  /* {a:#x}: sub sp, sp, #0x90 (tilt-shift set-up: blur removal) */')


PATCHES[TILT_INIT_SITE] = _tilt_init


# Event parameters: main+0x109fb80 looks up a String parameter of an event action and throws when it's missing
# ("String 型の '{0}' というパラメータが見つかりません"). Two of the game's timelines leave an AimPlayer / AimActor
# 'boneName' out (tl/WindFishSong: learning Marin's song; tl/HippoModelSlap); on the Switch the throw is caught up in
# the event system, but recompiled code can't unwind (crash). A missing String reads as "" instead - what the other
# events pass for boneName (tl/ManboTamegoro's PlayOneshotSE has no 'label': no sound).
MISSING_STRING_SITE = 0x700109fca8


def _missing_string(gen, a):
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    assert w == 0x321c03e0, hex(w)  # mov w0, #0x10 (orr w0, wzr, #0x10: __cxa_allocate_exception size)
    # "" (the terminator of ".ptcl" right before the message; callers strlen() the result, so not NULL)
    return ('{ ST64(c->sp + 0x18, 0x70016cf23aull); ST32(c->sp + 0x20, 0); goto L_700109fc78; }'
            f'  /* {a:#x}: mov w0, #0x10 (missing String event parameter: return "", no throw) */')


PATCHES[MISSING_STRING_SITE] = _missing_string

# The Float version (main+0x109fad0, "Float 型の '{0}' というパラメータが見つかりません"): a randomizer item on an
# instrument pedestal (any non-instrument) runs an action whose Float 'time' is missing - picking it up crashed
# (bug report 2026-10-11, reproduced on rando/DevInstr1 Tail Cave). A missing Float reads as 0.0 instead: the reader
# (0x7f3c90) treats time <= 0 as instant.
MISSING_FLOAT_SITE = 0x700109fb18


def _missing_float(gen, a):
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    assert w == 0x321c03e0, hex(w)  # mov w0, #0x10 (__cxa_allocate_exception size)
    # s0 = 0.0 (q0 at Cpu+512), then the function's own epilogue
    return ('{ __builtin_memset((char *)c + 512, 0, 16);'
            ' c->x[29] = LD64(c->sp + 48); c->x[30] = LD64(c->sp + 56);'
            ' c->x[20] = LD64(c->sp + 32); c->x[19] = LD64(c->sp + 40); c->sp += 0x40; return; }'
            f'  /* {a:#x}: mov w0, #0x10 (missing Float event parameter: return 0.0, no throw) */')


PATCHES[MISSING_FLOAT_SITE] = _missing_float


# Treat Tunics as Upgrades (runtime/hle/qol.c rt_tunic_upgrades): the add-item code (main+0x8d08e0) sets a tunic's
# bit in the tunics byte (PlayerStatus +0x9c = save +0x555C: 0x40 red, 0x80 blue), then clears the other one; with
# the seed option the clear is skipped, so both tunics (and both effects) are kept.
TUNIC_CLEAR_SITES = {
    0x70008d1118: (0x12197929, 0xffffffbf, 'and w9, w9, #0xffffffbf (blue tunic: red cleared)'),
    0x70008d12f8: (0x12001929, 0x7f, 'and w9, w9, #0x7f (red tunic: blue cleared)'),
}


def _tunic_clear(gen, a):
    word, mask, what = TUNIC_CLEAR_SITES[a]
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    assert w == word, hex(w)
    return ('{ extern int rt_tunic_upgrades(void); '
            f'if (!rt_tunic_upgrades()) c->x[9] = (uint32_t)((uint32_t)c->x[9] & {mask:#x}u); }}'
            f'  /* {a:#x}: {what} - kept with Treat Tunics as Upgrades */')


for _a in TUNIC_CLEAR_SITES:
    PATCHES[_a] = _tunic_clear


# Underwater checks (tools/rando/underwater.py; LAS 0.5 dev build's main+0x8c45b8 patch): the item actor's "should I
# appear?" (main+0x8c4510) asks the inventory whether Link owns its item (bl HasItem(item +0x2e8, 1, index +0xbc)). The
# sunken heart-piece actors hold randomized items now and hide by their own flag (Npc.gsheet layout condition), so for
# heart-piece actors in seeds made with it the answer is "not owned" (runtime/hle/qol.c rt_underwater_skip_owned).
UNDERWATER_OWNED_SITE = 0x70008c45b8


def _underwater_owned(gen, a):
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    assert w == 0x9400351e, hex(w)  # bl 0x8d1a30 (HasItem)
    return ('{ extern int rt_underwater_skip_owned(uint64_t); if (rt_underwater_skip_owned(c->x[20])) c->x[0] = 0; '
            'else { extern void f_70008d1a30(Cpu *); c->x[30] = 0x70008c45bcull; f_70008d1a30(c); } }'
            f'  /* {a:#x}: bl HasItem (sunken heart pieces: their own flag decides) */')


PATCHES[UNDERWATER_OWNED_SITE] = _underwater_owned

# ...and their item models show underwater. Water is opaque here (its depth hides what's below): what's seen below the
# surface is a second, low-resolution view the water samples, holding only models whose materials have the shader option
# under_water - the heart piece's do, other items' don't. Model setup (main+0xff53d8 loop over a material's options)
# reads it at 0xff54b4; in underwater-item seeds every material counts as under_water (as LAS-Randomizer's 0.5 dev
# build does: its exlaunch mod writes mov w27, #1 here).
UNDERWATER_VIEW_SITE = 0x7000ff54c0


def _underwater_view(gen, a):
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    assert w == 0x1a9f07fb, hex(w)  # cset w27, ne (under_water != 0)
    return ('{ extern int rt_underwater_items(void); c->x[27] = (rt_underwater_items() || COND_NE(c)) ? 1u : 0u; }'
            f'  /* {a:#x}: cset w27, ne (under_water: forced on for sunken items) */')


PATCHES[UNDERWATER_VIEW_SITE] = _underwater_view

# ...and picking one up gives only its own item (its event): the heart-piece actor's pickup (main+0x855a0, x19 = the
# actor) would also add a heart piece (bl 0x89b960; LAS-Randomizer's 0.5 dev build NOPs it).
UNDERWATER_PICKUP_SITE = 0x70000856e4


def _underwater_pickup(gen, a):
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    assert w == 0x9420589f, hex(w)  # bl 0x89b960
    return ('{ extern int rt_underwater_skip_owned(uint64_t); if (!rt_underwater_skip_owned(c->x[19])) '
            '{ extern void f_700089b960(Cpu *); c->x[30] = 0x70000856e8ull; f_700089b960(c); } }'
            f'  /* {a:#x}: bl 0x89b960 (adds the heart piece: skipped for sunken items) */')


PATCHES[UNDERWATER_PICKUP_SITE] = _underwater_pickup


# Event actions with no handler: main+0x108e300 runs an event action through the actor's handler map; an entry whose
# std::function is empty throws std::bad_function_call (0x108edc8), which the same function catches (catch
# std::exception, landing pad 0x108f1c8 -> 0x108ef3c) to abort the event. Common.bfevfl's OcarinaWarpOut (every Manbo's
# Mambo warp) gets here at WarpHoleEventBox.PlayTailorOtherChannelEx, after the level jump has been requested.
# Recompiled code can't unwind, so the throw goes straight into the catch block, without the C++ runtime's
# begin / end catch (nothing was thrown).
NO_HANDLER_SITES = {
    0x700108edc8: (0x321d03e0, 'goto L_700108ef3c;', 'mov w0, #8 (bad_function_call: to the catch block)'),
    0x700108ef40: (0x940e8868, '', 'bl __cxa_begin_catch (skipped)'),
    0x700108f074: (0x940e887b, '', 'bl __cxa_end_catch (skipped)'),
}


def _no_handler(gen, a):
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    want, code, what = NO_HANDLER_SITES[a]
    assert w == want, hex(w)
    return f'{{ {code} }}  /* {a:#x}: {what} */'


for _a in NO_HANDLER_SITES:
    PATCHES[_a] = _no_handler


# Mirror mode: the HUD update places the in-world action prompt (main+0xeaa800: stp s2, s0, [x8, #0x30] = the
# pane's translation x, y from the target's screen spot); x is mirrored there for that pane (runtime/hle/mirror.c).
PROMPT_POS_SITE = 0x7000eab138


def _prompt_pos(gen, a):
    w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
    assert w == 0x2d060102, hex(w)  # stp s2, s0, [x8, #0x30]
    return ('{ extern float rt_prompt_x(uint64_t pane, float x); float px_, py_; memcpy(&px_, &c->v[2], 4); '
            'memcpy(&py_, &c->v[0], 4); px_ = rt_prompt_x(c->x[8], px_); '
            'memcpy((void *)(uintptr_t)(c->x[8] + 0x30), &px_, 4); memcpy((void *)(uintptr_t)(c->x[8] + 0x34), &py_, 4); }'
            f'  /* {a:#x}: stp s2, s0, [x8, #0x30] (action prompt spot: mirror mode) */')


PATCHES[PROMPT_POS_SITE] = _prompt_pos
