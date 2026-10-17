#!/usr/bin/env python3
"""Boss souls and mini-boss souls (LAS Recomp randomizer options "Boss souls" / "Mini-boss souls", request keys
boss-souls / miniboss-souls).

Each dungeon boss (8 + the Color Dungeon's Evil Orb) and each dungeon mini-boss kind (Rolling Bones, Hinox, ...: one
soul per kind, it counts for every encounter) has a soul in the item pool. Until it's found the boss / mini-boss doesn't
appear, so it can't be beaten and what it guards can't be had:
  runtime   runtime/hle/souls.c (factory hook tools/patches.py BOSS_SPAWN_SITE) refuses to create the boss / mini-boss
            and a boss's reward actor (the instrument; the Color Dungeon's Great Fairy) while the soul flag is clear.
  doors     an arena's shutters follow its boss, so an empty arena would stand open. The shutters from a top-down
            arena onward (to the instrument / the Color Dungeon's fairy) become flag doors at seed time (boss_doors:
            closed until the boss's defeat flag, as before the fight in vanilla); the boss door side is left alone, so
            Link can always leave. Side-view arenas already open their way on by flag (Angler's Tunnel 04B north door
            via 846 <- 842; Eagle's Tower 03G via 939 <- 949); the Eagle's roof stairs back down to 03G (shown only
            after the fight) are made always there, or Link would be stuck on the empty roof.
  walls     mini-boss rooms (any doorway can be the way in): souls.c keeps Link inside, off every wall but the one
            toward the cell he came from, until the defeat flag is set or the soul is found (invisible wall).
  item      an Items.gsheet row per soul (the Secret Medicine's row with the fairy's model, drawn red; gettingFlag = flag), an Item.bfevfl entry
            showing the get message, System.msbt "You got Moldorm's Soul!" (English in every language folder). The
            item-get chains set the flag themselves (LAS item_get / chests, patched for 'BossSoul' / 'MinibossSoul').
  logic     the LAS kill condition (kill-moldorm, kill-hinox...) needs the soul, so every check that names it does too,
            also after tools/rando/room_logic.py rewrites dungeon checks (it keeps non-room atoms). Mini-boss rooms LAS
            has no node for: leaving such a room (other than back) needs its soul - room_logic.EXIT_REQ for shuffled
            dungeons, and for vanilla layouts the same room graph decides which checks lie behind (vanilla_gate).
            The Color Dungeon's fairy rewards get kill-evil-orb (LAS never asked for the boss there).
Overworld mini-bosses ("Mini-bosses among overworld enemies") are never held back: souls only apply in dungeons.
"""
import os
import glob

# dungeon (0 = Color Dungeon), boss, its actor type, LAS kill token, level
BOSSES = [
    (1, 'Moldorm', 0x63, 'kill-moldorm', 'Lv01TailCave'),
    (2, 'Genie', 0x64, 'kill-genie', 'Lv02BottleGrotto'),
    (3, 'Slime Eye', 0x65, 'kill-slime-eye', 'Lv03KeyCavern'),
    (4, 'Angler Fish', 0x66, 'kill-angler', 'Lv04AnglersTunnel'),
    (5, 'Slime Eel', 0x67, 'kill-slime-eel', 'Lv05CatfishsMaw'),
    (6, 'Facade', 0x68, 'kill-facade', 'Lv06FaceShrine'),
    (7, 'Evil Eagle', 0x69, 'kill-evil-eagle', 'Lv07EagleTower'),
    (8, 'Hot Head', 0x6a, 'kill-hothead', 'Lv08TurtleRock'),
    (0, 'Evil Orb', 0x62, 'kill-evil-orb', 'Lv10ClothesDungeon'),
]
# mini-boss kind, its actor type, LAS kill token (None: LAS never names the fight), checks that are its drop / reward
MINIBOSSES = [
    ('Rolling Bones', 0x51, 'kill-rolling-bones', ()),       # D1 06G, D8 07A
    ('Hinox', 0x53, 'kill-hinox', ()),                       # D2 04B, D7 04A, D8 06C
    ('Dodongo Snakes', 0x56, 'kill-dodongo', ('D8-dodongos',)),  # D3 02B, D6 05H, D8 03F
    ('Cue Ball', 0x58, 'kill-cueball', ()),                  # D4 03E, D8 03A
    ('Master Stalfos', 0x59, 'kill-master-stalfos', ()),     # D5 04H, 05F, 01F, 01B (he flees three times)
    ('Gohma', 0x5a, 'kill-gohma', ()),                       # D5 05D
    ('Smasher', 0x5c, None, ()),                             # D6 05F, D8 07H
    ('Grim Creeper', 0x5d, None, ('D7-grim-creeper', 'D7-2F')),  # D7 (one fight, two rooms); D7-2F: the tower top
    ('Blaino', 0x5f, 'kill-blaino', ()),                     # D8 03H
    ('Avalaunch', 0x60, 'kill-stone-hinox', ()),             # D0 06E
    ('Giant Buzz Blob', 0x61, 'kill-blob-boss', ()),         # D0 04G
]
DUNGEON_LEVELS = {'Lv10ClothesDungeon': 0, 'Lv01TailCave': 1, 'Lv02BottleGrotto': 2, 'Lv03KeyCavern': 3,
                  'Lv04AnglersTunnel': 4, 'Lv05CatfishsMaw': 5, 'Lv06FaceShrine': 6, 'Lv07EagleTower': 7,
                  'Lv08TurtleRock': 8}
# Items.gsheet itemIDs nothing else uses (LAS's extra rows: 63-70, 127+, 200+; pets 79-81; shop 230+; vanilla 100-126)
ITEM_IDS = {'boss': list(range(82, 91)), 'mini': list(range(91, 100)) + [103, 107]}
FAIRY_QUEEN = 0x6D0024005CF48AC2  # the Color Dungeon's Great Fairy (Lv10ClothesDungeon_04C): its tunics are the reward
JUNK = ('rupee-50', 'rupee-20', 'rupee-100', 'rupee-300')  # pool junk the souls replace
# the soul's look: the fairy, drawn evil red by the runtime while a soul is received (souls.c souls_fairy_tint)
MODEL = {'model-path': 'ItemFairy.bfres', 'model-name': 'Fairy', 'npc-key': 'ItemFairy'}


def item_key(name, kind='boss'):
    return ('BossSoul' if kind == 'boss' else 'MinibossSoul') + name.replace(' ', '')


def soul_name(name):
    return name + ("' Soul" if name.endswith('s') else "'s Soul")


def las_name(name):
    return 'soul-' + name.lower().replace(' ', '-')


def souls(kinds):
    """[(kind, name, actor type, kill token, dungeon or None)] for the enabled kinds ('boss', 'mini')."""
    out = []
    if 'boss' in kinds:
        out += [('boss', b, t, kill, n) for n, b, t, kill, _ in BOSSES]
    if 'mini' in kinds:
        out += [('mini', m, t, kill, None) for m, t, kill, _ in MINIBOSSES]
    return out


def enabled(settings):
    return [k for k, s in (('boss', 'boss-souls'), ('mini', 'miniboss-souls')) if settings.get(s)]


def _and(logic_defs, loc, atom):
    v = logic_defs.get(loc)
    if not v:
        return
    for k, c in list(v.items()):
        if k.startswith('condition-') and c:
            v[k] = f'({c}) & {atom}'
    if 'condition-basic' not in v:
        v['condition-basic'] = atom


def add_to_logic(logic_defs, item_defs, kinds, cat=None):
    """Souls into the pool (replacing junk, LAS needs one item per location) and into the kill conditions. Mini-boss
    rooms: room_logic.EXIT_REQ (leaving the room needs its soul) for the room graph of shuffled / vanilla dungeons."""
    for kind, name, typ, kill, _ in souls(kinds):
        item_defs[las_name(name)] = dict(MODEL, **{'type': 'important', 'quantity': 1, 'item-key': item_key(name, kind)})
        if kill:
            _and(logic_defs, kill, las_name(name))
    if 'boss' in kinds:
        for loc in ('D0-fairy-1', 'D0-fairy-2'):
            logic_defs[loc]['condition-basic'] += ' & kill-evil-orb'
    if 'mini' in kinds:
        for name, typ, _, locs in MINIBOSSES:
            for loc in locs:
                _and(logic_defs, loc, las_name(name))
        if cat is not None:
            import room_logic
            room_logic.EXIT_REQ.update(exit_req(cat))
    # (only rupees no location keeps vanilla: the blue rupees and some chamber stones stay put when not shuffled)
    for _ in souls(kinds):
        junk = max(JUNK, key=lambda k: item_defs[k]['quantity'])
        assert item_defs[junk]['quantity'] > 0
        item_defs[junk]['quantity'] -= 1
    return len(souls(kinds))


def exit_req(cat):
    """{catalog room: frozenset(soul atoms)} for every cell of a room unit holding a mini-boss."""
    by_type = {t: las_name(m) for m, t, _, _ in MINIBOSSES}
    out = {}
    for room, v in cat.items():
        atoms = {by_type[t] for u in v.get('unit') or [room] for t in cat.get(u, {}).get('types', ()) if t in by_type}
        if atoms:
            out[room] = frozenset(atoms)
    return out


def vanilla_gate(cat, romfs, logic_defs, levels, extra_locations=None):
    """Vanilla-layout dungeons (no room shuffle there): the room graph (room_logic, with EXIT_REQ) tells which cells
    can only be reached by walking on out of a mini-boss room; their checks get those souls."""
    import json
    import dungeon_gen
    import room_logic
    lr = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'location_rooms.json')))
    targets = {k: v.get('room') for k, v in lr.items()}
    targets.update(extra_locations or {})
    added = 0
    for lv in levels:
        layout = {k.rsplit('_', 1)[1]: k for k in cat if k.startswith(lv + '_')}
        edges, _ = room_logic.level_graph(cat, romfs, lv, layout, [], dungeon_gen.openings)
        start = dungeon_gen.ENTRANCES[lv].rsplit('_', 1)[1]
        terms = room_logic.reach_terms(edges, start, room_logic.LEVELS[lv][1])
        need = {}
        for cell, ts in terms.items():
            souls_in = [{a for a in t if a.startswith('soul-')} for t in ts]
            if souls_in:
                common = set.intersection(*souls_in)
                if common:
                    need[cell] = common
        for loc, room in targets.items():
            if room and room.startswith(lv + '_') and loc in logic_defs:
                for atom in sorted(need.get(room.rsplit('_', 1)[1], ())):
                    _and(logic_defs, loc, atom)
                    added += 1
    return added


def _reward_keys(out_romfs, romfs, leb):
    """Instrument actor key per dungeon (LAS writes 'D<n>-instrument' into its parameters)."""
    keys = {0: FAIRY_QUEEN}
    for n, _, _, _, level in BOSSES:
        if not n:
            continue
        for f in _level_files(out_romfs, romfs, level).values():
            with open(f, 'rb') as fh:
                room = leb.Room(fh.read())
            for a in room.actors:
                if f'D{n}-instrument'.encode() in a.parameters:
                    keys[n] = a.key
    return keys


def _level_files(out_romfs, romfs, level):
    files = {os.path.basename(f): f for f in glob.glob(os.path.join(romfs, 'region_common', 'level', level, '*.leb'))}
    files.update({os.path.basename(f): f for f in glob.glob(os.path.join(out_romfs, 'region_common', 'level', level, '*.leb'))})
    return files


def barriers(out_romfs, romfs, leb, cat, kinds):
    """Rooms whose held-back enemy leaves the shutters open: [(level, soul name, defeat flag, cells)]. cells: the
    room's cells as (column, row) of the 15 x 12 grid, from the enemy's position (Eagle's Tower's floors sit side by
    side: names don't match positions there) plus, for a room spanning several cells, the other cell files whose
    actors carry the same room id. Side-view rooms (z = 0 plane: Angler, Evil Eagle) are left out."""
    import collections
    types = {t: (name, kind) for kind, name, t, _, _ in souls(kinds) if kind == 'mini'}
    out = []
    for level in DUNGEON_LEVELS if types else ():
        rooms = {}
        for fname, f in sorted(_level_files(out_romfs, romfs, level).items()):
            with open(f, 'rb') as fh:
                rooms[fname[:-4]] = leb.Room(fh.read())
        room_id = {}
        for room, r in rooms.items():
            ids = collections.Counter(a.roomID for a in r.actors).most_common(1)
            if ids and room[-3:-1].isdigit():
                room_id[(ord(room[-1]) - 65, int(room[-3:-1]) - 1)] = ids[0][0]
        for room, r in rooms.items():
            for a in r.actors:
                if a.type not in types or a.switches[0][0] != 1 or abs(a.posZ) < 0.01:
                    continue
                col, row = int(a.posX // 15), int(a.posZ // 12)
                cells = {(col, row)}
                if room[-3:-1].isdigit():  # same room id in other cells: one room over several cells
                    c0, r0 = ord(room[-1]) - 65, int(room[-3:-1]) - 1
                    cells |= {(col + c - c0, row + rw - r0) for (c, rw), i in room_id.items() if i == a.roomID}
                row_ = (level, types[a.type][0], a.switches[0][1], tuple(sorted(cells)))
                if row_ not in out:
                    out.append(row_)
    return out


def boss_doors(out_romfs, romfs, leb, cat):
    """The shutters from each top-down boss arena onward (to the instrument / the Color Dungeon's fairy) open with the
    boss's defeat flag, like the flag doors of the game (Tail Cave 04C: switch (1, flag), parameters 1, 2, no link),
    instead of following the boss: without the boss they'd stand open. The entry side (the boss door's) is left as it
    is, so Link can always leave. Returns [(room, side)]."""
    out = []
    for n, boss, typ, _, level in BOSSES:
        for fname, f in sorted(_level_files(out_romfs, romfs, level).items()):
            room = fname[:-4]
            with open(f, 'rb') as fh:
                r = leb.Room(fh.read())
            b = next((a for a in r.actors if a.type == typ), None)
            if b is None or b.switches[0][0] != 1:
                continue
            changed = False
            for a in r.actors:  # Eagle's roof: the stairs back down only appear once the fight is over (room flag):
                if a.type == 0x1f7 and a.switches[0][0] == 0:  # always there (they lead back where Link came from)
                    a.switches[0] = (2, 1)
                    out.append((room, 'stairs'))
                    changed = True
            entry = {d[0] for d in cat.get(room, {}).get('doors', []) if 'bossdoor' in d[2]}
            c0, r0 = (ord(room[-1]) - 65) * 15, (int(room[-3:-1]) - 1) * 12 if room[-3:-1].isdigit() else 0
            for a in r.actors:
                if a.type != 0xef or abs(b.posZ) < 0.01:  # ObjClosedDoor (side-view arenas: their way on opens by flag)
                    continue
                dx, dz = a.posX - c0, a.posZ - r0
                side = 'N' if dz < 1.5 else 'S' if dz > 10.5 else 'W' if dx < 1.5 else 'E' if dx > 13.5 else '?'
                if side in entry or side == '?':
                    continue
                a.switches = [(1, b.switches[0][1]), (4, 0), (4, 0), (4, 0)]
                a.parameters = [1, 2] + [0] * (len(a.parameters) - 2)
                a.relationships.section_3 = []
                a.relationships.y = 0
                out.append((room, side))
                changed = True
            if changed:
                p = os.path.join(out_romfs, 'region_common', 'level', level, fname)
                os.makedirs(os.path.dirname(p), exist_ok=True)
                with open(p, 'wb') as fh:
                    fh.write(r.repack())
    return out


def apply(out_dir, romfs, leb, oead_tools, event_tools, msbt, kinds, cat):
    """After LAS's ModsProcess: flags, item rows, get messages, rando/<seed>/souls.tsv for the runtime."""
    out_romfs = os.path.join(out_dir, 'romfs')

    def src(rel):
        p = os.path.join(out_romfs, rel)
        return p if os.path.exists(p) else os.path.join(romfs, rel)

    def dst(rel):
        p = os.path.join(out_romfs, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        return p

    all_souls = souls(kinds)
    # event flags, after LAS's own (before potsanity's 1240+)
    rel = 'region_common/datasheets/GlobalFlags.gsheet'
    sheet = oead_tools.readSheet(src(rel))
    nxt = max(int(v['index']) for v in sheet['values'] if int(v['index']) < 1240) + 1
    flags = {}
    for kind, name, _, _, _ in all_souls:
        flags[name] = nxt
        sheet['values'].append({'symbol': item_key(name, kind), 'index': nxt})
        nxt += 1
    assert nxt <= 1240, nxt
    oead_tools.writeSheet(dst(rel), sheet)

    # item rows: the Secret Medicine's, renamed (the get sequence looks items up by key)
    rel = 'region_common/datasheets/Items.gsheet'
    sheet = oead_tools.readSheet(src(rel))
    base = next(oead_tools.parseStruct(v) for v in sheet['values'] if v['symbol'] == 'SecretMedicine')
    ids = {k: list(v) for k, v in ITEM_IDS.items()}
    for kind, name, _, _, _ in all_souls:
        k = item_key(name, kind)
        row = dict(base, symbol=k, itemID=ids[kind].pop(0), gettingFlag=k, npcKey=MODEL['npc-key'])
        sheet['values'].append(oead_tools.dictToStruct(row))
    oead_tools.writeSheet(dst(rel), sheet)

    # get messages: GenericItemGetSequenceByKey runs the Item.bfevfl entry point named by its messageEntry
    rel = 'region_common/event/Item.bfevfl'
    flow = event_tools.readFlow(src(rel))
    for kind, name, _, _, _ in all_souls:
        k = item_key(name, kind)
        event_tools.addEntryPoint(flow.flowchart, k)
        ev = event_tools.createActionEvent(flow.flowchart, 'Dialog', 'Show', {'message': f'System:Get{k}'})
        event_tools.insertEventAfter(flow.flowchart, k, ev)
    event_tools.writeFlow(dst(rel), flow)
    red, white = b'\x0e\x00\x00\x03\x00\x04\x00\xd9Em\xff', b'\x0e\x00\x00\x03\x00\x04\x00\xed\xed\xed\xff'
    end = b'\x0e\x01\x00\x04\x00\x00\x00\x00'
    n_msg = 0
    for f in glob.glob(os.path.join(romfs, 'region*', '*', 'message', 'System.msbt')):
        rel = os.path.relpath(f, romfs)
        m = msbt.Msbt(open(src(rel), 'rb').read())
        for kind, name, _, _, _ in all_souls:
            tail = f'!\n{name} now awaits you in its lair.' if kind == 'boss' else f'!\n{name} will now stand in your way.'
            m.set(f'Get{item_key(name, kind)}', b'You got ' + red + soul_name(name).encode() + white + tail.encode() + end)
        with open(dst(rel), 'wb') as fh:
            fh.write(m.pack())
        n_msg += 1

    # rando/<seed>/souls.tsv: boss lines (dungeon, actor type, soul flag, name, reward actor key: the layout of the
    # first boss souls seeds), 'mini' lines (actor type, soul flag, name), 'room' lines (level, soul flag, defeat flag,
    # cells c,r;c,r...)
    rewards = _reward_keys(out_romfs, romfs, leb) if 'boss' in kinds else {}
    doors = boss_doors(out_romfs, romfs, leb, cat) if 'boss' in kinds else []
    walls = barriers(out_romfs, romfs, leb, cat, kinds)
    with open(os.path.join(out_dir, 'souls.tsv'), 'w') as f:
        for kind, name, typ, _, n in all_souls:
            if kind == 'boss':
                f.write(f'{n}\t{typ:#x}\t{flags[name]}\t{name}\t{rewards.get(n, 0):#x}\n')
            else:
                f.write(f'mini\t{typ:#x}\t{flags[name]}\t{name}\n')
        for level, name, defeat, cells in walls:
            f.write(f'room\t{level}\t{flags[name]}\t{defeat}\t' + ';'.join(f'{c},{r}' for c, r in cells) + '\n')
    return (f'{len(all_souls)} souls (flags {min(flags.values())}-{max(flags.values())}), {len(rewards)} rewards held '
            f'back, onward doors shut until the boss is beaten: {", ".join(f"{r[-3:]} {d}" for r, d in doors)}; '
            f'{len(walls)} rooms walled while their enemy is missing, {n_msg} message files')


def write_spots(out_dir, leb, placements):
    """'spot' lines in souls.tsv: where a soul's fairy model stands in the world (standing items, the hidden items of
    pots / bushes: LAS-style item actors whose parameters are model file, model, location...). The runtime draws
    fairies there red (real fairies elsewhere stay pink). Run last: room shuffle moves rooms (and their actors)."""
    bosses = {las_name(b) for _, b, _, _, _ in BOSSES}
    spots = []
    for f in sorted(glob.glob(os.path.join(out_dir, 'romfs', 'region_common', 'level', '*', '*.leb'))):
        level = os.path.basename(os.path.dirname(f))
        with open(f, 'rb') as fh:
            data = fh.read()
        if MODEL['model-path'].encode() not in data:
            continue
        for a in leb.Room(data).actors:
            p = a.parameters
            if len(p) > 2 and p[0] == MODEL['model-path'].encode() and isinstance(p[2], bytes):
                item = placements.get(p[2].decode(errors='ignore'), '')
                if str(item).startswith('soul-'):
                    spots.append(f'spot\t{level}\t{a.posX:.2f}\t{a.posY:.2f}\t{a.posZ:.2f}\t{0 if item in bosses else 1}\n')
    # the Tool Shop's shelf / the Trendy Game's floor show their goods as models of their own (no real fairy there):
    # every fairy in that level is drawn red, boss red when any of its souls is a boss's
    for level, prefix in (('Shop', 'shop-slot'), ('CraneGame', 'trendy-prize')):
        here = [v for k, v in placements.items() if str(k).startswith(prefix) and str(v).startswith('soul-')]
        if here:
            spots.append(f'zone\t{level}\t{0 if any(v in bosses for v in here) else 1}\n')
    with open(os.path.join(out_dir, 'souls.tsv'), 'a') as fh:
        fh.writelines(spots)
    return len(spots)
