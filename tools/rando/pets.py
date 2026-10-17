#!/usr/bin/env python3
"""Pets (LAS Recomp addition): companions found as items, like any other item in the pool.

  Rooster  (setting pet-rooster): logic token `rooster` (LAS logic already uses it wherever the Flying Rooster can stand
           in for the feather or hookshot, inside dungeons only with bad-pets = "Companions allowed in dungeons").
           The vanilla revival at the rooster's grave in Mabe still works but leaves the logic.
  Ghost    (setting pet-ghost): an Items-page item, NOT a follower (Link has one companion slot: a following Ghost
           sent the Rooster / Marin away). Found, he sits in the equipment panel's free slot (runtime/ui/
           inventory_overlay.mm) and the grave takes him: reaching it (its vanilla EventTriggerTag, Graveyard) lays him
           to rest (Ghost4_Clear) and his Owl flies in with the grave check (`ghost-reward`); the slot then shows a
           tick (EighthClear). His house visit is skipped (Ghost2/3 flags). The Surf Harp no longer finishes the
           Ghost's quest, and he no longer appears by himself after the Harp (GhostJoinCheck).
  Marin    (open-desert off): the Walrus sleeps in front of Yarna Desert until Marin sings to him (vanilla event),
           so the desert needs Marin found. With open-desert on (LAS default: WalrusAwaked from the start) Marin
           isn't in the pool.

A pet item is a dummy Items.gsheet entry (the companion's model on the chest / in Link's hands) that reaches the
game's AddItem with index PET_INDEX + n; the runtime (runtime/hle/pets.c) turns that into the companion following
Link (save +0x555D; the Ghost: no) and remembers it (event flag FLAGS[n]) so the pause menu can call any found pet
back. Seeds mark the Ghost item in settings.json ("ghost-item"; older seeds: the Ghost follows, as before).
"""

PET_INDEX = 90
# itemIDs the dummies carry: CARRIER_ID + n (79-81, unused in the vanilla table). Checked in game: Link holds up the
# npcKey's model (the pet) and shows the Item.bfevfl entry named after the item key (added below; vanilla has none).
# ClothesGreen (17) showed "You got the green tunic!" and no model. One ID per pet: the Seashell Mansion present turns
# its itemKey into the ID and back into the FIRST key with that ID (LAS: why traps have their own IDs).
CARRIER_ID = 79
# item name (= logic token) -> (Items.gsheet symbol, npcKey, model file, model name, setting that puts it in the pool)
PETS = {
    'rooster': ('PetRooster', 'NpcFlyingCucco', 'NpcFlyingCucco.bfres', 'FlyingCucco'),
    'marin-companion': ('PetMarin', 'NpcMarin', 'NpcMarin.bfres', 'Marin'),
    'ghost': ('PetGhost', 'NpcGhost', 'NpcGhost.bfres', 'Ghost'),
}
ORDER = ['rooster', 'marin-companion', 'ghost']  # n in PET_INDEX + n (runtime/hle/pets.c kPets)
# runtime flags (unused vanilla GlobalFlags; flags.tsv via seed_tables.py EXACT): found rooster / Marin / Ghost,
# BowWow sent away by a pet (so he can be called back)
FLAGS = ['unused0427', 'unused0428', 'unused0429', 'unused0430']


def enabled(settings):
    out = []
    if settings.get('pet-rooster'):
        out.append('rooster')
    if not settings.get('open-desert', True):
        out.append('marin-companion')
    if settings.get('pet-ghost'):
        out.append('ghost')
    return out


def add_to_pool(logic, item_defs, settings):
    """Pet items into the pool (one 50-rupee less each) and the logic changes they make. Returns the pets."""
    pets = enabled(settings)
    for p in pets:
        sym, npc, path, model = PETS[p]
        item_defs[p] = {'type': 'important', 'quantity': 1, 'item-key': sym, 'model-path': path, 'model-name': model,
                        'npc-key': npc}
        item_defs['rupee-50']['quantity'] -= 1
    if 'rooster' in pets:
        del logic['rooster-statue']  # (LAS: the rooster follower location; the pet is the rooster now)
    if 'ghost' in pets:
        assert logic['ghost-reward'].get('condition-basic') == 'surf-harp'
        logic['ghost-reward']['condition-basic'] = 'ghost'
    if 'marin-companion' in pets:
        assert logic['desert'].get('condition-basic') == 'animal-village & break-bush'
        logic['desert']['condition-basic'] = 'animal-village & break-bush & marin-companion'  # (the Walrus)
    return pets


def mark_indexes(placements, logic):
    idx = placements.setdefault('indexes', {})
    n = 0
    for loc, item in list(placements.items()):
        if logic.get(loc, {}).get('type') == 'item' and item in ORDER:
            idx[loc] = PET_INDEX + ORDER.index(item)
            n += 1
    return n


def apply(out_dir, romfs, pets, oead_tools, event_tools):
    """After LAS's ModsProcess: the dummy item entries, the Walrus without Marin. (In LAS: no WalrusAwaked at the
    start without open-desert (player_start.py), GhostJoinCheck off and no Ghost4_Clear from the Surf Harp with
    pet-ghost (conditions.py, item_get.py GHOST_PET).)"""
    import os
    ds = os.path.join(out_dir, 'romfs', 'region_common', 'datasheets')

    def path(sub, name):
        p = os.path.join(out_dir, 'romfs', 'region_common', sub, name)
        return p if os.path.exists(p) else os.path.join(romfs, 'region_common', sub, name)

    sheet = oead_tools.readSheet(path('datasheets', 'Items.gsheet'))
    # (as LAS's own dummy entries: a YoshiDoll copy - that item actor shows the npcKey's model)
    d = oead_tools.parseStruct(next(v for v in sheet['values'] if v['symbol'] == 'YoshiDoll'))  # (parsed once,
    for p in pets:                                                                               # like LAS does)
        sym, npc = PETS[p][:2]
        d.update(symbol=sym, itemID=CARRIER_ID + ORDER.index(p), npcKey=npc, gettingFlag='')
        sheet['values'].append(oead_tools.dictToStruct(d))
    os.makedirs(ds, exist_ok=True)
    oead_tools.writeSheet(os.path.join(ds, 'Items.gsheet'), sheet)
    ev = os.path.join(out_dir, 'romfs', 'region_common', 'event')
    os.makedirs(ev, exist_ok=True)
    if 'ghost' in pets:
        # the grave's trigger (Field_07E EventTriggerTag Graveyard: companion Ghost, Ghost3_Clear, !Ghost4_Clear)
        # takes the Ghost ITEM (found flag) instead of the following Ghost
        cs = oead_tools.readSheet(path('datasheets', 'Conditions.gsheet'))
        g = next(v for v in cs['values'] if v['symbol'] == 'Graveyard')
        assert g['conditions'][0]['category'] == 3 and g['conditions'][0]['parameter'] == 'Ghost'
        g['conditions'][0] = {'category': 1, 'parameter': FLAGS[ORDER.index('ghost')]}  # (as LAS conditions.py)
        oead_tools.writeSheet(os.path.join(ds, 'Conditions.gsheet'), cs)
        # Graveyard_talk walks the Ghost to the grave, says goodbye, gives the vanilla bottle (Bottle 0) and leaves
        # the companion slot: with no Ghost actor only its Ghost4_Clear stays (the Owl, ghost-reward, then flies in)
        flow = event_tools.readFlow(path('event', 'Field.bfevfl'))
        e6 = event_tools.findEvent(flow.flowchart, 'Event6')
        assert e6.data.params.data['symbol'] == 'Ghost4_Clear'
        event_tools.insertEventAfter(flow.flowchart, 'Graveyard_talk', 'Event6')
        event_tools.removeEventAfter(flow.flowchart, 'Event6')
        event_tools.writeFlow(os.path.join(ev, 'Field.bfevfl'), flow)
    if 'marin-companion' in pets:
        # talking to the sleeping Walrus without Marin sets WalrusTalked (vanilla: Marin then moves to the Animal
        # Village and the trade NPCs change): skip it, only Marin's song (WalrusAwaked) changes anything
        flow = event_tools.readFlow(path('event', 'Walrus.bfevfl'))
        e12 = event_tools.findEvent(flow.flowchart, 'Event12')
        assert e12.data.params.data['symbol'] == 'WalrusTalked'
        event_tools.insertEventAfter(flow.flowchart, 'Event123', 'Event58')
        event_tools.writeFlow(os.path.join(ev, 'Walrus.bfevfl'), flow)
    # the get message: GenericItemGetSequenceByKey runs the Item.bfevfl entry named messageEntry - the item key for
    # constant gives (item_get) and for chests / presents ('' = the key) - so one entry per pet covers every source
    flow = event_tools.readFlow(path('event', 'Item.bfevfl'))
    for p in pets:
        sym = PETS[p][0]
        if event_tools.findEntryPoint(flow.flowchart, sym) is None:
            event_tools.addEntryPoint(flow.flowchart, sym)
            event_tools.createActionChain(flow.flowchart, sym, [('Dialog', 'Show', {'message': f'System:{sym}'})])
    event_tools.writeFlow(os.path.join(ev, 'Item.bfevfl'), flow)
    write_messages(out_dir, romfs, pets)
    return len(pets)


# ---- "<pet> joins you!" in every language's System.msbt. The name and the colour / end-of-box tags come from the
# game's own text (Glossary.msbt, System.msbt GetSmallKey); the words around the name are ours: English in full, other
# languages a short "New companion: <name>!" (gendered where the language needs it; Marin is the one female pet).
NAME_LABEL = {'rooster': 'glossary_sequence16', 'marin-companion': 'glossary_npc01', 'ghost': 'glossary_npc41'}
EN_ARTICLE = {'rooster': 'The ', 'marin-companion': '', 'ghost': 'The '}
# language (folder name minus the region) -> ((before the name, after it) male, female); {i} = Korean subject particle
TEMPLATES = {
    'de': (('Neuer Gefährte: ', '!'), ('Neue Gefährtin: ', '!')),
    'fr': (('Nouveau compagnon : ', ' !'), ('Nouvelle compagne : ', ' !')),
    'es': (('¡Nuevo compañero: ', '!'), ('¡Nueva compañera: ', '!')),
    'it': (('Nuovo compagno: ', '!'), ('Nuova compagna: ', '!')),
    'nl': (('Nieuwe metgezel: ', '!'), ('Nieuwe metgezel: ', '!')),
    'ru': (('Новый спутник: ', '!'), ('Новая спутница: ', '!')),
    'ja': (('', 'が なかまに なった！'), ('', 'が なかまに なった！')),
    'kr': (('', '{i} 동료가 되었다!'), ('', '{i} 동료가 되었다!')),
}
CH = {'CN': ('', '成为了伙伴！'), 'TW': ('', '成為了夥伴！')}  # Chinese by region: simplified / traditional


def compose(pet, lang, name_raw, get_raw):
    """lang = message folder (USen, EUfr, JPja, CNch_Validation, ...); raw MSBT text with the terminating NUL"""
    from keysanity_text import _tokens, COLOR, WAIT
    t = _tokens(get_raw)
    colors = [r for k, g, r in t if g == COLOR]
    on, off = (colors[0], colors[1]) if len(colors) >= 2 else (b'', b'')
    wait = next((r for k, g, r in reversed(t) if g == WAIT), b'')
    name = b''.join(r for k, g, r in _tokens(name_raw)).decode('utf-8')
    code = lang[2:4]
    if code == 'ch':
        pre, post = CH[lang[:2]]
    elif TEMPLATES.get(code):
        pre, post = TEMPLATES[code][pet == 'marin-companion']
        if lang.startswith('US'):  # (USfr writes "clé!", EUfr "clé !")
            post = post.lstrip()
    else:  # English (and any language without a template)
        return EN_ARTICLE[pet].encode() + on + name.encode() + off + b' joins you!' + wait + b'\0'
    if name[:1].islower():  # (glossary names like "fantôme" are lower case)
        name = name[0].upper() + name[1:]
    if '{i}' in post:  # 이 after a final consonant, else 가
        last = ord(name[-1]) - 0xAC00
        post = post.replace('{i}', '이' if 0 <= last < 11172 and last % 28 else '가')
    return pre.encode() + on + name.encode() + off + post.encode() + wait + b'\0'


def write_messages(out_dir, romfs, pets):
    import glob
    import os
    from keysanity_text import _read
    from msbt import Msbt
    n = 0
    for sysp in sorted(glob.glob(os.path.join(romfs, 'region*', '*', 'message', 'System.msbt'))):
        rel = os.path.relpath(sysp, romfs)
        lang = rel.split(os.sep)[1]
        sysm = Msbt(_read(out_dir, romfs, rel))
        names = Msbt(_read(out_dir, romfs, rel.replace('System.msbt', 'Glossary.msbt'))).texts
        for p in pets:
            if NAME_LABEL[p] in names:
                sysm.set(PETS[p][0], compose(p, lang, names[NAME_LABEL[p]], sysm.texts['GetSmallKey']))
                n += 1
        out = os.path.join(out_dir, 'romfs', rel)
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(out, 'wb') as f:
            f.write(sysm.pack())
    return n


if __name__ == '__main__':  # preview: pets.py <romfs>  (prints the composed texts for every language)
    import glob
    import os
    import sys
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from msbt import Msbt, readable
    for sysp in sorted(glob.glob(os.path.join(sys.argv[1], 'region*', '*', 'message', 'System.msbt'))):
        lang = sysp.split(os.sep)[-3]
        names = Msbt(open(sysp.replace('System.msbt', 'Glossary.msbt'), 'rb').read()).texts
        get = Msbt(open(sysp, 'rb').read()).texts['GetSmallKey']
        print(f'{lang:16}', ' | '.join(readable(compose(p, lang, names[NAME_LABEL[p]], get)) for p in ORDER))
