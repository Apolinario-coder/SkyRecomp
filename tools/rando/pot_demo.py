#!/usr/bin/env python3
"""Demo: Bow / Boomerang hidden in Marin & Tarin's house pots (seed rando/PotTest). Each run uses fresh flags
(so the items are back even if they were collected): pot_demo.py <first flag index>."""
import copy, os, sys, types
FLAG = int(sys.argv[1])
ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
LAS = os.path.join(ROOT, 'third_party', 'LAS-Randomizer')
sys.modules['PySide6'] = types.ModuleType('PySide6'); sys.modules['PySide6.QtCore'] = types.ModuleType('PySide6.QtCore')
sys.path.insert(0, LAS); os.chdir(LAS)
from RandomizerCore.Tools import leb, event_tools, oead_tools
from RandomizerCore.Randomizers import item_get
SEED = os.path.join(ROOT, 'rando/PotTest/romfs/region_common')
BASE = os.path.join(ROOT, 'rando/EntTest1/romfs/region_common')
ROM = os.path.join(ROOT, 'extracted/romfs/region_common')
items = [(7, f'LARPot{FLAG}', 'Bow', 'ItemBow.bfres', 'Bow', FLAG),
         (8, f'LARPot{FLAG + 1}', 'Boomerang', 'ItemBoomerang.bfres', 'Boomerang', FLAG + 1)]
sheet = oead_tools.readSheet(BASE + '/datasheets/GlobalFlags.gsheet')
for it in items: sheet['values'].append({'symbol': it[1], 'index': it[5]})
oead_tools.writeSheet(SEED + '/datasheets/GlobalFlags.gsheet', sheet)
flow = event_tools.readFlow(BASE + '/event/SinkingSword.bfevfl')
for _, flag, key, _, _, _ in items:
    get = item_get.insertItemGetAnimation(flow.flowchart, key, -1)
    event_tools.addEntryPoint(flow.flowchart, flag)
    event_tools.createActionChain(flow.flowchart, flag, [('SinkingSword', 'Destroy', {}),
                                  ('EventFlags', 'SetFlag', {'symbol': flag, 'value': True})], get)
event_tools.writeFlow(SEED + '/event/SinkingSword.bfevfl', flow)
r = leb.Room(open(ROM + '/level/MarinTarinHouse/MarinTarinHouse_01A.leb', 'rb').read())
for n, (pi, flag, key, mpath, mname, idx) in enumerate(items):
    pot = r.actors[pi]
    it = copy.deepcopy(pot); kk = 0x7A00000000000000 | (n << 32) | (pot.key & 0xFFFFFFFF)
    it.key = kk; it.name = b'SinkingSword-%016X' % kk; it.type = 0x194
    it.parameters = [mpath.encode(), mname.encode(), flag.encode(), flag.encode(), b'false', b'', b'', b'']
    it.switches = [(4, 0)] * 4
    it.scaleX = it.scaleY = it.scaleZ = 0.7
    r.actors.append(it)
open(SEED + '/level/MarinTarinHouse/MarinTarinHouse_01A.leb', 'wb').write(r.repack())
sys.path.insert(0, os.path.join(ROOT, 'tools', 'rando'))
import pots
pots.write_tsv(os.path.join(ROOT, 'rando/PotTest'), leb)
print('pot demo ready, flags', FLAG, FLAG + 1)
