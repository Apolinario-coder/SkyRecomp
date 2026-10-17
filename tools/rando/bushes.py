#!/usr/bin/env python3
"""Bushsanity (LAS Recomp addition, user 2026-10-08): every bush on Koholint's overworld (ObjGrass 0xb6 - the leafy
shrubs, model parts GrassBase / GrassTop; 381 on the Field) hides an item, like the pots (pots.py does the writing: a
scaled-down SinkingSword item actor inside the bush, cutting it reveals the item). Tall grass (ObjLawn 0xb5, 916 on
the Field - the names are misleading; checked by tinting each kind gold in game 2026-10-09) is NOT randomized, nor
are the 73 bushes over a hole / staircase (scan(): the item fell in) - 308 checks.
Unchecked bushes are drawn gold (runtime/hle/pot_glow.c reads bushes.tsv; bushes are drawn instanced, the renderer
splits those draws).

Flags: bush flags are numbered from 2048 up (the pots fill most of the game's 2048 GlobalFlags) and the runtime
extends the flag bitfield (runtime/hle/flags_ext.c, turned on by the seed's `flags_ext` file).

Logic: LAS's break-bush (sword | powders | bracelet | bombs | rod | boomerang) AND the logic regions of the bush's
screen. A screen's regions come from the LAS locations on it (location_rooms.json) and the house / cave doors on it
(entrance_data.yml); a screen with neither takes the regions of the nearest screens that have some - all of them
must be reachable (conservative: a bush never counts as reachable before the player can get there).
"""
import glob
import json
import os
import re
import struct

import pots

HERE = os.path.dirname(os.path.abspath(__file__))

BUSH_TYPE = 0xb6  # ObjGrass (bushes; ObjLawn 0xb5 is the tall grass)
FLAG_BASE = int(os.environ.get('LARECOMP_BUSH_FLAG_BASE', 2048))  # (env: experiments)
FLAG_LIMIT = int(os.environ.get('LARECOMP_BUSH_FLAG_LIMIT', 4096))  # runtime/hle/flags_ext.c: 512-byte bitfield
# what bushes give in the original game: hearts and rupees mostly (ItemDrop 'Grass'), a few refills
JUNK_MIX = [('pot-heart', 0.32), ('rupee-5', 0.34), ('rupee-20', 0.10), ('rupee-50', 0.03),
            ('pot-arrows', 0.08), ('pot-bombs', 0.08), ('pot-powder', 0.05)]
BUSH_TRAPS = pots.POT_TRAPS
HIDE_SCALE = float(os.environ.get('LARECOMP_BUSH_HIDE', 0.4))  # smaller than in pots: leaves don't cover a 0.7 item


def _pit_tiles(room):
    """{(col, row)} of the room's tiles Link (and a dropped item) falls into.
    Room grid (fixed-hash entry 'grid': 'info' = u16 rows, u16 cols, then f32 x, z of the tile grid's corner at +8;
    'data' = 0x10 bytes per tile, row-major, 1.5 units square): +0 flags (bit 0 fall / sink - a pit, or deep water /
    lava when +2 bit 2 is set too; bit 1 collision; bits 3 / 5 / 7 north / east / south walls), +1 (bit 1 west wall),
    +2 (bit 0 dig spot, bit 2 water / lava, bit 3 respawn point / doorway floor, bit 5 refreshes on reload), +3 (0x80),
    +4..+7 a copy of +0..+3, +8 u32 chain index, +0xC f32 floor height. LAS's leb.Grid names most of these bits."""
    g = next((e for e in room.fixed_hash.entries if e.name == b'grid'), None)
    g = g and {x.name: x for x in g.data.entries}
    if not g or b'info' not in g or b'data' not in g:
        return set(), (0.0, 0.0)
    rows, cols = struct.unpack_from('<HH', g[b'info'].data, 0)
    origin = struct.unpack_from('<2f', g[b'info'].data, 8)
    raw = g[b'data'].data
    return {(i % cols, i // cols) for i in range(rows * cols) if raw[0x10 * i] & 1}, origin


# bushes LAS's open-mabe setting removes (third_party/LAS-Randomizer mod_generator.openMabe): no check there
OPEN_MABE_REMOVED = {0x624A97005CD29205, 0x62000A005D15AC9E, 0x620015005D15AC9E}


def scan(romfs, leb, pits=False, open_mabe=False):
    """Every bush on the Field: list of dicts with level, room, key, n (index among the room's bushes).
    Bushes over a hole are left out (`pits`: kept) and stay vanilla bushes - a revealed item fell in: the 70 over a pit
    tile (Pothole Field Field_12H-14H, Toronbo 14E; each also has an ObjFieldTip<area> hole model at the bush) and the
    3 hiding a staircase (02H, 05K, 11K: the hole model with param 2 + an AreaLevelOpen; the tile isn't a pit - the
    item vanished into the stairs and the bush wasn't drawn gold). n counts every bush, so a bush's name doesn't
    depend on which others are left out."""
    limit = int(os.environ.get('LARECOMP_BUSH_MAX', 100000))  # (experiments)
    out = []
    for f in sorted(glob.glob(os.path.join(romfs, 'region_common', 'level', 'Field', 'Field_*.leb'))):
        room_name = os.path.basename(f)[:-4]
        with open(f, 'rb') as fh:
            room = leb.Room(fh.read())
        holes, (ox, oz) = _pit_tiles(room)
        tips = [(t.posX, t.posZ) for t in room.actors if t.name.startswith(b'ObjFieldTip')]
        n = 0
        for a in room.actors:
            only = os.environ.get('LARECOMP_BUSH_ROOMS')  # (experiments: comma-separated rooms)
            if a.type != BUSH_TYPE:
                continue
            pit = (int((a.posX - ox) // 1.5), int((a.posZ - oz) // 1.5)) in holes or \
                any(abs(x - a.posX) < 0.1 and abs(z - a.posZ) < 0.1 for x, z in tips)
            gone = open_mabe and a.key in OPEN_MABE_REMOVED
            if len(out) < limit and (not only or room_name in only.split(',')) and (pits or not pit) and not gone:
                out.append({'level': 'Field', 'room': room_name, 'key': a.key, 'n': n})
            n += 1
    return out


def _screen_pos(room):
    m = re.match(r'Field_(\d\d)([A-P])$', room)
    return (int(m.group(1)), ord(m.group(2)) - ord('A')) if m else None


def screen_regions():
    """{Field room: set of logic regions} for every Field screen (see the module docstring)."""
    known = {}
    for loc, v in json.load(open(os.path.join(HERE, 'location_rooms.json'))).items():
        if v.get('level') == 'Field' and v.get('room') and v.get('region'):
            known.setdefault(v['room'], set()).add(v['region'])
    import yaml
    for d in yaml.safe_load(open(os.path.join(HERE, 'entrance_data.yml')))['doors']:
        if d.get('field_room') and d.get('outer_region'):
            known.setdefault(d['field_room'], set()).add(d['outer_region'])
    kpos = {_screen_pos(r): s for r, s in known.items() if _screen_pos(r)}
    out = {}
    for row in range(1, 17):
        for col in range(16):
            room = f'Field_{row:02d}{chr(ord("A") + col)}'
            if (row, col) in kpos:
                out[room] = set(kpos[(row, col)])
                continue
            best, regs = None, set()
            for (r, c), s in kpos.items():
                dist = abs(r - row) + abs(c - col)
                if best is None or dist < best:
                    best, regs = dist, set(s)
                elif dist == best:
                    regs |= s
            out[room] = regs
    return out


def location_name(b):
    return f"bush-{b['room'].split('_')[-1]}-{b['n'] + 1}"


def add_to_logic(bushes, logic_defs, item_defs, traps='none', rng=None):
    """A location per bush in LAS's logic + matching filler in the pool. Returns {location: bush}."""
    regions = screen_regions()
    spoiler_of = {}
    for k, v in logic_defs.items():
        if v.get('type') == 'item' and v.get('region') and v.get('spoiler-region'):
            spoiler_of.setdefault(v['region'], v['spoiler-region'])
    locs = {}
    for b in bushes:
        regs = sorted(r for r in regions.get(b['room'], ()) if r in logic_defs)
        if not regs:
            continue
        name = location_name(b)
        cond = pots._and(*regs[1:], 'break-bush')
        logic_defs[name] = {'type': 'item', 'subtype': 'standing', 'content': 'rupee-5', 'region': regs[0],
                            'spoiler-region': spoiler_of.get(regs[0], 'mabe-village'), 'condition-basic': cond}
        locs[name] = b
    for k, d in pots.FILLER_DEFS.items():
        item_defs.setdefault(k, dict(d, type='junk', quantity=0, **{'chest-type': 'junk'}))
    n = len(locs)
    trap_kinds = [k for k in item_defs if k.endswith('-trap') and not k.startswith('zol')]
    n_traps = int(round(n * BUSH_TRAPS.get(traps, 0.0))) if trap_kinds else 0
    if n_traps:
        import random
        rng = rng or random.Random(n)
        for _ in range(n_traps):
            item_defs[rng.choice(trap_kinds)]['quantity'] += 1
    left = n - n_traps
    m = left
    for i, (item, frac) in enumerate(JUNK_MIX):
        k = left if i == len(JUNK_MIX) - 1 else int(round(m * frac))
        item_defs.setdefault(item, {'quantity': 0})
        item_defs[item]['quantity'] += k
        left -= k
    return locs


def apply(locs, mods, out_dir, romfs, leb, event_tools, oead_tools, item_get, data):
    lines = pots.apply(locs, mods, out_dir, romfs, leb, event_tools, oead_tools, item_get, data, flag_prefix='LARBush',
                       flag_base=FLAG_BASE, flag_limit=FLAG_LIMIT, key_tag=0x7B, hide_scale=HIDE_SCALE,
                       cut_flag=True)
    open(os.path.join(out_dir, 'flags_ext'), 'w').write('bushsanity: event flags 2048+ (runtime/hle/flags_ext.c)\n')
    return lines


def write_tsv(seed_dir, leb):
    return pots.write_tsv(seed_dir, leb, flag_prefix='LARBush', name='bushes.tsv', hide_scale=HIDE_SCALE)
