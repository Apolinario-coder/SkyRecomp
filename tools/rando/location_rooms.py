#!/usr/bin/env python3
"""Map LAS item locations to the room/level that contains them -> tools/rando/location_rooms.json."""
import json, os, re, sys, types
HERE = os.path.dirname(os.path.abspath(__file__))
LAS = os.path.normpath(os.path.join(HERE, '..', '..', 'third_party', 'LAS-Randomizer'))
sys.modules['PySide6'] = types.ModuleType('PySide6'); sys.modules['PySide6.QtCore'] = types.ModuleType('PySide6.QtCore')
sys.argv = [os.path.join(LAS, 'randomizer.py')]; sys.path.insert(0, LAS); os.chdir(LAS)
import yaml
from RandomizerCore.Randomizers import data
logic = yaml.safe_load(open('RandomizerCore/Data/logic.yml').read().split('---', 1)[1])
rooms = {}
for tab in ('CHEST_ROOMS', 'SMALL_KEY_ROOMS', 'GOLDEN_LEAF_ROOMS', 'INSTRUMENT_ROOMS', 'HEART_ROOMS'):
    for k, v in getattr(data, tab).items():
        if isinstance(v, (list, tuple)): v = v[0]
        rooms[k] = v
sys.path.insert(0, HERE)
import underwater  # (the sunken seashells: no LAS table has their rooms)
for k, (_, r, _) in underwater.SPOTS.items():
    rooms.setdefault(k, r)
out = {}
for k, v in logic.items():
    if v.get('type') != 'item': continue
    r = rooms.get(k)
    lvl = re.match(r'(.+)_\d\d[A-Z]', r).group(1) if r and re.match(r'(.+)_\d\d[A-Z]', r) else None
    out[k] = {'room': r, 'level': lvl, 'region': v.get('region'), 'subtype': v.get('subtype'),
              'cond': v.get('condition-basic')}
json.dump(out, open(os.path.join(HERE, 'location_rooms.json'), 'w'), indent=1)
unk = [k for k, v in out.items() if not v['room']]
print(len(out), 'item locations;', len(unk), 'without a room:')
for k in unk: print('  ', k, out[k]['region'], out[k]['subtype'])
