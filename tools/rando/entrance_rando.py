#!/usr/bin/env python3
"""Entrance randomizer for the Link's Awakening remake (LAS Recomp addition on top of LAS-Randomizer).

Doors are overworld loading zones (actor 0x190 in Field_* rooms) leading into another level; each is paired
with the interior's zone that leads back out to it. A shuffle maps every door to another door's interior:
  coupled   - entering door D leads into T(D)'s interior, and that interior's exit leads back out of D
  decoupled - entries and exits are shuffled independently (exiting may put you somewhere else entirely)

Tiers (cumulative): 1 dungeons, 2 + single-exit interiors, 3 + connector caves, 4 everything in one pool.
Logic: see make_logic() -- interiors get synthetic regions whose access is the access of whatever door
leads there (tools/rando/entrance_data.yml supplies door requirements and inside conditions).
"""
import json
import os
import random
import re

HERE = os.path.dirname(os.path.abspath(__file__))
ZONES = os.path.join(HERE, 'loading_zones.json')

# never shuffled: the Egg (end game), and doors whose loading depends on event state
FIXED_LEVELS = {'Lv09WindFishsEgg', 'DreamShrine', 'Ending', 'FishingPond', 'TownWell'}
DUNGEON_PREFIX = 'Lv'


def base_room(r):
    m = re.match(r'(.+_\d\d[A-Z])', r)
    return m.group(1) if m else r


class Door:
    def __init__(self, z):
        self.zone = z                        # outer zone (in a Field room)
        self.room = z['room']                # e.g. Field_14D
        self.index = z['index']              # loading-zone index within that room
        self.dest_level = z['params'][0]
        self.entry = list(z['params'])       # params that enter this door's interior
        self.exit_zone = None                # interior zone that leads back out here
        self.exit = None                     # params of that zone (lead to this door's outside)
        self.kind = None

    @property
    def id(self):
        return f'{self.room}#{self.index}'

    def __repr__(self):
        return f'<Door {self.id} -> {self.dest_level} ({self.kind})>'


def load_doors():
    zones = json.load(open(ZONES))
    by_level = {}
    for z in zones:
        by_level.setdefault(z['level'], []).append(z)
    doors = []
    for z in by_level.get('Field', []):
        if z['params'][0] in ('Field', '') or z['params'][0] in FIXED_LEVELS:
            continue
        doors.append(Door(z))
    # pair each door with the interior zone whose destination is this door's room
    for d in doors:
        cands = [z for z in by_level.get(d.dest_level, []) if z['params'][0] == 'Field' and base_room(z['params'][1]) == d.room]
        if cands:
            d.exit_zone = cands[0]
            d.exit = list(cands[0]['params'])
    # classify
    outs = {}
    for lv, zs in by_level.items():
        if lv == 'Field':
            continue
        outs[lv] = {base_room(z['params'][1]) for z in zs if z['params'][0] == 'Field'}
    for d in doors:
        if d.dest_level.startswith(DUNGEON_PREFIX):
            d.kind = 'dungeon' if len(outs.get(d.dest_level, ())) <= 1 or d.dest_level in ('Lv06FaceShrine', 'Lv08TurtleRock') else 'connector'
            if d.dest_level in ('Lv06FaceShrine', 'Lv08TurtleRock'):
                d.kind = 'dungeon-multi'
        elif len(outs.get(d.dest_level, ())) <= 1:
            d.kind = 'single'
        else:
            d.kind = 'connector'
        if d.exit is None:
            d.kind = 'oneway'
    return doors


def shuffle(doors, tier, decoupled, rng):
    """Returns (entry_map, exit_map): door id -> door id whose interior is entered / whose outside is exited to."""
    pools = {}
    for d in doors:
        if d.kind == 'oneway':
            continue
        if d.kind == 'dungeon-multi':
            continue  # multi-entrance dungeons (Face Shrine, Turtle Rock) stay put for now
        if tier >= 4:
            key = 'all'
        elif d.kind == 'dungeon' and tier >= 1:
            key = 'dungeon'
        elif d.kind == 'single' and tier >= 2:
            key = 'single'
        elif d.kind == 'connector' and tier >= 3:
            key = 'connector'
        else:
            continue
        pools.setdefault(key, []).append(d.id)
    entry_map, exit_map = {}, {}
    for key, ids in pools.items():
        targets = ids[:]
        rng.shuffle(targets)
        for a, b in zip(ids, targets):
            entry_map[a] = b
        if decoupled:
            outs = ids[:]
            rng.shuffle(outs)
            # exiting the interior of door b leads out of door c
            for b, c in zip(ids, outs):
                exit_map[b] = c
        else:
            for a, b in entry_map.items():
                if a in ids:
                    exit_map[b] = a
    return entry_map, exit_map


def apply(doors, entry_map, exit_map, read_room, write_room):
    """Rewrite loading zones. read_room(name) -> leb.Room, write_room(name, room)."""
    by_id = {d.id: d for d in doors}
    edits = {}  # room name -> {zone index: params}
    for a, b in entry_map.items():
        da, db = by_id[a], by_id[b]
        edits.setdefault(da.room, {})[da.index] = db.entry
    for b, c in exit_map.items():
        db, dc = by_id[b], by_id[c]
        if db.exit_zone is None or dc.exit is None:
            continue
        edits.setdefault(db.exit_zone['room'], {})[db.exit_zone['index']] = dc.exit
    for room_name, zone_edits in edits.items():
        room = read_room(room_name)
        zones = [a for a in room.actors if a.type == 0x190]
        for idx, params in zone_edits.items():
            if idx >= len(zones):
                continue
            for i, p in enumerate(params):
                zones[idx].parameters[i] = p.encode('utf-8') if isinstance(p, str) else p
        write_room(room_name, room)
    return len(edits)


def spoiler(doors, entry_map, exit_map):
    by_id = {d.id: d for d in doors}
    lines = ['Entrances:']
    for a in sorted(entry_map):
        b = entry_map[a]
        lines.append(f'  {a:14} ({by_id[a].dest_level:24}) -> {by_id[b].dest_level}')
    if any(exit_map[b] != a for a, b in entry_map.items() if b in exit_map):
        lines.append('Exits (decoupled):')
        for b in sorted(exit_map):
            lines.append(f'  leaving {by_id[b].dest_level:24} -> outside {exit_map[b]}')
    return '\n'.join(lines)


if __name__ == '__main__':
    ds = load_doors()
    from collections import Counter
    print(Counter(d.kind for d in ds))
    em, xm = shuffle(ds, 2, False, random.Random(1))
    print(spoiler(ds, em, xm)[:1500])
