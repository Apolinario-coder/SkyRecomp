#!/usr/bin/env python3
"""Shopsanity (LAS Recomp addition on top of LAS-Randomizer): the six goods of the Mabe Village Tool Shop are checks.

The shop's six slots (ShopItem.gsheet, left to right): shield, 3 hearts, deluxe shovel, bombs, chamber stone, piece
of heart. One check per slot and no restocking: once bought or stolen, the slot shows a SOLD OUT plate for good.
LAS's locations for the shovel (shop-slot3-1st), the first chamber stone (shop-slot5-1st) and the heart
(shop-slot6) are used (no longer forced vanilla); shop-slot1 / 2 / 4 are new. LAS's other shop locations (the bow
and six more chamber stones the vanilla shop sells later) are dropped: the bow goes into the pool, six chamber
stones leave it.

How it works in the game:
  buying    each check good has its own entry point in ToolShopkeeper.bfevfl (price check, the item-get animation
            of the randomized item, its done flag). Price tag and offer text name the item and the price (every
            language: System.msbt LARPriceTag<n> / LARBuy<n>).
  display   the good shows the randomized item's model (traps: a random decoy, as everywhere in LAS). A slot with
            nothing on sale gets the shop's own SOLD OUT plate (ObjSoldOutPlate), as in the vanilla shop.
  stealing  the good carries a dummy item (Items.gsheet LARShop<n>, itemID 230+): the shopkeeper's steal code adds
            it, runtime/hle/shopsanity.c turns that into the check's flags, PlayerStart's ExitOutShop gives the real
            item (animation) after the vanilla "you stole it" scene. The steal consequences stay vanilla (THIEF,
            the shopkeeper's punishment): fast-stealing is off with shopsanity.
Logic: every check = can-shop (LAS: can-farm-rupees | sword), whatever the price - as LAS does for its shop checks.
Prices: 'vanilla' (the slot's vanilla price; the chamber stone's 1280 is over the wallet's 999 -> 999) or 'random'.
"""
import os
import random
import re

# (location, ShopItem slot mIndex, vanilla price, new location's filler or None)
CHECKS = [
    ('shop-slot1', 0, 20, 'rupee-20'),
    ('shop-slot2', 1, 10, 'rupee-20'),
    ('shop-slot3-1st', 2, 200, None),
    ('shop-slot4', 3, 10, 'rupee-50'),
    ('shop-slot5-1st', 4, 999, None),
    ('shop-slot6', 5, 200, None),
]
DROPPED = ['shop-slot3-2nd'] + [f'shop-slot5-{n}' for n in ('2nd', '3rd', '4th', '5th', '6th', '7th')]
SHOP_DOOR = 'town-tool-shop'  # tools/rando/entrance_data.yml (shuffled interiors)
ITEM_ID_BASE = 230  # dummy item ids (one byte: the shop keeps the good's item id in a byte)
RANDOM_PRICES = [10, 20, 30, 40, 50, 60, 80, 100, 120, 150, 180, 200, 250, 300, 350, 400, 500, 600, 750, 980]
RANDOM_WEIGHTS = [4, 6, 6, 6, 8, 6, 8, 9, 8, 8, 6, 7, 5, 5, 3, 3, 2, 2, 1, 1]
FLAG_BASE = 1240  # (as pots.py: vanilla GlobalFlags <= 1118, LAS's own after that)

# item key -> UI.msbt ItemName_<x> where the names differ; others: the key itself, then System.msbt Get<key>
UI_NAME = {'SwordLv1': 'Sword1', 'SwordLv2': 'Sword2', 'PowerBraceletLv1': 'Bracelet1', 'PowerBraceletLv2': 'Bracelet2',
           'RocsFeather': 'Feather', 'FullMoonCello': 'Violin', 'ConchHorn': 'Horn', 'SeaLilysBell': 'Bell',
           'SurfHarp': 'Harp', 'WindMarimba': 'Marimba', 'CoralTriangle': 'Triangle', 'EveningCalmOrgan': 'Organ',
           'ThunderDrum': 'Drum', 'SecretSeashell': 'SecretShells', 'Seashell': 'SecretShells', 'MagicPowder': 'Powder',
           'Song_Mambo': 'Mambo', 'Song_Soul': 'Soul', 'Song_WindFish': 'WindFish', 'ClothesRed': 'WearRed',
           'ClothesBlue': 'WearBlue', 'ClothesGreen': 'WearGreen', 'Necklace': 'PinkBra'}
EN_NAME = {'HeartPiece': 'Piece of Heart', 'HeartContainer': 'Heart Container', 'SmallKey': 'Small Key',
           'NightmareKey': 'Nightmare Key', 'Compass': 'Compass', 'DungeonMap': 'Dungeon Map', 'StoneBeak': 'Stone Beak',
           'GoldenLeaf': 'Golden Leaf', 'Rupee5': '5 Rupees', 'Rupee20': '20 Rupees', 'Rupee50': '50 Rupees',
           'Rupee100': '100 Rupees', 'Rupee300': '300 Rupees', 'MagicPowder_MaxUp': 'Magic Powder Upgrade',
           'Bomb_MaxUp': 'Bomb Upgrade', 'Arrow_MaxUp': 'Arrow Upgrade', 'PanelDungeonPiece': 'Chamber Stone',
           'Bottle': 'Bottle', 'Heart': 'Heart', 'Arrow': 'Arrows'}
CJK = ('JP', 'CN', 'TW', 'KR')
PINK = b'\x0e\x00\x00\x03\x00\x04\x00\xd9Em\xff'  # colour tag around the price


def enabled(settings):
    return settings.get('shopsanity', 'off') in ('vanilla', 'random')


def add_to_logic(logic_defs, item_defs, settings):
    """The six goods as checks. Returns {entrance door: [(location, condition)]} for shuffled interiors."""
    spare = 0  # pool items freed by dropped locations: the new locations' filler
    for loc in DROPPED:
        d = logic_defs.pop(loc, None)
        if d and d['content'] == 'chamber-stone':
            item_defs['chamber-stone']['quantity'] -= 1
        elif d:
            spare += 1
    for loc, _, _, filler in CHECKS:
        d = logic_defs.get(loc)
        if d is None:
            d = logic_defs[loc] = {'type': 'item', 'subtype': 'shop', 'content': filler, 'region': 'mabe',
                                   'spoiler-region': 'mabe-village'}
            if spare:
                spare -= 1
            else:
                item_defs[filler]['quantity'] += 1
        d['condition-basic'] = 'can-shop'
    settings['fast-stealing'] = False  # the shopkeeper remembers a thief, as in the vanilla game
    return {SHOP_DOOR: [(loc, 'can-shop') for loc, _, _, _ in CHECKS]}


def alloc_flags(sheet, names, base=FLAG_BASE, limit=2048):
    """New GlobalFlags entries after the highest one in use (LAS's, earlier modules'): {name: index}."""
    used = [int(v['index']) for v in sheet['values']]
    nxt = max([base] + [u + 1 for u in used if base <= u < limit])
    out = {}
    for n in names:
        if nxt >= limit:
            raise RuntimeError('out of GlobalFlags indices')
        sheet['values'].append({'symbol': n, 'index': nxt})
        out[n] = nxt
        nxt += 1
    return out


def _lang_dirs(romfs):
    """(relative message dir, region code) for every language that has System.msbt + UI.msbt."""
    out = []
    for reg in sorted(os.listdir(romfs)):
        if not reg.startswith('region') or reg == 'region_common':
            continue
        for lang in sorted(os.listdir(os.path.join(romfs, reg))):
            rel = os.path.join(reg, lang, 'message')
            if all(os.path.exists(os.path.join(romfs, rel, f)) for f in ('System.msbt', 'UI.msbt')):
                out.append((rel, reg[6:8]))
    return out


def item_name(key, system, ui):
    """Localized display name of an item key (None if the language has none)."""
    raw = ui.texts.get('ItemName_' + UI_NAME.get(key, key))
    if raw:
        return raw.rstrip(b'\0')
    raw = system.texts.get('Get' + key)
    if raw:  # "You got the <colour>Hookshot<colour>! ..." -> the coloured part
        m = re.search(rb'\x0e\x00\x00\x03\x00\x04\x00....(.*?)\x0e', raw, re.S)
        if m and m.group(1).strip():
            return m.group(1).strip()
    return None


def _digits_like(sample, n):
    """n written like the price in a template (ASCII or full-width digits)."""
    s = str(n)
    return s.encode() if sample[:1].isdigit() else ''.join(chr(0xFF10 + int(ch)) for ch in s).encode()


def _set_price(raw, price):
    """Replaces the number right after the price colour tag."""
    i = raw.index(PINK) + len(PINK)
    m = re.match(rb'((?:[0-9,]|\xef\xbc[\x90-\x99]|\xef\xbc\x8c)+)', raw[i:])
    return raw[:i] + _digits_like(m.group(1), price) + raw[i + len(m.group(1)):]


def texts(romfs, out_romfs, msbt_mod, entries):
    """System.msbt LARPriceTag<n> / LARBuy<n> in every language. entries: [(n, item key, price)]."""
    count = 0
    for rel, region in _lang_dirs(romfs):
        sys_path = os.path.join(out_romfs, rel, 'System.msbt')
        src = sys_path if os.path.exists(sys_path) else os.path.join(romfs, rel, 'System.msbt')
        system = msbt_mod.Msbt(open(src, 'rb').read())
        ui = msbt_mod.Msbt(open(os.path.join(romfs, rel, 'UI.msbt'), 'rb').read())
        tag = system.texts['PriceTagScoop']   # "<name>\n<pink>200 Rupees<white><end>"
        buy = system.texts['BuyHeartPeace']   # "<name>: a low <pink>200 rupees<white>!<choice>Buy / Don't"
        for n, key, price in entries:
            name = item_name(key, system, ui) or EN_NAME.get(key, key).encode()
            t = name + b'\n' + tag[tag.index(PINK):]
            system.set(f'LARPriceTag{n}', _set_price(t, price))
            b = name + (b' ' if region in CJK else b': ') + buy[buy.index(PINK):]
            system.set(f'LARBuy{n}', _set_price(b, price))
        os.makedirs(os.path.dirname(sys_path), exist_ok=True)
        with open(sys_path, 'wb') as f:
            f.write(system.pack())
        count += 1
    return count


def apply(mods, out_dir, romfs, settings, event_tools, oead_tools, item_get, msbt_mod):
    """Writes the shop changes after LAS's ModsProcess. Returns spoiler lines."""
    common_out = os.path.join(out_dir, 'romfs', 'region_common')
    common_in = os.path.join(romfs, 'region_common')

    def path(rel):
        p = os.path.join(common_out, rel)
        return p if os.path.exists(p) else os.path.join(common_in, rel)

    def out(rel):
        p = os.path.join(common_out, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        return p

    rng = random.Random(f"{settings['seed']}-shop")
    mode = settings.get('shopsanity')
    checks = []
    for n, (loc, slot, price, _) in enumerate(CHECKS):
        if mode == 'random':
            price = rng.choices(RANDOM_PRICES, RANDOM_WEIGHTS)[0]
        key, index, model_path, model_name = mods.getItemInfo(loc, mods.trap_models)
        checks.append({'n': n, 'loc': loc, 'slot': slot, 'price': price, 'key': key, 'index': index,
                       'model_path': model_path, 'model_name': model_name, 'item_id': ITEM_ID_BASE + n})

    # flags: bought or stolen (the good disappears), and "stolen, give it on the way out"
    sheet = oead_tools.readSheet(path('datasheets/GlobalFlags.gsheet'))
    flags = alloc_flags(sheet, [f'LARShop{c["n"]}' for c in checks] + [f'LARShopSteal{c["n"]}' for c in checks])
    oead_tools.writeSheet(out('datasheets/GlobalFlags.gsheet'), sheet)

    # dummy items for stealing (copies of the Bow row: just a symbol + id)
    sheet = oead_tools.readSheet(path('datasheets/Items.gsheet'))
    base = next(oead_tools.parseStruct(v) for v in sheet['values'] if v['symbol'] == 'Bow')
    for c in checks:
        sheet['values'].append(oead_tools.dictToStruct(dict(base, symbol=f'LARShop{c["n"]}', itemID=c['item_id'],
                                                            gettingFlag='')))
    oead_tools.writeSheet(out('datasheets/Items.gsheet'), sheet)

    sheet = oead_tools.readSheet(path('datasheets/Conditions.gsheet'))
    for c in checks:
        sheet['values'].append(oead_tools.createCondition(f'LARShopCond{c["n"]}', [(1, f'!LARShop{c["n"]}')]))
    oead_tools.writeSheet(out('datasheets/Conditions.gsheet'), sheet)

    # each slot: its check until bought or stolen, then nothing (SOLD OUT plate; no vanilla goods, no restocking)
    sheet = oead_tools.readSheet(path('datasheets/ShopItem.gsheet'))
    for s in sheet['values']:
        new = []
        for c in checks:
            if c['slot'] == s['mIndex']:
                new.append({'mModelPath': f"actor/{c['model_path']}", 'mModelName': c['model_name'], 'mCount': 1,
                            'mLabel': f"System:LARPriceTag{c['n']}", 'mCondition': f"LARShopCond{c['n']}",
                            'mEntryPoint': f"LARShop{c['n']}", 'mItem': f"LARShop{c['n']}", 'mIndex': -1})
        s['mGoods'] = oead_tools.listToSheetArray('mGoods', new)
    oead_tools.writeSheet(out('datasheets/ShopItem.gsheet'), sheet)

    # buying: ToolShopkeeper entry point per good (same shape as the vanilla Take_* flows)
    flow = event_tools.readFlow(path('event/ToolShopkeeper.bfevfl'))
    fc = flow.flowchart
    talk_params = dict(event_tools.findEvent(fc, 'Event138').data.params.data)
    for c in checks:
        n = c['n']
        ep = f'LARShop{n}'
        done = event_tools.createActionEvent(fc, 'EventFlags', 'SetFlag', {'symbol': f'LARShop{n}', 'value': True})
        give = item_get.insertItemGetAnimation(fc, c['key'], c['index'], None, done)
        bought = event_tools.createActionChain(fc, None, [
            ('Link', 'AddRupee', {'amount': -c['price']}),
            ('ToolShopkeeper', 'RemoveGoods', {}),
            ('ToolShopkeeper', 'Talk', {'message': 'Npc:BuyToolShop'}),
        ], give)
        poor = event_tools.createActionEvent(fc, 'ToolShopkeeper', 'Talk', {'message': 'Npc:NoneMoneyToolShop'},
                                             event_tools.createActionEvent(fc, 'ToolShopkeeper', 'RestoreGoods', {}))
        money = event_tools.createSwitchEvent(fc, 'Link', 'CheckRupee', {'amount': c['price']}, {0: poor, 1: bought})
        answer = event_tools.createSwitchEvent(fc, 'Dialog', 'GetLastResult', {}, {
            0: money, 1: event_tools.createActionEvent(fc, 'ToolShopkeeper', 'RestoreGoods', {})})
        offer = event_tools.createActionEvent(fc, 'ToolShopkeeper', 'Talk', {'message': f'System:LARBuy{n}'})
        turn = event_tools.createActionEvent(fc, 'ToolShopkeeper', 'GenericTalkSequence', dict(talk_params))
        fork = event_tools.createForkEvent(fc, None, [turn, offer], answer)[0]
        event_tools.addEntryPoint(fc, ep)
        event_tools.insertEventAfter(fc, ep, event_tools.createActionEvent(fc, 'Hud', 'ShowRupee', {'visible': True}, fork))
    event_tools.writeFlow(out('event/ToolShopkeeper.bfevfl'), flow)

    # stealing: after the vanilla ExitOutShop "stolen" scene, give what was stolen (flags set by the runtime)
    flow = event_tools.readFlow(path('event/PlayerStart.bfevfl'))
    fc = flow.flowchart
    last = event_tools.findEvent(fc, 'Event774')
    tail = last.data.nxt.v.name if last.data.nxt.v is not None else None
    nxt = tail
    for c in reversed(checks):
        n = c['n']
        clear = event_tools.createActionEvent(fc, 'EventFlags', 'SetFlag', {'symbol': f'LARShopSteal{n}', 'value': False}, nxt)
        give = item_get.insertItemGetAnimation(fc, c['key'], c['index'], None, clear)
        nxt = event_tools.createSwitchEvent(fc, 'EventFlags', 'CheckFlag', {'symbol': f'LARShopSteal{n}'}, {0: nxt, 1: give})
    event_tools.insertEventAfter(fc, 'Event774', nxt)
    event_tools.writeFlow(out('event/PlayerStart.bfevfl'), flow)

    texts(romfs, os.path.join(out_dir, 'romfs'), msbt_mod, [(c['n'], c['key'], c['price']) for c in checks])

    with open(os.path.join(out_dir, 'shopsanity.tsv'), 'a') as f:  # runtime/hle/shopsanity.c
        for c in checks:
            f.write(f"item\t{c['item_id']}\t{flags['LARShop%d' % c['n']]}\t{flags['LARShopSteal%d' % c['n']]}\n")
    with open(os.path.join(out_dir, 'check_flags.tsv'), 'a') as f:  # tracker.py: location -> done flag
        for c in checks:
            f.write(f"{c['loc']}\t{flags['LARShop%d' % c['n']]}\n")
    return [f"  {c['loc']:20} {mods.placements[c['loc']]:22} {c['price']:4} rupees" for c in checks]
