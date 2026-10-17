#!/usr/bin/env python3
"""Runtime lookup tables for a generated seed: rando/<seed>/flags.tsv (GlobalFlags name -> index for the flags
the runtime reads, e.g. the TradeXxxGet flags LAS adds and the trade NPCs' done flags). seed_tables.py <seed dir> regenerates it."""
import os
import sys
import types

WANT = ('Trade',)
# set by the trade NPCs' flows when they take the item (= traded away; Items-screen trade row, inventory_overlay.mm).
# Audited: each is set only by its NPC's flow (all of the seed's + the game's event flows scanned for SetFlag), except
# StickDrop: with open-bridge LAS sets it at the start (PlayerStart: the bridge) and Kiki sets KikiGone instead - so the
# Bananas' done flag is the alias TradeBananasDone below, never StickDrop itself.
WANT_EXACT = {'RibbonDrop', 'DogfoodDrop', 'BananaDrop', 'HoneycombDrop', 'PineappleGet', 'HibiscusDrop',
              'LetterDrop', 'BroomDrop', 'FishhookDrop', 'BrassiereDrop', 'ScaleDrop', 'MarthaStatueOpened'}
# + the flags runtime/hle/pets.c reads / sets (tools/rando/pets.py RUNTIME_FLAGS)
EXACT = {'unused0427', 'unused0428', 'unused0429', 'unused0430', 'GhostClear1', 'Ghost2_Clear', 'Ghost3_Clear',
         'Ghost4_Clear', 'WalrusAwaked', 'EighthClear'}


def write_flags(seed_dir, oead_tools, romfs=None):
    p = os.path.join(seed_dir, 'romfs', 'region_common', 'datasheets', 'GlobalFlags.gsheet')
    if not os.path.exists(p) and romfs:
        p = os.path.join(romfs, 'region_common', 'datasheets', 'GlobalFlags.gsheet')
    sheet = oead_tools.readSheet(p)
    rows = [f"{v['symbol']}\t{int(v['index'])}\n" for v in sheet['values']
            if str(v['symbol']).startswith(WANT) or v['symbol'] in WANT_EXACT or str(v['symbol']) in EXACT]
    sp = os.path.join(seed_dir, 'settings.json')
    if os.path.exists(sp):  # (unknown setting: no alias, the Bananas never show as traded)
        import json
        bridge = json.load(open(sp)).get('open-bridge')
        idx = {str(v['symbol']): int(v['index']) for v in sheet['values']}
        alias = idx.get('KikiGone' if bridge else 'StickDrop') if bridge is not None else None
        if alias is not None:
            rows.append(f"TradeBananasDone\t{alias}\n")
    with open(os.path.join(seed_dir, 'flags.tsv'), 'w') as f:
        f.writelines(rows)
    return len(rows)


if __name__ == '__main__':
    here = os.path.dirname(os.path.abspath(__file__))
    las = os.path.normpath(os.path.join(here, '..', '..', 'third_party', 'LAS-Randomizer'))
    sys.modules['PySide6'] = types.ModuleType('PySide6')
    sys.modules['PySide6.QtCore'] = types.ModuleType('PySide6.QtCore')
    sys.path.insert(0, las)
    from RandomizerCore.Tools import oead_tools as ot
    for d in sys.argv[1:]:
        print(d, write_flags(os.path.abspath(d), ot), 'flags')
