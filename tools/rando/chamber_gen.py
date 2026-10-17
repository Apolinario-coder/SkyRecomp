#!/usr/bin/env python3
"""Dungeon generator prototype: random Chamber Dungeon layouts from the game's own panel catalogue.

The Switch remake's Chamber Dungeon engine (Dampe) builds playable dungeons from "panels": single rooms taken from
every dungeon (romfs level/Panel*). Its data:
  datasheets/MapPieceList.gsheet   247 panels (tools/rando/map_pieces.json): mId, mKey (PanelLvXX_room),
      mDoorTypeArray [N, W, E, S] (0 = wall), mRoomType (1..15 = bitmask of open sides N=1 W=2 E=4 S=8,
      16 = entrance, 17 = Nightmare's Lair (boss door type 10), 24+ = side-view / special), mStairsType, mBoxType (1 = chest)
  datasheets/MapPieceTheme.gsheet  Dampe's challenges; mMapArray = 8x8 grid, cell = panel mId (high 16 bits =
      editor tile marks: chest / locked door / blocked)
Rules (game + Zelda Wiki "Chamber Dungeon"): one entrance, one Nightmare's Lair, every doorway connects to a
doorway (none onto walls or off the grid), enough chests for the locked doors; stairs chambers pair up through
2D passages (left out of this prototype). Door types (inferred from vanilla rooms): 2 doorway, 6 shutter,
5 locked/boss door, 7 cracked (bomb) wall, 9 counted as locked to be safe, 10 entrance doorway, 1 rare.
"""
import json
import os
import random

HERE = os.path.dirname(os.path.abspath(__file__))
N, W, E, S = 0, 1, 2, 3  # mDoorTypeArray order (verified in game: [N, W, E, S], same as the mRoomType bits)
DXY = {N: (0, -1), E: (1, 0), S: (0, 1), W: (-1, 0)}
OPP = {N: S, S: N, E: W, W: E}
LOCKED = {5, 9}
# door types that may face each other: the pairs adjacent rooms use in the original dungeons (counted from the
# panels' grid positions, seen at least twice). A type-9 door facing a type-6 shutter makes the engine bounce Link
# back (seen in game), so anything else is treated as a mismatch.
COMPAT = {frozenset(p) for p in ((2, 2), (2, 5), (2, 6), (5, 6), (5, 10), (6, 6), (7, 7), (9, 9), (1, 9), (2, 9))}


def compatible(a, b):
    return frozenset((a, b)) in COMPAT


def can_enter(p, side):
    """mFlag bits 0-3 (door order N, W, E, S) mark doors that can't be entered: one-way shutters (e.g. a mini-boss
    room you may only enter from its original side). Entering through one gets Link pushed back out (seen in game)."""
    if p['room'] == NIGHTMARE:
        return True  # (flag 11 marks its only door; it's entered through the boss door as usual)
    return not (p['flag'] >> side) & 1
ENTRANCE, NIGHTMARE = 16, 17  # entrance = type 16 (e.g. Color Dungeon 08C), boss lair = 17 (door type 10)
GRID = 8


def load_pieces(path=os.path.join(HERE, 'map_pieces.json')):
    ps = json.load(open(path))
    usable = []
    for p in ps:
        if p['stairs'] or p['sideview'] >= 0 or p['room'] >= 24:
            continue  # stairs / side-view chambers need pairing (later)
        if any(d in (7,) for d in p['doors']):
            continue  # bombable walls need bombs (logic later)
        usable.append(p)
    return usable


def opens(p, side):
    return p['doors'][side] != 0


def generate(rng, rooms=14, pieces=None, tries=2000):
    pieces = pieces or load_pieces()
    entrances = [p for p in pieces if p['room'] == ENTRANCE]
    bosses = [p for p in pieces if p['room'] == NIGHTMARE]
    normal = [p for p in pieces if p['room'] < 16]
    for _ in range(tries):
        grid = {}
        used = set()
        e = rng.choice(entrances)
        ex, ey = rng.randrange(1, GRID - 1), rng.randrange(1, GRID - 1)
        if not fits(grid, ex, ey, e):
            continue
        grid[(ex, ey)] = e
        used.add(e['id'])
        boss_placed = False
        for _step in range(400):
            frontier = open_ends(grid)
            if not frontier:
                break
            x, y, side = rng.choice(frontier)
            nx, ny = x + DXY[side][0], y + DXY[side][1]
            want_close = len(grid) >= rooms
            cands = []
            pool = list(normal)
            if not boss_placed and len(grid) >= rooms // 2:
                pool += bosses
            for p in pool:
                if p['id'] in used or not opens(p, OPP[side]) or not can_enter(p, OPP[side]) or not fits(grid, nx, ny, p):
                    continue
                n_open = sum(1 for s in range(4) if opens(p, s))
                if want_close and n_open > 1 and p['room'] != NIGHTMARE:
                    continue
                cands.append(p)
            if not cands:
                break
            # prefer branching early, dead ends late; put the Nightmare in as soon as the dungeon is big enough
            boss_c = [p for p in cands if p['room'] == NIGHTMARE]
            if boss_c and len(grid) >= rooms * 2 // 3 and rng.random() < 0.5:
                p = rng.choice(boss_c)
            else:
                p = rng.choice([p for p in cands if p['room'] != NIGHTMARE] or cands)
            grid[(nx, ny)] = p
            used.add(p['id'])
            boss_placed |= p['room'] == NIGHTMARE
        if open_ends(grid) or not boss_placed:
            continue
        chests = sum(p['box'] for p in grid.values())
        locks = sum(1 for p in grid.values() for d in p['doors'] if d in LOCKED)
        if locks > chests:
            continue
        return grid
    return None


def fits(grid, x, y, p):
    if not (0 <= x < GRID and 0 <= y < GRID) or (x, y) in grid:
        return False
    for s in range(4):
        nx, ny = x + DXY[s][0], y + DXY[s][1]
        inside = 0 <= nx < GRID and 0 <= ny < GRID
        if not inside:
            if opens(p, s):
                return False
            continue
        q = grid.get((nx, ny))
        if q is not None:
            if opens(p, s) != opens(q, OPP[s]):
                return False
            if opens(p, s) and not compatible(p['doors'][s], q['doors'][OPP[s]]):
                return False
    return True


def open_ends(grid):
    out = []
    for (x, y), p in grid.items():
        for s in range(4):
            if opens(p, s) and (x + DXY[s][0], y + DXY[s][1]) not in grid:
                out.append((x, y, s))
    return out


def map_array(grid):
    arr = [0] * (GRID * GRID)
    for (x, y), p in grid.items():
        arr[y * GRID + x] = p['id']
    return arr


def render(grid):
    """ASCII picture: E entrance, B Nightmare, C chest, . room; doors drawn between cells."""
    lines = []
    for y in range(GRID):
        top, mid = '', ''
        for x in range(GRID):
            p = grid.get((x, y))
            if not p:
                top += '    '
                mid += '    '
                continue
            top += ' ' + ('|' if opens(p, N) else '-') + '  '
            c = 'E' if p['room'] == ENTRANCE else 'B' if p['room'] == NIGHTMARE else 'C' if p['box'] else 'o'
            mid += ('=' if opens(p, W) else '[') + c + ('=' if opens(p, E) else ']') + ' '
        lines += [top, mid]
    return '\n'.join(lines)


def tailcave_exit(seed_dir=None):
    """Where Tail Cave's exit door leads (level, locator) in a seed - entrance rando rewrites it - else vanilla."""
    import sys
    import types
    root = os.path.normpath(os.path.join(HERE, '..', '..'))
    las = os.path.join(root, 'third_party', 'LAS-Randomizer')
    sys.modules.setdefault('PySide6', types.ModuleType('PySide6'))
    sys.modules.setdefault('PySide6.QtCore', types.ModuleType('PySide6.QtCore'))
    if las not in sys.path:
        sys.path.insert(0, las)
    from RandomizerCore.Tools import leb
    rel = os.path.join('region_common', 'level', 'Lv01TailCave', 'Lv01TailCave_08D.leb')
    for base in ([os.path.join(seed_dir, 'romfs')] if seed_dir else []) + [os.path.join(root, 'extracted', 'romfs')]:
        p = os.path.join(base, rel)
        if os.path.exists(p):
            zones = [a for a in leb.Room(open(p, 'rb').read()).actors if a.type == 0x190]
            if zones:
                lv, loc = (x.decode() if isinstance(x, bytes) else str(x) for x in zones[0].parameters[:2])
                return lv, loc
    return 'Field', 'Field_14D'


def write_layout(grid, path, exit_=('Field', 'Field_14D')):
    """Runtime layout file (runtime/hle/chamber.c): 64 panel ids row-major (-1 = empty), the entrance cell, then
    `exit <level> <locator>` (where leaving the generated dungeon goes)."""
    ids = [-1] * (GRID * GRID)
    ent = -1
    for (x, y), p in grid.items():
        ids[y * GRID + x] = p['id']
        if p['room'] == ENTRANCE:
            ent = y * GRID + x
    with open(path, 'w') as f:
        for y in range(GRID):
            f.write(' '.join(f'{v:5d}' for v in ids[y * GRID:(y + 1) * GRID]) + '\n')
        f.write(f'{ent}\n')
        f.write(f'exit {exit_[0]} {exit_[1]}\n')


if __name__ == '__main__':
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument('seed', nargs='?', default='chamber')
    ap.add_argument('--rooms', type=int, default=14)
    ap.add_argument('--out', help='write the runtime layout file here')
    ap.add_argument('--seed-dir', help='seed folder (its Tail Cave exit, for shuffled entrances)')
    a = ap.parse_args()
    g = generate(random.Random(a.seed), rooms=a.rooms)
    if not g:
        print('no layout found')
        raise SystemExit(1)
    print(render(g))
    print(len(g), 'rooms:', ', '.join(p['key'].replace('Panel', '') for p in g.values()))
    if a.out:
        write_layout(g, a.out, tailcave_exit(a.seed_dir))
        print('wrote', a.out)
