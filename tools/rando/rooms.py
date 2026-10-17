#!/usr/bin/env python3
"""Real dungeon rooms as building blocks for generated dungeons (no Chamber stones).

Every dungeon shares one room grid: a top-down room is a 15 x 12 cell, column letter -> x = 15 * col,
row -> z = 12 * (row - 1); its .leb has a 10 x 8 tile grid (1.5 units, origin = cell corner) with collision flags.

    catalog(romfs) -> {room: Room}            every top-down dungeon room with its openings per side
    transplant(romfs, out_romfs, src, dst)    put room `src` (e.g. 'Lv03KeyCavern_05B') into cell `dst`
                                              ('Lv01TailCave_04B'): actors / points / grid moved, the destination
                                              cell's own roof actor kept, the textures its model needs merged
                                              into the destination level's map bntx (tools/rando/bntx_merge.py)

Openings: edge tiles without collision are open passages; door actors standing on an edge are doors (their
tiles read as collision). A side's openings are the tile indices along it (west->east / north->south), so two
rooms connect where the indices match on the shared edge.

Findings behind this (notes/PLAN.md 2026-10-08): the moved room's own ObjDungeonRoof (black per-room fade cover)
must be replaced by the destination's or the level renders black; merging a whole dungeon's textures crashes the
load, only the referenced ones are merged. Side-view rooms sit off the grid and are skipped here.

Runs in the LAS venv.
"""
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', '..', 'third_party', 'LAS-Randomizer'))

from RandomizerCore.Tools import leb  # noqa: E402

DUNGEONS = ['Lv01TailCave', 'Lv02BottleGrotto', 'Lv03KeyCavern', 'Lv04AnglersTunnel', 'Lv05CatfishsMaw',
            'Lv06FaceShrine', 'Lv07EagleTower', 'Lv08TurtleRock', 'Lv10ClothesDungeon']
ROOF = 0x1B6
MAPSTATIC = 0x185
DOORS = {0xED: 'lock', 0xEE: 'bossdoor', 0xEF: 'shutter', 0x1A3: 'oneway', 0x1A4: 'oneway',  # ObjLockDoor,
         0x121: 'cracked', 0x123: 'cracked', 0x131: 'cracked', 0x132: 'cracked', 0x133: 'cracked',  # ObjBossDoor,
         0x139: 'cracked', 0x20c: 'cracked', 0x149: 'potdoor'}  # ObjClosedDoor, ObjOnewayDoor, ObjDungeonCrack*,
# ObjBrokenWall (bombable walls), ObjClosedPotDoor (a pot to lift in the doorway: Face Shrine 07F)
# things that can stand IN a doorway (puzzles): such a doorway is never joined by a door jump
BLOCKERS = {0x100: 'blocked:block', 0x1aa: 'blocked:crystal', 0x1ab: 'blocked:crystal', 0x122: 'blocked:statue',
            0xf7: 'blocked:chest'}


def cell_origin(room):
    rc = room.rsplit('_', 1)[1][:3]
    return (ord(rc[2]) - ord('A')) * 15.0, (int(rc[:2]) - 1) * 12.0


def level_of(room):
    return room.rsplit('_', 1)[0]


def _leb_path(romfs, room):
    return os.path.join(romfs, 'region_common', 'level', level_of(room), room + '.leb')


def _grid(r):
    for e in r.fixed_hash.entries:
        if e.name == b'grid':
            return {x.name: x for x in e.data.entries}
    return None


class Room:
    def __init__(self, name, tiles, openings, doors, actors):
        self.name, self.tiles, self.openings, self.doors, self.actors = name, tiles, openings, doors, actors

    def __repr__(self):
        return f'<{self.name} N{self.openings["N"]} S{self.openings["S"]} W{self.openings["W"]} E{self.openings["E"]}>'


def load_room(romfs, name):
    data = open(_leb_path(romfs, name), 'rb').read()
    r = leb.Room(data)
    g = _grid(r)
    if not g or b'info' not in g:
        return None
    h, w = struct.unpack_from('<HH', g[b'info'].data, 0)
    ox, oz = struct.unpack_from('<2f', g[b'info'].data, 8)
    if (h, w) != (8, 10) or (ox, oz) != cell_origin(name):
        return None  # side-view / off-grid
    raw = g[b'data'].data
    # an edge tile is part of a walkable doorway when it has no collision or carries the floor flag (byte 2 bit 3;
    # LAS calls it 'respawnvoid': it marks the 2 tiles of every doorway, door or not, even where they read as
    # collision) - but not deep water / lava (byte 2 bit 2 with elevation < -0.3; shallow puddles are -0.05):
    # walking into a room through Face Shrine 06G's north water channel sinks Link and he respawns in the room he
    # came from (deep water -1.5, Turtle Rock lava -0.38)
    def blocked(row, col):
        o = 0x10 * (row * 10 + col)
        t = raw[o:o + 3]
        h = struct.unpack_from('<f', raw, o + 12)[0]
        if t[2] & 4 and h < -0.3:
            return True

        return bool(t[0] & 2) and not t[2] & 8
    coll = [[blocked(row, col) for col in range(10)] for row in range(8)]

    def height(row, col):
        return round(struct.unpack_from('<f', raw, 0x10 * (row * 10 + col) + 12)[0], 1)
    edge_h = {'N': [height(0, c) for c in range(10)], 'S': [height(7, c) for c in range(10)],
              'W': [height(rw, 0) for rw in range(8)], 'E': [height(rw, 9) for rw in range(8)]}
    op = {'N': [c for c in range(10) if not coll[0][c]], 'S': [c for c in range(10) if not coll[7][c]],
          'W': [rw for rw in range(8) if not coll[rw][0]], 'E': [rw for rw in range(8) if not coll[rw][9]]}
    tile_open = {k: list(v) for k, v in op.items()}
    doors = []
    # water at a doorway (byte 2 bit 2, any depth): Link swims through it, and the game ignores position writes
    # while he swims - such a doorway can't take a door jump (Catfish's Maw 04F north: refused every time)
    edge = {'N': lambda i: (0, i), 'S': lambda i: (7, i), 'W': lambda i: (i, 0), 'E': lambda i: (i, 9)}
    for side, tiles_ in op.items():
        for i in tiles_:
            row, col = edge[side](i)
            if raw[0x10 * (row * 10 + col) + 2] & 4:
                doors.append((side, i, 'blocked:water', 2 * i + 1))
    for a in r.actors:
        if a.type in BLOCKERS:
            lx, lz = (a.posX - ox) / 1.5, (a.posZ - oz) / 1.5
            side = 'N' if lz < 1.0 else 'S' if lz > 7.0 else 'W' if lx < 1.0 else 'E' if lx > 9.0 else None
            if side:
                doors.append((side, int(lx) if side in 'NS' else int(lz), BLOCKERS[a.type],
                              round(2 * (lz if side in 'WE' else lx))))
        if a.type in DOORS:
            lx, lz = (a.posX - ox) / 1.5, (a.posZ - oz) / 1.5
            side = 'N' if lz < 1.2 else 'S' if lz > 6.8 else 'W' if lx < 1.2 else 'E' if lx > 8.8 else None
            if side:
                idx = int(lx) if side in 'NS' else int(lz)
                # centre in half tiles: doors sit between two tiles (4.0) or on one (4.5): Face Shrine 05B's west
                # shutter (4.5) and Catfish's Maw 07E's (4.0) both read as tile 4 - paired, Link hit the wall
                half = round(2 * (lz if side in 'WE' else lx))
                doors.append((side, idx, DOORS[a.type], half))
                for k in (idx - 1, idx, idx + 1):  # a door spans 2 tiles; record its centre tiles
                    rng = range(10) if side in 'NS' else range(8)
                    if k in rng and k not in op[side]:
                        pass
                op[side] = sorted(set(op[side]) | {idx})
    rm = Room(name, coll, op, doors, [(hex(a.type), a.name.split(b'-')[0].decode()) for a in r.actors])
    rm.edge_h, rm.tile_open = edge_h, tile_open
    return rm


OPP = {'N': 'S', 'S': 'N', 'W': 'E', 'E': 'W'}
STEP = {'N': (0, -1), 'S': (0, 1), 'W': (-1, 0), 'E': (1, 0)}


def neighbour(room, side):
    lv, rc = room.rsplit('_', 1)
    col, row = ord(rc[2]) - ord('A') + STEP[side][0], int(rc[:2]) + STEP[side][1]
    if not (0 <= col < 26 and 1 <= row <= 99):
        return None
    return f'{lv}_{row:02d}{chr(ord("A") + col)}'


def catalog(romfs):
    """Rooms with openings per side. A door actor sits in only one of the two rooms it joins (the other room's
    tiles there read as collision), so doors are mirrored onto the neighbour's opposite side. Cells joined by a
    wide open edge (>= 3 matching open tiles) form one multi-cell room: `unit` lists its cells (move together)."""
    out = {}
    for lv in DUNGEONS:
        d = os.path.join(romfs, 'region_common', 'level', lv)
        for f in sorted(os.listdir(d)):
            if f.endswith('.leb') and re.match(rf'{lv}_\d\d[A-Z]\.leb$', f):
                rm = load_room(romfs, f[:-4])
                if rm:
                    out[rm.name] = rm
    own = {k: list(v.doors) for k, v in out.items()}
    for rm in list(out.values()):
        for side, idx, kind, half in own[rm.name]:
            if kind.startswith('blocked'):
                continue
            nb = out.get(neighbour(rm.name, side))
            if nb:
                o = OPP[side]
                nb.openings[o] = sorted(set(nb.openings[o]) | {idx})
                nb.doors.append((o, idx, kind + '(mirror)', half))
    # units: union cells joined by wide openings
    parent = {k: k for k in out}

    def find(k):
        while parent[k] != k:
            parent[k] = parent[parent[k]]
            k = parent[k]
        return k
    for rm in out.values():
        for side in ('E', 'S'):
            nb = out.get(neighbour(rm.name, side))
            if nb and len(set(rm.openings[side]) & set(nb.openings[OPP[side]])) >= 3:
                parent[find(rm.name)] = find(nb.name)
    # ... and cells sharing one camera zone in their level: the game treats them as one room (the camera scrolls
    # along, no room transition between them) even across 1-tile doorways - Tail Cave 05A/06A/07A (chest, hallway,
    # stairs), Face Shrine 03G-06G, Turtle Rock 01E/02E, 05D-05F, 06D-06F (user 2026-10-08). Split apart, an edge
    # inside a zone isn't a room boundary to the game: door jumps from there were undone.
    import fixedhash
    for lv in DUNGEONS:
        p = os.path.join(romfs, 'region_common', 'level', lv, lv + '.lvb')
        if not os.path.exists(p):
            continue
        fh = fixedhash.FixedHash(open(p, 'rb').read())
        names = [e.name.decode() for e in fh.get(b'area').data.entries]
        for z in fh.get(b'zone').data.entries:
            n = z.data[0x9c + 6]
            cells = [names[i] for i in z.data[0x9c + 7:0x9c + 7 + n] if i < len(names) and names[i] in out]
            for c in cells[1:]:
                parent[find(c)] = find(cells[0])
    groups = {}
    for k in out:
        groups.setdefault(find(k), []).append(k)
    for g in groups.values():
        for k in g:
            out[k].unit = sorted(g)
    return out


def slots(tiles):
    """Openings as doorway positions: a door actor stands on the second tile of the 2-tile doorway it fills
    (centre doors: N/S tile 5, W/E tile 4), an open passage shows both tiles -> both read as that second tile;
    a single open tile is taken as is; runs of 3+ tiles are wide openings (multi-cell rooms), kept as 'w<a>-<b>'."""
    out, run = [], []
    for t in sorted(set(tiles)) + [None]:
        if run and (t is None or t != run[-1] + 1):
            out.append(run[-1] if len(run) <= 2 else f'w{run[0]}-{run[-1]}')
            run = []
        if t is not None:
            run.append(t)
    return out


def half_slots(rm, side):
    """Doorway centres on a side in HALF tiles (2 x tile coordinate): a 2-tile passage over tiles 3-4 -> 8, a
    1-tile one on tile 4 -> 9, a door actor -> 2 x its position. Rooms join where these are equal (doors in the
    two rooms half a tile apart don't line up). Door positions win over the tile run they stand in."""
    out = set()
    run = []
    for t in sorted(set(rm.tile_open[side])) + [None]:
        if run and (t is None or t != run[-1] + 1):
            out.add(2 * run[0] + 1 if len(run) == 1 else 2 * run[-1] if len(run) == 2 else f'w{run[0]}-{run[-1]}')
            run = []
        if t is not None:
            run.append(t)
    halves = {d[3] for d in rm.doors if d[0] == side and not d[2].startswith('blocked')}
    out = {x for x in out if isinstance(x, str) or all(abs(x - h) > 1 for h in halves)} | halves
    return sorted(out, key=lambda x: (isinstance(x, str), str(x) if isinstance(x, str) else x))


def to_json(cat):
    # heights: floor height at each opening (0 ground, 1.5 raised floors): doorways join only at equal height
    # (None at a door: its tiles carry the closed door's collision height)
    def slot_h(v, s, sl):
        if not isinstance(sl, str) and any(d[0] == s and d[3] == sl for d in v.doors):
            return None
        i = int(sl[1:].split('-')[0]) if isinstance(sl, str) else min(sl // 2, len(v.edge_h[s]) - 1)
        return v.edge_h[s][i]
    out = {}
    hs = {k: {s: half_slots(v, s) for s in 'NSWE'} for k, v in cat.items()}
    for k, v in cat.items():
        op = hs[k]
        # a doorway is real when the room it led to in the original dungeon has one at the same spot (or a door
        # there was mirrored onto it); an unmatched centre is floor running into the neighbour's wall (Key Cavern
        # 03A/03B, Face Shrine 05B east 2, ...). Wide openings (multi-cell rooms) and dungeon exits stay.
        for s in 'NSWE':
            nb = neighbour(k, s)
            if nb in hs:
                theirs = hs[nb][OPP[s]]
                op[s] = [sl for sl in op[s] if isinstance(sl, str) or sl in theirs]
        out[k] = {'openings': op, 'heights': {s: [slot_h(v, s, sl) for sl in op[s]] for s in 'NSWE'},
                  'tiles': v.openings, 'doors': v.doors, 'unit': v.unit,
                  'actors': sorted({n for _, n in v.actors}), 'types': sorted({int(t, 16) for t, _ in v.actors})}
    return out


def remove_actors(r, pred):
    """Remove the actors matching pred and keep every link valid: actors refer to each other by index in the
    room (relationship section 1 entries [[p1, p2], index] and section 3 index lists, e.g. a kill-all-enemies
    trigger -> its enemies, shutters -> that trigger), so later indices shift down and links to removed actors
    are dropped (removing a roof mid-list without this broke "open when all enemies are killed" rooms)."""
    keep = [i for i, a in enumerate(r.actors) if not pred(a)]
    new_index = {old: new for new, old in enumerate(keep)}
    r.actors = [r.actors[i] for i in keep]
    for a in r.actors:
        rel = a.relationships
        rel.section_1 = [[e[0], new_index[e[1]]] for e in rel.section_1 if e[1] in new_index]
        rel.section_3 = [new_index[i] for i in rel.section_3 if i in new_index]
        rel.x, rel.y = len(rel.section_1), len(rel.section_3)


def transplant(romfs, out_romfs, src, dst, merge_textures=True):
    """Writes out_romfs/region_common/level/<dstLv>/<dst>.leb (+ the merged map bntx). Returns the moved room."""
    sx, sz = cell_origin(src)
    tx, tz = cell_origin(dst)
    dx, dz = tx - sx, tz - sz
    r = leb.Room(open(_leb_path(romfs, src), 'rb').read())
    for a in r.actors:
        a.posX += dx
        a.posZ += dz
    for e in r.fixed_hash.entries:
        if e.name == b'point':
            for p in e.data.entries:
                x, y, z = struct.unpack_from('<3f', p.data, 0)
                p.data = struct.pack('<3f', x + dx, y, z + dz) + p.data[12:]
    g = _grid(r)
    if g and b'info' in g:
        info = bytearray(g[b'info'].data)
        struct.pack_into('<2f', info, 8, tx, tz)
        g[b'info'].data = bytes(info)
    # 'infomation' = CRC32 of the room's own name: the level finds the room's area by it (a moved room keeping its
    # source name's CRC makes the load throw std::out_of_range)
    import zlib
    for e in r.fixed_hash.entries:
        if e.name == b'infomation':
            e.data = struct.pack('<I', zlib.crc32(dst.encode()))
    dst_path = _leb_path(romfs, dst)
    # The roof / room-fade actor (black cover for rooms Link isn't in) is shaped for ONE room: the moved room's own
    # breaks the level (black screen), and the destination cell's original one is shaped for the room that used
    # to be there and blacks out parts of the new room and its neighbours (seen 2026-10-08: Turtle Rock 06A half
    # black next to a cell carrying Tail Cave's 05F fade). Transplanted rooms get none (opt-in: LARECOMP_DST_ROOF).
    # (2026-10-08) retest: with actor roomIDs restamped per cell (the real cause of the black screens), the moved
    # room's OWN roof may work -> LARECOMP_ROOF=src keeps it (moved with the room)
    keep_src = os.environ.get('LARECOMP_ROOF', 'src') == 'src'
    roof = []
    if keep_src:
        pass
    elif os.path.exists(dst_path) and os.environ.get('LARECOMP_DST_ROOF'):
        roof = [a for a in leb.Room(open(dst_path, 'rb').read()).actors if a.type == ROOF]
    elif os.environ.get('LARECOMP_ROOF_TEMPLATE'):  # (experiment) a new cell borrows another cell's roof actor
        tpl = os.environ['LARECOMP_ROOF_TEMPLATE']
        roof = [a for a in leb.Room(open(_leb_path(romfs, tpl), 'rb').read()).actors if a.type == ROOF]
        ox, oz = cell_origin(tpl)
        for a in roof:
            a.posX += tx - ox
            a.posZ += tz - oz
    if not keep_src:
        remove_actors(r, lambda a: a.type == ROOF)
    for a in roof:  # appended last: no index shifts; its links pointed into the destination's own room
        a.relationships.section_1, a.relationships.section_3 = [], []
        a.relationships.x = a.relationships.y = 0
    r.actors += roof
    out = os.path.join(out_romfs, 'region_common', 'level', level_of(dst), dst + '.leb')
    os.makedirs(os.path.dirname(out), exist_ok=True)
    open(out, 'wb').write(r.repack())
    if merge_textures and level_of(src) != level_of(dst):
        import bntx_merge
        model = [a.parameters[0] for a in r.actors if a.type == MAPSTATIC][0].decode()
        bf = open(os.path.join(romfs, 'region_common', 'map', model + '.bfres'), 'rb').read()
        refs = {m.group().decode() for m in re.finditer(rb'MI_[A-Za-z0-9_.]+', bf)}
        bpath = os.path.join(out_romfs, 'region_common', 'map', level_of(dst) + '.bntx')
        base = open(bpath, 'rb').read() if os.path.exists(bpath) else \
            open(os.path.join(romfs, 'region_common', 'map', level_of(dst) + '.bntx'), 'rb').read()
        other = open(os.path.join(romfs, 'region_common', 'map', level_of(src) + '.bntx'), 'rb').read()
        os.makedirs(os.path.dirname(bpath), exist_ok=True)
        open(bpath, 'wb').write(bntx_merge.merge(base, [other], only=refs))
    return r


if __name__ == '__main__':
    romfs = sys.argv[1]
    if len(sys.argv) > 3:
        transplant(romfs, sys.argv[2], sys.argv[3], sys.argv[4])
        print('ok')
    else:
        import json
        cat = catalog(romfs)
        units = {tuple(v.unit) for v in cat.values()}
        print(len(cat), 'top-down rooms,', len(units), 'units,', sum(len(u) > 1 for u in units), 'multi-cell')
        json.dump(to_json(cat), open(os.path.join(HERE, 'room_catalog.json'), 'w'), indent=1)
        for k, v in list(cat.items())[:8]:
            print(v, v.doors, v.unit)
