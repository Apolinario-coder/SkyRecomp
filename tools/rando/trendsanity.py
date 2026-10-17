#!/usr/bin/env python3
"""Trendsanity (LAS Recomp addition on top of LAS-Randomizer): each prize spot on the Trendy Game floor is one check.

The crane floor has five prize spots (CranePrize place type 1 = the lower row, index 0-2 left to right; type 2 = the
two raised boxes, index 0-1; type 0 = the silver rupee, left as it is). Each spot holds its check from the start;
once won, it holds the spot's generic prizes from then on (rupees, recovery hearts, magic powder, bombs / shield
refills - the vanilla non-featured rows of that spot). No featured-prize rotation: the vanilla featured prizes
(Yoshi doll, pieces of heart, seashells, chamber stone, figures) never appear. Locations: LAS's trendy-prize-1..5
are the five spots; trendy-prize-6 is dropped (one rupee less in the pool). Without figures nothing starts
Trendy's final gift (vanilla: the BowWow figure), so trendy-prize-final is excluded (junk, never given).
Logic: every spot = can-farm-rupees (10 rupees a try; the crane reaches every spot).

In the game (CranePrize.gsheet): a check is a new non-featured row (21+) at its spot, showing the randomized item's
model: actor/Prize<symbol>.bfres = the item's model with the model renamed in place to a unique name of the same
length (same last character, so the file's name dictionary still finds it; a model not named <symbol> crashes).
Winning it: AddPrizeByKey -> runtime/hle/shopsanity.c sets the check's flags (won: the row's condition hides it and
the spot's generic rows' condition shows them; "give": PrizeCommon gives the item with its item-get animation).
"""
import copy
import os

# (location, crane floor spot (place type, index))
CHECKS = [
    ('trendy-prize-1', (1, 0)),
    ('trendy-prize-2', (1, 1)),
    ('trendy-prize-3', (1, 2)),
    ('trendy-prize-4', (2, 0)),
    ('trendy-prize-5', (2, 1)),
]
DROPPED = 'trendy-prize-6'
FINAL = 'trendy-prize-final'
DOOR = 'trendy-game'  # tools/rando/entrance_data.yml (shuffled interiors)
FIRST_ROW = 21  # AddPrize knows rows 0-20
TRAIT = 'LARUpright'  # CranePrizePhysicsTraits row for check prizes


def enabled(settings):
    return bool(settings.get('trendsanity'))


def add_to_logic(logic_defs, item_defs, settings):
    """Returns {entrance door: [(location, condition)]} for shuffled interiors."""
    if logic_defs.pop(DROPPED, None):
        item_defs['rupee-50']['quantity'] -= 1  # (its seashell stays in the pool)
    settings['excluded-locations'].add(FINAL)
    for loc, _ in CHECKS:
        logic_defs[loc]['condition-basic'] = 'can-farm-rupees'
    return {DOOR: [(loc, 'can-farm-rupees') for loc, _ in CHECKS]}


def rename_model(data, old, new):
    """The bfres with its (only) model renamed in the string pool (same length: offsets stay valid)."""
    assert len(old) == len(new)
    pat = len(old).to_bytes(2, 'little') + old.encode() + b'\0'
    assert data.count(pat) == 1, old
    i = data.index(pat)
    return data[:i + 2] + new.encode() + data[i + 2 + len(old):]


def symbol_for(model_name, n):
    """Unique prize symbol of the model name's length, ending with its last character."""
    code = ''
    v = n
    for _ in range(len(model_name) - 2):
        code = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ'[v % 36] + code
        v //= 36
    assert v == 0 and len(model_name) >= 3, model_name
    return 'L' + code + model_name[-1]


def apply(mods, out_dir, romfs, settings, event_tools, oead_tools, item_get, las_data, alloc_flags):
    common_out = os.path.join(out_dir, 'romfs', 'region_common')
    common_in = os.path.join(romfs, 'region_common')

    def path(rel):
        p = os.path.join(common_out, rel)
        return p if os.path.exists(p) else os.path.join(common_in, rel)

    def out(rel):
        p = os.path.join(common_out, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        return p

    sheet = oead_tools.readSheet(path('datasheets/GlobalFlags.gsheet'))
    names = [f'LARPrize{n}' for n in range(len(CHECKS))] + [f'LARPrizeGive{n}' for n in range(len(CHECKS))]
    flags = alloc_flags(sheet, names + ['LARPrizeOff'])
    oead_tools.writeSheet(out('datasheets/GlobalFlags.gsheet'), sheet)

    sheet = oead_tools.readSheet(path('datasheets/CranePrize.gsheet'))
    rows = [oead_tools.parseStruct(v) for v in sheet['values']]
    assert len(rows) == FIRST_ROW, len(rows)
    won = {place: f'LARPrize{n}' for n, (_, place) in enumerate(CHECKS)}
    # vanilla rows: featured prizes never appear; a spot's generic prizes only once its check is won
    for v in sheet['values']:
        featured = v['options']['isFeatured'] or v['symbol'] == 'YoshiDoll'  # (LAS made the doll non-featured)
        for lay in v['layouts']:
            place = (int(lay['place']['type']), int(lay['place']['index']))
            for cond in lay['conditions']:  # LAS: the red rupee in the doll's spot waits for the doll
                if cond['parameter'] == 'TradeYoshiDollGet':
                    cond['parameter'] = won[(1, 0)]
            if featured:
                lay['conditions'].append({'category': 1, 'parameter': 'LARPrizeOff'})
            elif place in won and not any(c['parameter'] == won[place] for c in lay['conditions']):
                lay['conditions'].append({'category': 1, 'parameter': won[place]})
    checks = []
    for n, (loc, place) in enumerate(CHECKS):
        key, index, model_path, model_name = mods.getItemInfo(loc, mods.trap_models)
        sym = symbol_for(model_name, n)
        src = os.path.join(common_out, 'actor', model_path)
        if not os.path.exists(src):
            src = os.path.join(common_in, 'actor', model_path)
        with open(src, 'rb') as f:
            model = rename_model(f.read(), model_name, sym)
        with open(out(f'actor/Prize{sym}.bfres'), 'wb') as f:
            f.write(model)
        new = copy.deepcopy(rows[7])  # the Yoshi doll row: a grabbable "Sample" prize
        new.update(symbol=sym, getCount=1, modelScale=float(las_data.MODEL_SIZES.get(model_name, 1.0)),
                   layouts=[{'itemIndex': -1, 'conditions': [{'category': 1, 'parameter': f'!LARPrize{n}'}],
                             'place': {'type': place[0], 'index': place[1]}}])
        new['options']['isFeatured'] = False
        new['collision']['traits'] = TRAIT
        sheet['values'].append(oead_tools.dictToStruct(new))
        checks.append({'n': n, 'loc': loc, 'row': FIRST_ROW + n, 'key': key, 'index': index, 'sym': sym})
    oead_tools.writeSheet(out('datasheets/CranePrize.gsheet'), sheet)

    # item models aren't made to sit on the prize boxes (a stick or an NPC model tipped over and rolled off the raised
    # boxes): check prizes get physics that can't tip
    sheet = oead_tools.readSheet(path('datasheets/CranePrizePhysicsTraits.gsheet'))
    trait = oead_tools.parseStruct(next(v for v in sheet['values'] if v['symbol'] == 'Sample'))
    trait.update(symbol=TRAIT, bounciness=0.0, angularDamp=1.0,
                 axisLock={'rotationX': True, 'rotationY': True, 'rotationZ': True})
    sheet['values'].append(oead_tools.dictToStruct(trait))
    oead_tools.writeSheet(out('datasheets/CranePrizePhysicsTraits.gsheet'), sheet)

    # PrizeCommon 'get': Deactivate -> AddPrizeByKey (the runtime flags a check) -> the check's item, or the vanilla
    # get sequence -> the rest as before. (AddPrize moves before the vanilla fanfare.)
    flow = event_tools.readFlow(path('event/PrizeCommon.bfevfl'))
    fc = flow.flowchart
    seq, add, after = 'Event0', 'Event1', 'Event5'
    assert event_tools.findEvent(fc, seq).data.nxt.v.name == add
    event_tools.insertEventAfter(fc, seq, after)
    nxt = seq
    for c in reversed(checks):
        n = c['n']
        clear = event_tools.createActionEvent(fc, 'EventFlags', 'SetFlag', {'symbol': f'LARPrizeGive{n}', 'value': False}, after)
        give = item_get.insertItemGetAnimation(fc, c['key'], c['index'], None, clear)
        nxt = event_tools.createSwitchEvent(fc, 'EventFlags', 'CheckFlag', {'symbol': f'LARPrizeGive{n}'}, {0: nxt, 1: give})
    event_tools.insertEventAfter(fc, add, nxt)
    event_tools.insertEventAfter(fc, 'Event2', add)
    event_tools.writeFlow(out('event/PrizeCommon.bfevfl'), flow)

    with open(os.path.join(out_dir, 'shopsanity.tsv'), 'a') as f:  # runtime/hle/shopsanity.c (255: add nothing)
        for c in checks:
            f.write(f"prize\t{c['row']}\t{flags['LARPrize%d' % c['n']]}\t{flags['LARPrizeGive%d' % c['n']]}\t255\n")
    with open(os.path.join(out_dir, 'check_flags.tsv'), 'a') as f:
        for c in checks:
            f.write(f"{c['loc']}\t{flags['LARPrize%d' % c['n']]}\n")
    return [f"  {c['loc']:20} {mods.placements[c['loc']]}" for c in checks]
