#!/usr/bin/env python3
"""Digsanity (LAS Recomp addition): every diggable tile of Koholint's overworld is a check - thousands of them.

Dig spots: each Field screen's .leb grid (10 x 8 tiles of 1.5 units) marks soft ground with the tile flag LAS calls
'isdigspot' (byte 2 bit 0); the shovel snaps its hole to the centre of the tile in front of Link. Tiles that also
carry collision are left out. The flag alone over-counts (about a quarter of those tiles are house fronts, cliff bases,
doorways: 3D collision the grid doesn't know), so checks are the tiles the in-game dig test proved diggable
(dig_tiles.txt, see VERIFIED). Grass and bushes on a tile have to be cut first (logic: sword / break-bush).

The item comes up out of the hole: the runtime (runtime/hle/digs.c) sees the hole the shovel makes and spawns the
tile's item there as a SinkingSword item actor (the actor LAS uses for standing items, pots and bushes), so picking
it up runs LAS's own item-get flow - Link holds the item up, the game's message box, traps, progressive items. Its
flow entries are added here to SinkingSword.bfevfl, one per distinct item (shared by every tile holding it: the flow
file's load buffer is limited). Dug tiles are event flags 4096 + tile id, picked-up items 24576 + tile id, in the
runtime's extended flag store (runtime/hle/flags_ext.c): holes stay open and items not picked up yet come back.

rando/<seed>/digs.tsv: screen, tile, ground height, item name, then for a check its model path, model name, flow entry,
seashell (1/0), scale and turn (degrees) ('-' on a tile that isn't a check), and what stands on the tile (grass / bush:
the runtime keeps it from growing back once the tile is dug, so the hole stays).

Logic: the shovel AND the logic regions of the tile's screen (the same screen regions as bushsanity: bushes.py).
Filler: what digging turns up in the original game - rupees and hearts mostly, a few refills.
"""
import glob
import os

import bushes
import pots
import spoiler_view

FLAG_BASE = 4096  # runtime/hle/digs.c DIG_FLAG_BASE: flag of tile g = FLAG_BASE + g
HERE = os.path.dirname(os.path.abspath(__file__))
# Tiles proven diggable in game (the dig test, LARECOMP_DIG_TEST, dug a hole there): the grid flag alone isn't enough
# - walls, cliff bases and doorways are 3D collision the grid doesn't know about. Tile ids only (no game data).
VERIFIED = os.path.join(HERE, 'dig_tiles.txt')
# (own rupee items: LAS's trapsanity turns every rupee-20 - and with blupsanity every rupee-5 - into a trap, which
# would make thousands of dig spots traps)
JUNK_MIX = [('dig-rupee-1', 0.42), ('dig-rupee-5', 0.18), ('pot-heart', 0.26), ('dig-rupee-20', 0.06),
            ('pot-arrows', 0.03), ('pot-bombs', 0.03), ('pot-powder', 0.02)]
FILLER_DEFS = {
    'dig-rupee-1': {'item-key': 'Rupee1', 'model-path': 'ItemRupeeGreen.bfres', 'model-name': 'RupeeGreen',
                    'npc-key': 'ItemRupeeGreen'},
    'dig-rupee-5': {'item-key': 'Rupee5', 'model-path': 'ItemRupeeBlue.bfres', 'model-name': 'RupeeBlue',
                    'npc-key': 'ItemRupeeBlue'},
    'dig-rupee-20': {'item-key': 'Rupee20', 'model-path': 'ItemRupeeRed.bfres', 'model-name': 'RupeeRed',
                     'npc-key': 'ItemRupeeRed'},
}
ITEM_FLAG = 'LARDigItem'  # the spawned item actors' flag parameter: never set (the runtime knows what's picked up)
NAMES = {'dig-rupee-1': '1 Rupee', 'dig-rupee-5': '5 Rupees', 'dig-rupee-20': '20 Rupees', 'pot-heart': 'Recovery Heart',
         'pot-arrows': 'Arrows', 'pot-bombs': 'Bombs', 'pot-powder': 'Magic Powder'}


# actors standing on a tile: these don't keep the shovel out (enemies, markers, items lying there); a bush (ObjGrass)
# or tall grass (ObjLawn) has to be cut first (checked in game: the shovel hits nothing on uncut grass); anything else
# (rocks, statues, signs, NPCs, scenery) - no check there
OPEN_ACTORS = ('Enemy', 'Tag', 'Area', 'Item', 'ObjButterfly')
LAWN_TYPE = 0xb5  # ObjLawn
CUT = {'bush': 'break-bush', 'grass': 'sword'}  # LAS logic: what clears the tile first
RANK = {'grass': 1, 'bush': 2, 'other': 3}


def _blockers(room, screen):
    """{tile: 'bush' | 'grass' | 'other'} for the tiles of a screen something stands on."""
    row, col = screen
    out = {}
    for a in room.actors:
        tx, tz = int((a.posX - col * 15.0) // 1.5), int((a.posZ - (row - 1) * 12.0) // 1.5)
        if not (0 <= tx < 10 and 0 <= tz < 8):
            continue
        name = a.name.decode('latin1')
        kind = (None if name.startswith(OPEN_ACTORS) else 'bush' if a.type == bushes.BUSH_TYPE
                else 'grass' if a.type == LAWN_TYPE else 'other')
        if kind and RANK[kind] > RANK.get(out.get(tz * 10 + tx), 0):
            out[tz * 10 + tx] = kind
    return out


def _rooms(romfs, leb):
    for f in sorted(glob.glob(os.path.join(romfs, 'region_common', 'level', 'Field', 'Field_*.leb'))):
        name = os.path.basename(f)[:-4]
        screen = bushes._screen_pos(name)
        if screen is None:
            continue
        with open(f, 'rb') as fh:
            room = leb.Room(fh.read(), edit_grid=True)
        if room.grid is not None and room.grid.info.room_height == 8:
            yield name, screen, room


def scan(romfs, leb):
    """Every dig tile of the Field: dicts with room ('Field_10D'), tile (0-79, row-major), y (ground height) and
    blocker (None, 'bush' / 'grass': cut it first, 'other': not a check)."""
    out = []
    for name, screen, room in _rooms(romfs, leb):
        blocked = _blockers(room, screen)
        for i, t in enumerate(room.grid.tilesdata):
            if t.flags3['isdigspot'] and not t.flags1['containscollision'] and not t.flags1['deepwaterlava']:  # (pits)
                out.append({'room': name, 'tile': i, 'y': t.elevation, 'blocker': blocked.get(i)})
    return out


def tile_id(d):
    row, col = bushes._screen_pos(d['room'])
    return ((row - 1) * 16 + col) * 80 + d['tile']


def location_name(d):
    return f"dig-{d['room'].split('_')[-1]}-{d['tile']:02d}"


def verified_tiles():
    """Tile ids from dig_tiles.txt (None without the file: every dig tile counts)."""
    if not os.path.exists(VERIFIED):
        return None
    out = set()
    for line in open(VERIFIED):
        if not line.startswith('#'):
            out.update(int(v) for v in line.split())
    return out


def add_to_logic(digs, logic_defs, item_defs):
    """A location per proven dig tile in LAS's logic + matching filler in the pool. Returns {location: dig}."""
    regions = bushes.screen_regions()
    proven = verified_tiles()
    spoiler_of = {}
    for k, v in logic_defs.items():
        if v.get('type') == 'item' and v.get('region') and v.get('spoiler-region'):
            spoiler_of.setdefault(v['region'], v['spoiler-region'])
    locs = {}
    for d in digs:
        regs = sorted(r for r in regions.get(d['room'], ()) if r in logic_defs)
        if not regs or d['blocker'] == 'other' or (proven is not None and tile_id(d) not in proven):
            continue
        name = location_name(d)
        logic_defs[name] = {'type': 'item', 'subtype': 'standing', 'content': 'rupee-5', 'region': regs[0],
                            'spoiler-region': spoiler_of.get(regs[0], 'mabe-village'),
                            'condition-basic': pots._and(*regs[1:], 'shovel',
                                                         CUT.get(d['blocker']))}
        locs[name] = d
    for k, v in list(pots.FILLER_DEFS.items()) + list(FILLER_DEFS.items()):
        item_defs.setdefault(k, dict(v, type='junk', quantity=0, **{'chest-type': 'junk'}))
    left = n = len(locs)
    for i, (item, frac) in enumerate(JUNK_MIX):
        k = left if i == len(JUNK_MIX) - 1 else int(round(n * frac))
        item_defs[item]['quantity'] += k
        left -= k
    return locs


def apply(locs, digs, mods, out_dir, romfs, event_tools, oead_tools, item_get, data):
    """Flow entries for the dug-up items (SinkingSword.bfevfl), their actor flag, and rando/<seed>/digs.tsv."""
    common_out = os.path.join(out_dir, 'romfs', 'region_common')

    def path(rel):
        p = os.path.join(common_out, rel)
        return p if os.path.exists(p) else os.path.join(romfs, 'region_common', rel)

    def out(rel):
        p = os.path.join(common_out, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        return p

    sheet = oead_tools.readSheet(path('datasheets/GlobalFlags.gsheet'))
    used = {int(v['index']) for v in sheet['values']}
    free = max(i for i in range(2048, 4096) if i not in used)
    sheet['values'].append({'symbol': ITEM_FLAG, 'index': free})
    oead_tools.writeSheet(out('datasheets/GlobalFlags.gsheet'), sheet)
    flow = event_tools.readFlow(path('event/SinkingSword.bfevfl'))
    entries = {}
    by_tile = {tile_id(d): name for name, d in locs.items()}
    lines, spoiler = ['# screen\ttile\ty\titem\tmodel\tname\tentry\tshell\tscale\tturn\tblocker (tools/rando/digs.py)\n'], []
    for d in sorted(digs, key=tile_id):
        name = by_tile.get(tile_id(d))
        cols = ['-'] * 7
        if name:
            item = mods.placements[name]
            item_key, item_index, model_path, model_name = mods.getItemInfo(name, mods.trap_models)
            quick = item_key[:3] == 'Rup' or item in pots.QUICK or item in FILLER_DEFS  # (no item-get: like rupees)
            key = (item_key, item_index, quick)
            if key not in entries:  # one entry per distinct item, shared by every tile holding it
                entries[key] = entry = f'LARDig{len(entries)}'
                if quick:
                    get = event_tools.createActionEvent(flow.flowchart, 'Inventory', 'AddItemByKey',
                                                        {'itemKey': item_key, 'count': 1, 'index': item_index,
                                                         'autoEquip': False})
                else:
                    get = item_get.insertItemGetAnimation(flow.flowchart, item_key, item_index)
                event_tools.addEntryPoint(flow.flowchart, entry)
                event_tools.insertEventAfter(flow.flowchart, entry,
                                             event_tools.createActionEvent(flow.flowchart, 'SinkingSword', 'Destroy', {}, get))
            label = NAMES.get(item) or spoiler_view.pretty_item(item)
            cols = [label, model_path, model_name, entries[key], '1' if item_key == 'Seashell' else '0',
                    f"{data.MODEL_SIZES.get(model_name, 1.0):g}", f"{data.MODEL_ROTATIONS.get(model_name, 0):g}"]
            spoiler.append(f'  {name:34} {item}')
        lines.append('\t'.join([d['room'].split('_')[-1], str(d['tile']), f"{d['y']:g}"] + cols + [d['blocker'] or '-']) + '\n')
    event_tools.writeFlow(out('event/SinkingSword.bfevfl'), flow)
    with open(os.path.join(out_dir, 'digs.tsv'), 'w') as f:
        f.writelines(lines)
    open(os.path.join(out_dir, 'flags_ext'), 'w').write('digsanity / bushsanity: event flags 2048+ (runtime/hle/flags_ext.c)\n')
    return spoiler


def test_list(romfs, leb, out_path):
    """Dig test list (runtime/hle/digs.c LARECOMP_DIG_TEST) for every possible check: tile id, C when grass / a bush
    has to be cut first, the nearest Field locator (the test warps there first; the list is grouped by it), then each
    free walkable neighbour tile at the same height as x,z,direction (N/S/E/W toward the tile; N = -z)."""
    grids, blocked, starts = {}, {}, []
    for name, screen, room in _rooms(romfs, leb):
        grids[screen] = room.grid.tilesdata
        blocked[screen] = _blockers(room, screen)
        # (plain Field_ locators: warping to the Manbo warp points or the swim-in at 15K crashed the test now and then)
        starts += [(a.posX, a.posZ, a.parameters[0].decode()) for a in room.actors
                   if a.name.startswith(b'TagPlayerStart') and isinstance(a.parameters[0], bytes)
                   and a.parameters[0].startswith(b'Field_') and a.parameters[0] != b'Field_15K']

    def free(gx, gz, y):  # global tile coordinates (0..159, 0..127)
        screen = (gz // 8 + 1, gx // 10)
        t = grids.get(screen)
        t = t and t[(gz % 8) * 10 + gx % 10]
        return (t and not t.flags1['containscollision'] and not t.flags3['iswaterlava'] and not t.flags1['deepwaterlava']
                and abs(t.elevation - y) < 0.3 and blocked[screen].get((gz % 8) * 10 + gx % 10) in (None, 'grass'))

    rows = []
    for d in scan(romfs, leb):
        if d['blocker'] == 'other':
            continue
        row, col = bushes._screen_pos(d['room'])
        gx, gz = col * 10 + d['tile'] % 10, (row - 1) * 8 + d['tile'] // 10
        x, z = gx * 1.5 + 0.75, gz * 1.5 + 0.75
        warp = min(starts, key=lambda s: (s[0] - x) ** 2 + (s[1] - z) ** 2)[2]  # nearest Field locator
        stands = [f"{x + dx * 1.5:g},{z + dz * 1.5:g},{face}"
                  for dx, dz, face in ((1, 0, 'W'), (-1, 0, 'E'), (0, 1, 'N'), (0, -1, 'S')) if free(gx + dx, gz + dz, d['y'])]
        rows.append((warp, tile_id(d), f"{tile_id(d)} {'C' if d['blocker'] in CUT else '-'} {warp} {' '.join(stands)}\n"))
    with open(out_path, 'w') as f:
        f.writelines(r[2] for r in sorted(rows))
    return len(rows)


def write_verified(results):
    """dig_tiles.txt from dig test outputs (their 'DUG <tile>' lines)."""
    tiles = sorted({int(line.split()[1]) for f in results for line in open(f) if line.startswith('DUG ')})
    with open(VERIFIED, 'w') as f:
        f.write('# Field tiles the shovel digs (tile id = ((row - 1) * 16 + column) * 80 + tile): the in-game dig test\n'
                '# (runtime/hle/digs.c LARECOMP_DIG_TEST) dug a hole on each; tools/rando/digs.py --verified <results>\n')
        for i in range(0, len(tiles), 16):
            f.write(' '.join(str(t) for t in tiles[i:i + 16]) + '\n')
    return len(tiles)


if __name__ == '__main__':  # digs.py --test-list <romfs> <out> | --verified <test results...>
    import sys
    import types
    sys.modules['PySide6'] = types.ModuleType('PySide6')
    sys.modules['PySide6.QtCore'] = types.ModuleType('PySide6.QtCore')
    sys.path.insert(0, os.path.normpath(os.path.join(HERE, '..', '..', 'third_party', 'LAS-Randomizer')))
    from RandomizerCore.Tools import leb as _leb
    if sys.argv[1] == '--test-list':
        print(test_list(sys.argv[2], _leb, sys.argv[3]), 'tiles')
    elif sys.argv[1] == '--verified':
        print(write_verified(sys.argv[2:]), 'proven dig tiles ->', VERIFIED)
