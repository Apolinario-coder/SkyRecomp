#!/usr/bin/env python3
"""Check-tracker data for a seed: rando/<seed>/tracker.json, read by runtime/ui/tracker_overlay.mm.

  nodes   LAS logic as the seed uses it (logic.json from generation, else base LAS logic + pots): name, type,
          region, condition (basic | advanced... per the seed's logic level), [pit]/[heavy] enemy variants
  checks  every item location: label, overworld map position (cell units: x = column 0..16, y = row 0..16),
          group (dungeon marker / house), flag (GlobalFlags index that is set once collected, -1 = unknown)
  groups  markers that stand for many checks (dungeons; houses with item pots)
  settings  boolean seed settings (logic tokens)
The runtime evaluates reachability from the live save's inventory (never from placements: no spoilers).
"""
import glob
import json
import os
import re
import sys
import types

import underwater  # (the sunken seashells' flags)

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
LAS = os.path.join(ROOT, 'third_party', 'LAS-Randomizer')
DUNGEONS = {'Lv01TailCave': 'D1', 'Lv02BottleGrotto': 'D2', 'Lv03KeyCavern': 'D3', 'Lv04AnglersTunnel': 'D4',
            'Lv05CatfishsMaw': 'D5', 'Lv06FaceShrine': 'D6', 'Lv07EagleTower': 'D7', 'Lv08TurtleRock': 'D8',
            'Lv10ClothesDungeon': 'D0'}
LEVELS = {'basic': ['basic'], 'advanced': ['basic', 'advanced'], 'glitched': ['basic', 'advanced', 'glitched'],
          'hell': ['basic', 'advanced', 'glitched', 'hell'], 'none': ['basic']}


def _las():
    sys.modules.setdefault('PySide6', types.ModuleType('PySide6'))
    sys.modules.setdefault('PySide6.QtCore', types.ModuleType('PySide6.QtCore'))
    if LAS not in sys.path:
        sys.path.insert(0, LAS)
    if HERE not in sys.path:
        sys.path.insert(0, HERE)


def cell_of_field(room):
    m = re.match(r'Field_(\d\d)([A-P])', room or '')
    return (ord(m.group(2)) - 65 + 0.5, int(m.group(1)) - 1 + 0.5) if m else None


def condition(node, levels):
    parts = [node.get('condition-' + lv) for lv in levels if node.get('condition-' + lv)]
    return ' | '.join(f'({p})' for p in parts) if parts else ''


def build(seed_dir, romfs):
    _las()
    import yaml
    from RandomizerCore.Tools import leb, oead_tools
    from RandomizerCore.Randomizers import data as las_data
    import pots as pots_mod
    settings = json.load(open(os.path.join(seed_dir, 'settings.json')))
    levels = LEVELS.get(settings.get('logic', 'basic'), ['basic'])
    lpath = os.path.join(seed_dir, 'logic.json')
    if os.path.exists(lpath):
        logic = json.load(open(lpath))
    else:  # seeds generated before logic.json existed: base LAS logic (+ pots)
        logic = yaml.safe_load(open(os.path.join(LAS, 'RandomizerCore', 'Data', 'logic.yml')).read().split('---', 1)[1])
        if settings.get('pots'):
            ed = yaml.safe_load(open(os.path.join(HERE, 'entrance_data.yml')))
            items = {k: {'quantity': 0} for k in ('rupee-5', 'rupee-20', 'rupee-50', 'rupee-100')}
            pots_mod.add_to_logic(pots_mod.scan(romfs, leb), logic, items, ed)
    ed = yaml.safe_load(open(os.path.join(HERE, 'entrance_data.yml')))
    loc_rooms = json.load(open(os.path.join(HERE, 'location_rooms.json')))

    sheet_path = os.path.join(seed_dir, 'romfs', 'region_common', 'datasheets', 'GlobalFlags.gsheet')
    if not os.path.exists(sheet_path):
        sheet_path = os.path.join(romfs, 'region_common', 'datasheets', 'GlobalFlags.gsheet')
    flags = {str(v['symbol']): int(v['index']) for v in oead_tools.readSheet(sheet_path)['values']}
    check_flags = {}  # shopsanity / trendsanity checks: location -> done flag (shopsanity.py, trendsanity.py)
    cf = os.path.join(seed_dir, 'check_flags.tsv')
    if os.path.exists(cf):
        check_flags = {ln.split('\t')[0]: int(ln.split('\t')[1]) for ln in open(cf) if '\t' in ln}

    def room_file(room):
        lv = re.match(r'(.+)_\d\d[A-Z]', room).group(1)
        for base in (os.path.join(seed_dir, 'romfs', 'region_common', 'level'), os.path.join(romfs, 'region_common', 'level')):
            p = os.path.join(base, lv, room + '.leb')
            if os.path.exists(p):
                return leb.Room(open(p, 'rb').read())
        return None

    # where interiors are: level -> door cell, and LAS location -> door (entrance_data `inside`)
    level_cell, loc_door_cell = {}, {}
    for d in ed['doors']:
        c = cell_of_field(d['field_room'])
        level_cell.setdefault(d['dest_level'], c)
        for it in d.get('inside') or []:
            loc_door_cell[it['location']] = c
    nodes = []
    for name, v in logic.items():
        t = v.get('type')
        if t not in ('region', 'item', 'room', 'interaction', 'enemy', 'trick', 'follower'):
            continue
        n = {'n': name, 't': t, 'r': v.get('region', ''), 'c': condition(v, levels)}
        for x in ('pit', 'heavy'):
            if v.get('condition-' + x):
                n[x] = v['condition-' + x]
        nodes.append(n)

    groups, checks = [], []
    gidx = {}

    def group(key, label, cell):
        if key not in gidx:
            gidx[key] = len(groups)
            groups.append({'label': label, 'x': cell[0], 'y': cell[1]})
        return gidx[key]

    region_cells = {}
    pos = {}
    absent = set(settings.get('absent-locations', ()))  # (rooms the dungeon shuffle left out: not in the game)
    for name, v in logic.items():
        if v.get('type') != 'item' or name.startswith('starting-') or name in absent:
            continue
        lr = loc_rooms.get(name, {})
        room, level = lr.get('room'), lr.get('level')
        g = -1
        cell = None
        if name.startswith('pot-') or re.match(r'D\d-pot-', name) or name.startswith('bush-'):
            continue  # pots and bushes come from pots.tsv / bushes.tsv below
        m = re.match(r'dig-(\d\d)([A-P])-(\d\d)$', name)
        if m:  # digsanity: one badge per screen; collected = flag 24576 + tile id (tools/rando/digs.py)
            row, col, tile = int(m.group(1)), ord(m.group(2)) - 65, int(m.group(3))
            cell = cell_of_field(f'Field_{m.group(1)}{m.group(2)}')
            checks.append({'n': name, 'x': cell[0], 'y': cell[1], 'g': group(f'dig-{row}-{col}', 'Digs', cell),
                           'flag': 24576 + ((row - 1) * 16 + col) * 80 + tile})
            continue
        if level in DUNGEONS or (re.match(r'D(\d)-', name) and not room):
            dk = DUNGEONS.get(level) or name[:2]
            lvname = level or next(k for k, x in DUNGEONS.items() if x == dk)
            cell = level_cell.get(lvname)
            if cell:
                g = group(dk, dk, cell)
        elif room and room.startswith('Field'):
            cell = cell_of_field(room)
        elif name in loc_door_cell:
            cell = loc_door_cell[name]
        elif level and level in level_cell:
            cell = level_cell[level]
        if cell:
            region_cells.setdefault(v.get('region'), []).append(cell)
        pos[name] = (cell, g, room)
    centroid = {r: (sum(c[0] for c in cs) / len(cs), sum(c[1] for c in cs) / len(cs)) for r, cs in region_cells.items()}
    for name, (cell, g, room) in pos.items():
        if not cell:
            cell = centroid.get(logic[name].get('region'))
        if not cell:
            continue
        flag = check_flags.get(name, -1)
        sub = logic[name].get('subtype')
        try:
            if name in check_flags:
                pass
            elif name in getattr(las_data, 'HEART_FLAGS', {}):
                flag = flags.get(las_data.HEART_FLAGS[name], -1)
            elif name in underwater.SHELL_FLAGS:  # (the sunken seashells' own flags: tools/rando/underwater.py)
                flag = flags.get(underwater.SHELL_FLAGS[name], -1)
            elif room and sub in ('chest', 'standing', 'drop'):
                r = room_file(room)
                boxes = [a for a in r.actors if a.type == 0xf7] if r else []
                keys = [a for a in r.actors if a.type == 0xa9] if r else []
                if sub == 'chest' and len(boxes) == 1 and boxes[0].switches[1][0] == 1:
                    flag = boxes[0].switches[1][1]
                elif len(keys) == 1 and keys[0].switches[1][0] == 1:
                    flag = keys[0].switches[1][1]
        except Exception:
            pass
        checks.append({'n': name, 'x': cell[0], 'y': cell[1], 'g': g, 'flag': flag})
    # item pots (pots.tsv: level x y z flag) -> logic node name via the same naming as pots.py
    tsv = os.path.join(seed_dir, 'pots.tsv')
    if os.path.exists(tsv):
        by_level = {}
        for p in pots_mod.scan(romfs, leb):
            by_level.setdefault(p['level'], []).append(p)
        names = {}
        for lv, ps in by_level.items():
            rf = {}
            for p in ps:
                r = room_file(p['room'])
                a = next((a for a in r.actors if a.key == p['key']), None) if r else None
                if a:
                    names[(lv, round(a.posX, 2), round(a.posZ, 2))] = pots_mod.location_name(p)
        for line in open(tsv):
            lv, x, y, z, fl = line.split()
            n = names.get((lv, round(float(x), 2), round(float(z), 2)))
            if not n or n not in logic:
                continue
            cell = level_cell.get(lv)
            if not cell:
                continue
            key = DUNGEONS.get(lv, lv)
            label = DUNGEONS.get(lv) or lv
            checks.append({'n': n, 'x': cell[0], 'y': cell[1], 'g': group(key, label, cell), 'flag': int(fl)})
    # bushsanity (bushes.tsv: level x y z flag): a dot on the bush's Field screen (bush-<screen>-<n>), hidden once cut
    tsv = os.path.join(seed_dir, 'bushes.tsv')
    if os.path.exists(tsv):
        import bushes as bushes_mod
        names = {}
        for b in bushes_mod.scan(romfs, leb, pits=True):  # (older seeds hid items over pits too)
            r = room_file(b['room'])
            a = next((a for a in r.actors if a.key == b['key']), None) if r else None
            if a:
                names[(round(a.posX, 2), round(a.posZ, 2))] = bushes_mod.location_name(b)
        for line in open(tsv):
            lv, x, y, z, fl = line.split()[:5]  # (bushes.tsv rows may carry more: pots.write_tsv)
            n = names.get((round(float(x), 2), round(float(z), 2)))
            cell = n and cell_of_field('Field_' + n.split('-')[1])
            if n in logic and cell:
                checks.append({'n': n, 'x': cell[0], 'y': cell[1], 'g': -1, 'flag': int(fl)})
    out = {'settings': {k: 1 for k, v in settings.items() if v is True}, 'nodes': nodes, 'checks': checks,
           'groups': groups, 'logic': settings.get('logic', 'basic')}
    with open(os.path.join(seed_dir, 'tracker.json'), 'w') as f:
        json.dump(out, f)
    # same data as tab-separated lines for the runtime (no JSON parser there)
    with open(os.path.join(seed_dir, 'tracker.tsv'), 'w') as f:
        for k in out['settings']:
            f.write(f'S\t{k}\n')
        for n in nodes:
            f.write(f"N\t{n['t']}\t{n['n']}\t{n['r'] or '-'}\t{n['c'] or '-'}\t{n.get('pit', '-')}\t{n.get('heavy', '-')}\n")
        for g in groups:
            f.write(f"G\t{g['label']}\t{g['x']:.3f}\t{g['y']:.3f}\n")
        for c in checks:
            f.write(f"C\t{c['n']}\t{c['x']:.3f}\t{c['y']:.3f}\t{c['g']}\t{c['flag']}\n")
    return len(checks), len(groups)


if __name__ == '__main__':
    for d in sys.argv[1:]:
        print(d, build(os.path.abspath(d), os.path.join(ROOT, 'extracted', 'romfs')))
