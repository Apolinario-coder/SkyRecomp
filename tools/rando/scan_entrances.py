#!/usr/bin/env python3
"""Inventory every loading zone (actor type 0x190) in every room file -> tools/rando/loading_zones.json."""
import json, os, sys, types
HERE = os.path.dirname(os.path.abspath(__file__))
LAS = os.path.normpath(os.path.join(HERE, '..', '..', 'third_party', 'LAS-Randomizer'))
ROMFS = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', '..', 'extracted', 'romfs'))
sys.modules['PySide6'] = types.ModuleType('PySide6'); sys.modules['PySide6.QtCore'] = types.ModuleType('PySide6.QtCore')
sys.argv = [os.path.join(LAS, 'randomizer.py')]
sys.path.insert(0, LAS); os.chdir(LAS)
from RandomizerCore.Tools import leb

def s(v):
    return v.decode('utf-8', 'replace') if isinstance(v, bytes) else v

zones = []
types_count = {}
lv = os.path.join(ROMFS, 'region_common', 'level')
for level in sorted(os.listdir(lv)):
    d = os.path.join(lv, level)
    if not os.path.isdir(d): continue
    for f in sorted(os.listdir(d)):
        if not f.endswith('.leb'): continue
        with open(os.path.join(d, f), 'rb') as fh:
            try: room = leb.Room(fh.read())
            except Exception as e:
                print('skip', f, e); continue
        idx = 0
        for a in room.actors:
            types_count[a.type] = types_count.get(a.type, 0) + 1
            if a.type == 0x190:
                zones.append({'level': level, 'room': f[:-4], 'index': idx, 'name': s(a.name),
                              'params': [s(p) for p in a.parameters], 'pos': [a.posX, a.posY, a.posZ],
                              'scale': [a.scaleX, a.scaleY, a.scaleZ]})
                idx += 1
json.dump(zones, open(os.path.join(HERE, 'loading_zones.json'), 'w'), indent=1)
print(len(zones), 'loading zones')
