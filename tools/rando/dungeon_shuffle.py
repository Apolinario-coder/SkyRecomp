#!/usr/bin/env python3
"""Generated dungeons v0: re-fill a dungeon's cells with real rooms from every dungeon (no Chamber stones).

The target dungeon keeps its cell layout (its .lvb: room bounds, cameras, starts stay valid). Rooms with a role
stay in place: entrance / boss / mini-boss / instrument / Nightmare door / anything holding items, keys, chests,
stairs or warps, and multi-cell rooms. Every other cell gets a plain top-down room from any dungeon such that
  * each shared edge matches (same opening tiles on both sides; door actors count as openings, rooms.py),
  * a side with no neighbouring cell has no opening (it would lead into the void),
  * no room is used twice (also not a room already in place).
Backtracking search, deterministic per seed. Plain rooms carry no items, so dungeon logic is unchanged.

    plan(cat, level, seed) -> {cell: source room}       (only the changed cells)
    python3 dungeon_shuffle.py <romfs> <out romfs> <level> [seed]   write the rooms + merged textures

Runs in the LAS venv (rooms.py).
"""
import json
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import rooms  # noqa: E402

ROLE_ACTORS = ('TreasureBox', 'SmallKey', 'NightmareKey', 'Stairs', 'Downstairs', 'Upstairs', 'Warp', 'Boss',
               'Degu', 'Rola', 'Hinox', 'Bossblin', 'Dodongo', 'Lanmola', 'Hiploop', 'Stalfon', 'Gohma', 'Jacky',
               'PiccoloMaster', 'EnemyTurtleRock', 'MightPunch', 'StoneHinox', 'BossBlob', 'DonPawn', 'PotDemon',
               'Angler', 'Hooker', 'Albatoss', 'Shadow', 'PlayerStart', 'Hole', 'Npc', 'Shop', 'FullMoon',
               'Instrument', 'ConchHorn', 'SeaLily', 'SurfHarp', 'WindMarimba', 'CoralTriangle', 'EveningCalm',
               'ThunderDrum', 'DungeonCo', 'Compass', 'Beak', 'Map', 'Tunic', 'Panel', 'Golden', 'HeartContainer')


BOSS_TYPES = set(range(0x51, 0x70))  # mini-bosses 0x51-0x61 and bosses (EnemyRola .. ), by actor type id
# per-dungeon environment switches (TagChangeEnv -> e.g. 'Lv02Base'): crash a level that lacks that environment
BOSS_TYPES |= {0x220}


def is_plain(info):
    if BOSS_TYPES & set(info.get('types', [])):
        return False  # (actor names in the catalog are truncated: match bosses by type id)
    return not any(k in a for a in info['actors'] for k in ROLE_ACTORS if not a.startswith('MapStatic'))


def plan(cat, level, seed, tries=200000):
    cells = {k: v for k, v in cat.items() if k.startswith(level + '_')}
    free = sorted(k for k, v in cells.items() if len(v['unit']) == 1 and is_plain(v))
    used = {k for k in cells if k not in free}  # rooms staying in place
    pool = sorted(k for k, v in cat.items() if len(v['unit']) == 1 and is_plain(v))
    rng = random.Random(f'{seed}-{level}')

    def sides(room):
        return {s: tuple(cat[room]['openings'][s]) for s in 'NSWE'}

    assign = {}

    def ok(cell, room):
        op = sides(room)
        for s in 'NSWE':
            nb = rooms.neighbour(cell, s)
            if nb in cells:
                other = assign.get(nb, nb if nb not in free else None)
                if other is not None and op[s] != tuple(cat[other]['openings'][rooms.OPP[s]]):
                    return False
            elif op[s]:
                return False
        return True

    order = sorted(free, key=lambda c: rng.random())
    budget = [tries]

    def solve(i):
        if i == len(order):
            return True
        budget[0] -= 1
        if budget[0] < 0:
            return False
        cell = order[i]
        cands = [r for r in pool if r not in used and r not in assign.values() and ok(cell, r)]
        rng.shuffle(cands)
        if cell not in assign.values() and ok(cell, cell):
            cands.append(cell)  # its own room as the last resort
        for r in cands:
            assign[cell] = r
            if solve(i + 1):
                return True
            del assign[cell]
        return False

    if not solve(0):
        return None
    return {c: r for c, r in assign.items() if c != r}


if __name__ == '__main__':
    romfs, out, level = sys.argv[1:4]
    seed = sys.argv[4] if len(sys.argv) > 4 else 'LARecomp'
    cat = json.load(open(os.path.join(HERE, 'room_catalog.json')))
    p = plan(cat, level, seed)
    if p is None:
        sys.exit('no layout found')
    for cell, src in sorted(p.items()):
        rooms.transplant(romfs, out, src, cell)
        print(f'{cell} <- {src}')
    print(len(p), 'rooms replaced')
