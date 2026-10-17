#!/usr/bin/env python3
"""Collect every instruction word LAS-Randomizer can patch into main (all settings combinations).

The recompiled game can't take patches at run time, so gen.py compiles every variant of each patched
instruction in, and the active seed's 1.0.0.pchtxt selects which one runs (runtime/rando.c).

Writes tools/rando/patch_variants.json:
  {"code": {"0xADDR": ["0xWORD", ...]}, "data": ["0xADDR", ...]}   (addresses are main image offsets)
"""
import json
import os
import random
import sys
import types

HERE = os.path.dirname(os.path.abspath(__file__))
LAS = os.path.normpath(os.path.join(HERE, '..', '..', 'third_party', 'LAS-Randomizer'))
qtcore = types.ModuleType('PySide6.QtCore')
sys.modules['PySide6'] = types.ModuleType('PySide6')
sys.modules['PySide6.QtCore'] = qtcore
sys.argv[0] = os.path.join(LAS, 'randomizer.py')
sys.path.insert(0, LAS)
os.chdir(LAS)

from keystone import Ks, KS_ARCH_ARM64, KS_MODE_LITTLE_ENDIAN  # noqa: E402
from RandomizerCore.ASM import assemble  # noqa: E402
from RandomizerCore.randomizer_data import ENEMY_DATA  # noqa: E402

assemble.IS_RUNNING_FROM_SOURCE = False  # quiet
ks = Ks(KS_ARCH_ARM64, KS_MODE_LITTLE_ENDIAN)
CONDS = ['free-book', 'randomize-enemies', 'OHKO', 'lv1-beam', 'nice-rod', 'nice-bombs', 'stealing', 'keys',
         'keys+mcb', 'blur-removal']

code, data = {}, set()
asm_files = sorted(f for f in os.listdir(assemble.ASM_PATH) if f.endswith('.asm'))
for state in (True, False):
    for enemy in ENEMY_DATA['Chest_Enemies']:
        settings = {k: state for k in CONDS}
        asm_data = {'CHEST_ENEMY': f'#{enemy}'}
        for asm in asm_files:
            for address, instruction, comment in assemble.readASM(asm, asm_data, settings):
                if instruction.startswith('.string') or instruction.startswith('.short'):
                    data.add(address)
                    continue
                raw = ks.asm(instruction, as_bytes=True)[0]  # exactly as LAS assembles it (base 0)
                if address >= 0x16a0000:  # rodata patched with instructions syntax (e.g. `.byte`): data
                    data.add(address)
                    continue
                for i in range(0, len(raw), 4):
                    w = int.from_bytes(raw[i:i + 4], 'little')
                    code.setdefault(address + i, set()).add(w)

out = {'code': {hex(a): sorted(hex(w) for w in ws) for a, ws in sorted(code.items())},
       'data': sorted(hex(a) for a in data)}
with open(os.path.join(HERE, 'patch_variants.json'), 'w') as f:
    json.dump(out, f, indent=1)
print(len(out['code']), 'code words,', sum(len(v) for v in out['code'].values()), 'variants;', len(out['data']), 'data patches')
