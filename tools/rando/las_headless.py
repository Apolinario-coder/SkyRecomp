#!/usr/bin/env python3
"""Headless driver for LAS-Randomizer (third_party/LAS-Randomizer, MIT, by Owen-Splat and contributors).

Runs the item shuffler and the mod generator without the Qt GUI, using GUI-equivalent defaults.

usage: las_headless.py <romfs dir> <out dir> <settings.json>
settings.json: {"seed": "...", "logic": "basic", ...any LAS setting key..., "exclude": [...]} -- keys missing
from the file get the LAS GUI defaults. The output dir receives romfs/ (replacement files), exefs patches
(1.0.0.pchtxt) and the spoiler log.
"""
import json
import os
import re
import sys
import types

LAS = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'third_party', 'LAS-Randomizer')
LAS = os.path.normpath(LAS)
if getattr(sys, 'frozen', False):  # standalone helper (tools/rando/rando_gen.spec): LAS + tools/rando data in the bundle
    LAS = sys._MEIPASS


# ---- minimal PySide6.QtCore stand-in: the core only uses QThread.run() and Signal.emit/connect
class _Signal:
    def __init__(self, *types_):
        self._slots = []

    def __get__(self, obj, objtype=None):
        if obj is None:
            return self
        key = '_sig_%d' % id(self)
        if key not in obj.__dict__:
            obj.__dict__[key] = _BoundSignal()
        return obj.__dict__[key]


class _BoundSignal:
    def __init__(self):
        self.slots = []

    def connect(self, f):
        self.slots.append(f)

    def emit(self, *a):
        for f in self.slots:
            f(*a)


class _QThread:
    def __init__(self, parent=None):
        pass

    def setParent(self, p):
        pass

    def start(self):
        self.run()


qtcore = types.ModuleType('PySide6.QtCore')
qtcore.QThread = _QThread
qtcore.Signal = _Signal
qtcore.QObject = object
pyside = types.ModuleType('PySide6')
pyside.QtCore = qtcore
sys.modules['PySide6'] = pyside
sys.modules['PySide6.QtCore'] = qtcore

ARGS = [os.path.abspath(a) for a in sys.argv[1:4]]
sys.argv[0] = os.path.join(LAS, 'randomizer.py')  # LAS resolves its data paths from argv[0]
sys.path.insert(0, LAS)
os.chdir(LAS)

import yaml  # noqa: E402
from RandomizerCore import randomizer_data as rd  # noqa: E402
from RandomizerCore.shuffler import ItemShuffler  # noqa: E402
from RandomizerCore.mod_generator import ModsProcess  # noqa: E402


def default_settings(seed):
    excluded = set()
    excluded.update(rd.FISHING_REWARDS)
    excluded.update(rd.RAPIDS_REWARDS)
    excluded.update(rd.DAMPE_REWARDS)
    excluded.update(rd.TRADE_GIFT_LOCATIONS)
    excluded.update(rd.BLUE_RUPEES)
    excluded.update(['30-seashell-reward', '40-seashell-reward', '50-seashell-reward'])
    return {
        'seed': seed, 'logic': 'basic', 'platform': 'emulator', 'create-spoiler': True, 'free-book': True,
        'extended-consumable-drop': True, 'dungeon-items': 'none', 'unlocked-bombs': True, 'shuffle-bombs': False,
        'shuffle-powder': False, 'fast-fishing': False, 'fast-stealing': True, 'fast-songs': False,
        'shuffle-instruments': False, 'starting-instruments': 0, 'bad-pets': False, 'open-kanalet': True,
        'open-bridge': True, 'open-mamu': True, 'traps': 'few', 'blupsanity': False, 'classic-d2': False,
        'owl-overworld-gifts': False, 'owl-dungeon-gifts': False, 'fast-stalfos': False, 'chest-aspect': 'default',
        'seashells-important': True, 'trade-important': False, 'randomize-music': False, 'open-mabe': False,
        'boss-cutscenes': True, 'randomize-enemies': False, 'randomize-enemy-sizes': False, 'shuffle-dungeons': False,
        'blur-removal': False, 'OHKO': False, 'lv1-beam': False, 'nice-rod': True, 'nice-bombs': False,
        'stealing': 'always', 'fast-chests': True, 'fast-keys': True, 'keysanity': 'off', 'randomize-environments': False,
        'skip-story-events': True, 'tunic-color': 'default', 'enemy-minibosses': False, 'no-buff-drops': False,
        'bushes': False, 'boss-souls': False, 'miniboss-souls': False,
        'digs': False,  # digsanity: every diggable overworld tile is a check (tools/rando/digs.py)
        'hookshot-anything': False,  # the hookshot latches onto anything (tools/rando/hookshot_logic.py)
        'dungeon-room-shuffle': 'off',  # off / coherent / chaos / madness (tools/rando/dungeon_gen.py)
        'pet-rooster': False, 'pet-ghost': False, 'open-desert': True,  # pets (tools/rando/pets.py)
        'shopsanity': 'off', 'trendsanity': False,  # tools/rando/shopsanity.py (off / vanilla / random prices), trendsanity.py
        'starting-items': ['sword', 'shield', 'ocarina', 'song-mambo'], 'starting-rupees': 0,
        'excluded-locations': excluded,
    }


import time as _time
_T0 = _time.time()


def stage(label, frac):
    """Progress line for the in-game menu (runtime/ui/rando_tab.mm): `stage <fraction> <label>`."""
    print(f'stage {frac:.3f} {label}', flush=True)


def shuffle_dungeon_rooms(romfs, out_dir, mode, layouts, jumps):
    """Dungeon room shuffle (Coherent / Chaos, tools/rando/dungeon_gen.py) on top of the seed: the generator reads
    rooms from an overlay romfs where the seed's own room files (LAS chests, pots, bushes, entrances) replace the
    game's - as copies, since it writes the seed's rooms while still reading others."""
    import shutil
    import dungeon_gen

    def link(src, dst):  # symlink; Windows (no symlink rights): directory junction / hard link / copy
        src = os.path.abspath(src)
        try:
            os.symlink(src, dst, target_is_directory=os.path.isdir(src))
        except OSError:
            if os.path.isdir(src):
                import _winapi
                _winapi.CreateJunction(src, dst)
            else:
                try:
                    os.link(src, dst)
                except OSError:
                    shutil.copyfile(src, dst)
    out_romfs = os.path.join(out_dir, 'romfs')
    overlay = os.path.join(out_dir, '.room_overlay')
    shutil.rmtree(overlay, ignore_errors=True)
    rc = os.path.join(overlay, 'region_common')
    os.makedirs(os.path.join(rc, 'level'))
    for e in os.listdir(romfs):
        if e != 'region_common':
            link(os.path.join(romfs, e), os.path.join(overlay, e))
    src_rc = os.path.join(romfs, 'region_common')
    for e in os.listdir(src_rc):
        if e != 'level':
            link(os.path.join(src_rc, e), os.path.join(rc, e))
    for lv in os.listdir(os.path.join(src_rc, 'level')):
        src_lv = os.path.join(src_rc, 'level', lv)
        seed_lv = os.path.join(out_romfs, 'region_common', 'level', lv)
        if not os.path.isdir(seed_lv):
            link(src_lv, os.path.join(rc, 'level', lv))
            continue
        dst = os.path.join(rc, 'level', lv)
        os.makedirs(dst)
        for f in os.listdir(src_lv):
            link(os.path.join(src_lv, f), os.path.join(dst, f))
        for f in os.listdir(seed_lv):  # the seed's version, copied (a snapshot)
            if os.path.lexists(os.path.join(dst, f)):
                os.remove(os.path.join(dst, f))
            shutil.copyfile(os.path.join(seed_lv, f), os.path.join(dst, f))
    try:
        dungeon_gen.write_all(overlay, out_romfs, layouts, jumps)  # (jumps None: Coherent)
    finally:
        shutil.rmtree(overlay, ignore_errors=True)
    return f"{mode}, {len(layouts)} dungeons, {sum(sum(1 for k, v in L.items() if k != v) for L in layouts.values())} rooms moved"


def main():
    romfs, out_dir, settings_file = ARGS
    stage('Preparing', 0.01)
    with open(settings_file) as f:
        user = json.load(f)
    settings = default_settings(str(user.get('seed', 'LARecomp')))
    for k, v in user.items():
        if k == 'exclude':
            settings['excluded-locations'].update(v)
        elif k == 'include':
            settings['excluded-locations'].difference_update(v)
        else:
            settings[k] = v
    settings['excluded-locations'] = set(settings['excluded-locations'])
    # optional location groups (Randomizer tab): included = real checks with logic instead of junk
    groups = {'include-fishing': rd.FISHING_REWARDS, 'include-rapids': rd.RAPIDS_REWARDS,
              'include-dampe': rd.DAMPE_REWARDS, 'include-trade-gifts': rd.TRADE_GIFT_LOCATIONS,
              'include-blue-rupees': rd.BLUE_RUPEES}
    for k, locs in groups.items():
        if user.get(k):
            settings['excluded-locations'].difference_update(locs)
    if user.get('include-blue-rupees'):
        settings['blupsanity'] = True
    logic_defs = yaml.safe_load(rd.LOGIC_RAW)
    import copy
    item_defs = copy.deepcopy(rd.ITEM_DEFS)
    # Escape Bottle (runtime/hle/escape.c): every seed starts with the game's third bottle as a warp item, so the pool
    # has one Bottle less (the logic only ever needs one), a 50 Rupees in its place. Not a logic item: nothing may
    # require it (the seed is marked only in the settings.json written at the end, never in LAS's access).
    item_defs['bottle']['quantity'] -= 1
    item_defs['rupee-50']['quantity'] += 1
    os.makedirs(out_dir, exist_ok=True)
    import shutil
    if os.path.isdir(os.path.join(out_dir, 'romfs')):  # regenerating into the same folder: start from vanilla files
        shutil.rmtree(os.path.join(out_dir, 'romfs'))
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from RandomizerCore.Tools import leb
    if settings.get('hookshot-anything'):  # new routes before anything reads the logic (room shuffle, entrances)
        import hookshot_logic
        print('hookshot anything:', hookshot_logic.apply(logic_defs), 'routes', flush=True)

    # keysanity: the freed dungeon items must be placed with logic (LAS only logic-checks 'important' items)
    ks_free = {'keys': ('key-',), 'keys+nk': ('key-', 'nightmare-key-'),
               'all': ('key-', 'nightmare-key-', 'map', 'compass', 'stone-beak')}.get(settings.get('keysanity', 'off'), ())
    for k, d in item_defs.items():
        if ks_free and k.startswith(ks_free) and k[-2:-1] == 'D':
            d['type'] = 'important'

    import souls as souls_mod  # boss / mini-boss souls: it only appears once its soul is found (tools/rando/souls.py)
    soul_kinds = souls_mod.enabled(settings)
    soul_cat = None
    if soul_kinds:
        from RandomizerCore.Randomizers import chests as las_chests
        soul_cat = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'room_catalog.json')))
        print('souls:', souls_mod.add_to_logic(logic_defs, item_defs, soul_kinds, soul_cat), 'in the pool', flush=True)
        las_chests.SOUL_KEYS[:] = [souls_mod.item_key(s[1], s[0]) for s in souls_mod.souls(soul_kinds)]

    pot_locs = {}
    if settings.get('pots'):
        import pots as pots_mod
        with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'entrance_data.yml')) as f:
            entrance_data = yaml.safe_load(f)
        import random as _rnd
        pot_locs, skipped = pots_mod.add_to_logic(pots_mod.scan(romfs, leb), logic_defs, item_defs, entrance_data,
                                                  settings.get('traps', 'none'), _rnd.Random(f"{settings['seed']}-pots"))
        print('pots:', len(pot_locs), 'locations', ('(no logic for: ' + ', '.join(sorted(skipped)) + ')') if skipped else '',
              flush=True)

    bush_locs = {}
    if settings.get('bushes'):  # bushsanity: every overworld bush hides an item (tools/rando/bushes.py)
        import bushes as bushes_mod
        import random as _rnd
        bush_locs = bushes_mod.add_to_logic(bushes_mod.scan(romfs, leb, open_mabe=settings.get('open-mabe')), logic_defs, item_defs,
                                            settings.get('traps', 'none'), _rnd.Random(f"{settings['seed']}-bushes"))
        print('bushes:', len(bush_locs), 'locations', flush=True)

    dig_locs, dig_tiles = {}, []
    if settings.get('digs'):  # digsanity: every diggable overworld tile is a check (tools/rando/digs.py)
        import digs as digs_mod
        dig_tiles = digs_mod.scan(romfs, leb)
        dig_locs = digs_mod.add_to_logic(dig_tiles, logic_defs, item_defs)
        print('digs:', len(dig_locs), 'locations of', len(dig_tiles), 'dig tiles', flush=True)

    import shopsanity
    import trendsanity
    for f in ('shopsanity.tsv', 'check_flags.tsv'):  # (appended to by both modules)
        if os.path.exists(os.path.join(out_dir, f)):
            os.remove(os.path.join(out_dir, f))
    sanity_inside = {}  # their locations inside shuffled interiors: {door: [(location, condition)]}
    if shopsanity.enabled(settings):
        sanity_inside.update(shopsanity.add_to_logic(logic_defs, item_defs, settings))
    if trendsanity.enabled(settings):
        sanity_inside.update(trendsanity.add_to_logic(logic_defs, item_defs, settings))

    import pets as pets_mod  # companions as items (tools/rando/pets.py)
    pets = pets_mod.add_to_pool(logic_defs, item_defs, settings)
    if pets:
        print('pets:', ', '.join(pets), flush=True)

    # ---- LAS Recomp entrance randomizer: dungeon entrances are LAS's own 'shuffle-dungeons' (its own lever in the
    # menu); tier 2 = single-entrance interiors and connector caves, with or without the dungeons (tier 1 = the old
    # "dungeons only" level: turns shuffle-dungeons on; tier 3 was "+ connector caves" on its own: same as 2 now)
    tier = settings['entrance-tier'] = min(int(settings.get('entrance-tier', 0)), 2)
    ent = None
    if tier == 1:
        settings['shuffle-dungeons'] = True
    if tier >= 2:
        import random as _random
        import entrance_logic as el
        ed = el.load()
        decoupled = bool(settings.get('entrance-decoupled', False))
        # (decoupled with the desert shut by the sleeping Walrus: its doors stay vanilla - entrance_logic.pool)
        ent_doors = el.pool(ed, ('desert',) if decoupled and not settings.get('open-desert', True) else ())
        ent_entry, ent_exit = el.shuffle(ent_doors, decoupled, _random.Random(f"{settings['seed']}-entrances"))
        extra = {k: list(v) for k, v in sanity_inside.items()}
        lv_door = {d['dest_level']: d['id'] for d in ent_doors}
        for name, p in pot_locs.items():  # pots inside shuffled interiors follow their interior
            if p['level'] in lv_door:
                extra.setdefault(lv_door[p['level']], []).append((name, 'break-pot'))
        # connector caves: their own pool, logic first (house exits are ORed into regions it rewrites)
        cdoors = el.connector_pool(ed)
        lv_node = el.connector_levels(cdoors)
        cextra = {}
        for name, p in pot_locs.items():
            if p['level'] in lv_node:
                cextra.setdefault(lv_node[p['level']], []).append((name, 'break-pot'))
        pockets = el.pocket_screens(cdoors)

        def with_caves(cv):
            lg = copy.deepcopy(logic_defs)
            n = el.apply_connector_logic(lg, ed, *cv, cextra) if cv[0] else 0
            for name, b in bush_locs.items():  # a bush on a pocket's screen may stand in the pocket: needs it
                if cv[0] and pockets.get(b['room']) and name in lg:
                    lg[name]['condition-basic'] = el._and(lg[name].get('condition-basic'), pockets[b['room']])
            return lg, n + el.apply_logic(lg, ent_doors, ent_entry, ent_exit, extra)

        items = lambda lg: {k for k in el.reach_all(lg, item_defs, settings) if lg[k].get('type') == 'item'}
        want = items(with_caves(([], {}, {}))[0])  # (no caves shuffled)
        for attempt in range(100):  # re-roll a cave layout that loses a location even with every item
            rng = _random.Random(f"{settings['seed']}-connectors" + (f'-{attempt}' if attempt else ''))
            cave = (cdoors,) + el.shuffle(cdoors, decoupled, rng)
            lg, moved = with_caves(cave)
            if not want - items(lg):
                break
            print(f'caves: layout {attempt} loses', ', '.join(sorted(want - items(lg))[:6]), flush=True)
        else:
            raise RuntimeError('no connector cave layout keeps every location reachable')
        logic_defs.clear()
        logic_defs.update(lg)
        ent = (ent_doors + cave[0], {**ent_entry, **cave[1]}, {**ent_exit, **cave[2]})
        print('entrances:', len(ent_entry), 'doors +', len(cave[1]), 'cave doors shuffled,', moved, 'locations moved', flush=True)

    # ---- Dungeon room shuffle (Coherent / Chaos): the layouts are planned first, the item logic follows them
    # (tools/rando/room_logic.py), the same layouts are written into the seed after LAS's files
    room_mode = settings.get('dungeon-room-shuffle', 'off')
    room_plan = None
    if room_mode in ('coherent', 'chaos', 'madness'):
        stage('Planning dungeon rooms', 0.03)
        import dungeon_gen
        import room_logic
        cat = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'room_catalog.json')))
        extra = {name: p['room'] for name, p in pot_locs.items() if p.get('room')}
        base = (copy.deepcopy(logic_defs), copy.deepcopy(item_defs), copy.deepcopy(settings))
        layouts = jumps = None
        for attempt in range(40):  # a layout whose dungeons can't be filled: the next one (same seed ID, same result)
            sub = settings['seed'] if attempt == 0 else f"{settings['seed']}~{attempt}"
            if room_mode in ('chaos', 'madness'):  # (Madness: Chaos with one-way doorways)
                try:
                    layouts, jumps = dungeon_gen.plan_all_chaos(cat, sub, romfs, madness=room_mode == 'madness',
                                                                  settings=settings)
                except RuntimeError as e:  # (no round left every dungeon room for its keys and items: next one)
                    print('room layout:', e, '- planning another', flush=True)
                    continue
                logic_rows = jumps
            else:
                layouts, jumps = dungeon_gen.plan_all(cat, sub, romfs), None
                logic_rows = [row for lv, L in layouts.items() for row in dungeon_gen._jumps(cat, lv, L)]
            logic_defs, item_defs, settings = (copy.deepcopy(x) for x in base)
            print('room logic:', room_logic.apply(cat, romfs, layouts, logic_rows, logic_defs, item_defs, settings, extra),
                  flush=True)
            why = settings.pop('room-logic-unfillable', None)
            if not why:
                break
            print('room layout can\'t be filled -', why, '- planning another', flush=True)
        if layouts is None:
            raise RuntimeError('no dungeon room layout for this seed')
        room_plan = (layouts, jumps)
        if os.environ.get('LARECOMP_PLAN_ONLY'):  # (test: layouts + room logic only; =<names>: print those conditions)
            for k in os.environ['LARECOMP_PLAN_ONLY'].split(','):
                if k in logic_defs:
                    print(k, '=', logic_defs[k].get('condition-basic'), flush=True)
            sys.exit(0)
    if 'mini' in soul_kinds:  # mini-boss rooms of vanilla-layout dungeons: what lies behind needs the soul
        import dungeon_gen
        vanilla = [lv for lv in dungeon_gen.ENTRANCES if not room_plan or lv not in room_plan[0]]
        print('mini-boss souls on vanilla dungeons:', souls_mod.vanilla_gate(
            soul_cat, romfs, logic_defs, vanilla, {n: p['room'] for n, p in pot_locs.items() if p.get('room')}),
            'conditions', flush=True)

    result = {}
    errors = []
    stage('Shuffling items', 0.04)
    sh = ItemShuffler(out_dir, settings['seed'], settings['logic'], settings, item_defs, copy.deepcopy(logic_defs))
    # Speed: LAS rebuilds each condition with regexes and eval()s the string on every check (millions of times
    # per seed). Same semantics, compiled once: condition string -> code object evaluated with the same locals.
    _code = {}
    _parse = sh.parseCondition

    def compiled(cond):
        c = _code.get(cond)
        if c is None:
            c = _code[cond] = compile(_parse(cond), '<logic>', 'eval')
        return c

    def check_access(newCheck, access, logic):
        no_params = re.match('[a-zA-Z0-9-]+', newCheck).group(0)
        if logic == 'none':
            return True
        defs = sh.logic_defs
        env = {'self': sh, 'access': access}
        if defs[no_params]['type'] == 'enemy':
            param = re.search('\\[([a-z]+)\\]', newCheck)
            if param:
                return eval(compiled(defs[no_params]['condition-basic']), env) or \
                    eval(compiled(defs[no_params]['condition-' + param.group(1)]), env)
            return eval(compiled(defs[no_params]['condition-basic']), env)
        node = defs[newCheck]
        if node['type'] in ('item', 'follower') and not sh.hasAccess(access, node['region']):
            return False
        if 'condition-basic' in node and eval(compiled(node['condition-basic']), env):
            return True
        if 'condition-basic' not in node:
            return True
        for lv, allowed in (('advanced', ('advanced', 'glitched', 'hell')), ('glitched', ('glitched', 'hell')), ('hell', ('hell',))):
            if 'condition-' + lv in node and logic in allowed and eval(compiled(node['condition-' + lv]), env):
                return True
        return False

    if not os.environ.get('LAS_ORIGINAL_EVAL'):  # LAS_ORIGINAL_EVAL=1: LAS's own (slow) evaluation, for comparisons
        sh.checkAccess = check_access
    _reach = sh.canReachLocation
    _last = [0.0]

    def reach_with_progress(to_reach, placements, *a, **k):
        now = _time.time()
        if now - _last[0] > 0.5:
            _last[0] = now
            locs = [l for l, v in sh.logic_defs.items() if v.get('type') == 'item']
            done = sum(1 for l in locs if placements.get(l) is not None)
            stage(f'Shuffling items ({done}/{len(locs)} placed)', 0.04 + 0.62 * done / max(1, len(locs)))
        return _reach(to_reach, placements, *a, **k)

    sh.canReachLocation = reach_with_progress
    sh.give_placements.connect(lambda p: result.update(placements=p[0], randstate=p[1]))
    sh.error.connect(lambda e: errors.append(e))
    sh.start()
    if errors or 'placements' not in result:
        print('SHUFFLE ERROR', errors, file=sys.stderr)
        sys.exit(1)
    print('placements ready', flush=True)
    with open(os.path.join(out_dir, 'logic.json'), 'w') as f:  # the seed's final logic (tracker: tools/rando/tracker.py)
        json.dump({k: {kk: vv for kk, vv in v.items() if kk in ('type', 'subtype', 'region', 'content') or kk.startswith('condition-')}
                   for k, v in sh.logic_defs.items() if not v.get('_check_only')}, f)
    stage('Checking the seed is beatable', 0.67)
    # Independent playthrough check: from nothing, keep collecting whatever is reachable.
    pl = result['placements']
    access = {k: 1 for k, v in settings.items() if v is True}
    for it in settings.get('starting-items', []):
        access[it] = access.get(it, 0) + 1
    # LAS logic.yml (also upstream) never defines rooms D6-4D / D6-2A, so LAS treats the Face Shrine boss,
    # instrument and rapids-south-island as unreachable (conservative for placement). The check only needs
    # the vanilla instrument there, so approximate them from the neighbouring room here (check only).
    aliases = {'D6-4D': 'D6-5D', 'D6-2A': 'D6-4D'}
    for k, v in aliases.items():
        sh.logic_defs.setdefault(k, {'type': 'room', 'condition-basic': v, '_check_only': True})
    import playthrough
    sph, access, seen = playthrough.spheres(sh.logic_defs, pl, access,
                                            lambda k, acc: sh.checkAccess(k, acc, settings['logic']))
    with open(os.path.join(out_dir, 'spheres.json'), 'w') as f:  # walkthrough for the readable spoiler
        json.dump(sph, f)
    for k in [k for k, v in sh.logic_defs.items() if v.get('_check_only')]:
        del sh.logic_defs[k]
    missing = [i for i in playthrough.INSTRUMENTS if not access.get(i)]
    absent = settings.get('absent-locations', set())  # (rooms the dungeon shuffle left out: not in the game)
    locs = [k for k, v in sh.logic_defs.items() if v.get('type') == 'item' and k not in absent]
    unreached = [k for k in locs if k not in seen]
    print('playthrough:', 'all instruments reachable' if not missing else f'MISSING {missing}',
          f'- {len(locs) - len(unreached)}/{len(locs)} item locations reachable', flush=True)
    if os.environ.get('LAS_DEBUG_PLAYTHROUGH'):
        print('  have:', sorted(k for k in access if k in sh.item_defs), flush=True)
        print('  regions:', sorted(k for k in seen if sh.logic_defs[k].get('type') == 'region'), flush=True)
        import re as _re
        for k in [k for k in locs if k not in seen and k.startswith(('D6-','dampe','rapids-south'))][:6]:
            c = sh.logic_defs[k].get('condition-basic', '')
            toks = set(_re.findall(r'[a-zA-Z0-9\-\[\]]+', c or ''))
            print('  ', k, 'region', sh.logic_defs[k]['region'], 'cond', c, '| missing', [t for t in toks if not access.get(t)], flush=True)
    if unreached:
        print('  unreachable:', ', '.join(sorted(unreached)[:30]), flush=True)
    if missing:
        print('SHUFFLE ERROR unbeatable seed', missing, file=sys.stderr)
        sys.exit(3)

    if pot_locs or dig_locs:  # bomb / powder / arrow refills anywhere: only once owned, else a blue rupee (runtime)
        import pots as _pots
        print('pot refills marked:', _pots.mark_refills(result['placements']), flush=True)
    pl = result['placements']
    bottles = {loc: i for loc, i in pl['indexes'].items() if pl.get(loc) == 'bottle'}
    for loc, i in bottles.items():  # the third bottle slot (index 2) is the Escape Bottle's
        if i == 2:
            pl['indexes'][loc] = bottles[loc] = min({0, 1} - set(bottles.values()))
    if pets:
        print('pets placed:', pets_mod.mark_indexes(result['placements'], logic_defs), flush=True)
    from RandomizerCore.Randomizers import item_get as _item_get
    _item_get.GHOST_PET = 'ghost' in pets
    mods = ModsProcess(result['placements'], romfs, out_dir, item_defs, settings['seed'], result['randstate'])
    last = [0]

    def progress(n):
        if n - last[0] >= 5:
            last[0] = n
            print('progress', n, flush=True)
            stage(f'Building game files ({n} tasks)', 0.72 + 0.2 * min(1.0, n / 280))

    mods.progress_update.connect(progress)
    mods.error.connect(lambda e: errors.append(e))
    mods.start()
    if errors:
        print('MODS ERROR', errors, file=sys.stderr)
        sys.exit(2)
    if shopsanity.enabled(settings) or trendsanity.enabled(settings):  # (before pots: they take the flags after ours)
        from RandomizerCore.Tools import event_tools, oead_tools
        from RandomizerCore.Randomizers import item_get, data as las_data
        import msbt
        stage('Stocking the shop', 0.925)
        if shopsanity.enabled(settings):
            lines = shopsanity.apply(mods, out_dir, romfs, settings, event_tools, oead_tools, item_get, msbt)
            print('shopsanity:\n' + '\n'.join(lines), flush=True)
        if trendsanity.enabled(settings):
            lines = trendsanity.apply(mods, out_dir, romfs, settings, event_tools, oead_tools, item_get, las_data,
                                      shopsanity.alloc_flags)
            print('trendsanity:\n' + '\n'.join(lines), flush=True)
    if pets:
        from RandomizerCore.Tools import event_tools, oead_tools
        print('pets:', pets_mod.apply(out_dir, romfs, pets, oead_tools, event_tools), 'item entries', flush=True)
    if soul_kinds:
        import msbt
        from RandomizerCore.Tools import event_tools, oead_tools
        print('souls:', souls_mod.apply(out_dir, romfs, leb, oead_tools, event_tools, msbt, soul_kinds, soul_cat),
              flush=True)
    if pot_locs:
        from RandomizerCore.Tools import event_tools, oead_tools
        from RandomizerCore.Randomizers import item_get, data as las_data
        stage('Hiding items in pots', 0.93)
        lines = pots_mod.apply(pot_locs, mods, out_dir, romfs, leb, event_tools, oead_tools, item_get, las_data)
        with open(os.path.join(out_dir, f'pots_{settings["seed"]}.txt'), 'w') as f:
            f.write('Pots:\n' + '\n'.join(lines) + '\n')
        print('pots placed:', len(lines), flush=True)
        print('pots.tsv:', pots_mod.write_tsv(out_dir, leb), flush=True)
    if bush_locs:
        from RandomizerCore.Tools import event_tools, oead_tools
        from RandomizerCore.Randomizers import item_get, data as las_data
        stage('Hiding items in bushes', 0.94)
        lines = bushes_mod.apply(bush_locs, mods, out_dir, romfs, leb, event_tools, oead_tools, item_get, las_data)
        with open(os.path.join(out_dir, f'bushes_{settings["seed"]}.txt'), 'w') as f:
            f.write('Bushes:\n' + '\n'.join(lines) + '\n')
        print('bushes placed:', len(lines), flush=True)
        print('bushes.tsv:', bushes_mod.write_tsv(out_dir, leb), flush=True)
    if ent:
        lvl_out = os.path.join(out_dir, 'romfs', 'region_common', 'level')

        def room_path(name, base):
            level = re.match(r'(.+)_\d\d[A-Z]', name).group(1)
            return os.path.join(base, level, name + '.leb')

        def read_room(name):
            p = room_path(name, lvl_out)
            if not os.path.exists(p):
                p = room_path(name, os.path.join(romfs, 'region_common', 'level'))
            with open(p, 'rb') as f:
                return leb.Room(f.read())

        def write_room(name, room):
            p = room_path(name, lvl_out)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, 'wb') as f:
                f.write(room.repack())

        stage('Shuffling entrances', 0.97)
        n = el.apply_rooms(*ent, read_room, write_room)
        with open(os.path.join(out_dir, f'entrances_{settings["seed"]}.txt'), 'w') as f:
            f.write(el.spoiler(*ent) + '\n')
        print('entrance rooms edited:', n, flush=True)

    if room_plan:
        stage('Building the shuffled dungeons', 0.975)
        print('dungeon rooms:', shuffle_dungeon_rooms(romfs, out_dir, room_mode, *room_plan), flush=True)
        # gold pots / bushes are found by position: moved rooms moved their item actors too
        if pot_locs:
            print('pots.tsv:', pots_mod.write_tsv(out_dir, leb), flush=True)
        if bush_locs:
            print('bushes.tsv:', bushes_mod.write_tsv(out_dir, leb), flush=True)

    import escape_bottle  # Tarin gives it with the starting items, in the game's own get-item message box
    print('escape bottle:', escape_bottle.apply(out_dir, romfs), flush=True)
    import keysanity_text  # (after every flow edit) a dungeon item outside its dungeon names it in the get message
    keysanity_text.apply(out_dir, romfs, result['placements'])

    if soul_kinds:  # where the souls' fairy models stand (after the rooms moved): drawn red there (souls.c)
        print('soul spots:', souls_mod.write_spots(out_dir, leb, result['placements']), flush=True)
    import seed_tables
    from RandomizerCore.Tools import oead_tools as _ot
    if dig_locs:
        from RandomizerCore.Tools import event_tools, oead_tools
        from RandomizerCore.Randomizers import item_get, data as las_data
        stage('Burying items', 0.95)
        lines = digs_mod.apply(dig_locs, dig_tiles, mods, out_dir, romfs, event_tools, oead_tools, item_get, las_data)
        with open(os.path.join(out_dir, f'digs_{settings["seed"]}.txt'), 'w') as f:
            f.write('Digs:\n' + '\n'.join(lines) + '\n')
        print('digs placed:', len(lines), flush=True)
    import underwater  # sunken heart pieces / seashells: their randomized item, its model, still dived for
    from RandomizerCore.Tools import event_tools as _uw_ev, leb as _uw_leb
    from RandomizerCore.Randomizers import item_get as _uw_get, data as _uw_data
    print('underwater:', ', '.join(underwater.apply(mods, out_dir, romfs, _uw_leb, _uw_ev, _ot, _uw_get, _uw_data)),
          flush=True)
    if settings.get('enemy-minibosses'):  # the Field's walls for roaming mini-bosses (tools/rando/roam.py)
        import roam
        print('roam:', roam.write(out_dir, _uw_leb, romfs), flush=True)
        import miniboss_guard  # (copies of a mini-boss whose flow gives a check don't give it again)
        print('miniboss guard:', miniboss_guard.apply(out_dir, romfs, _uw_leb, _uw_ev, _ot, settings), flush=True)
    import bomb_bag_text  # (after every flow edit, digs too) found bombs: "You found your Bomb Bag!"
    print('bomb bag text:', bomb_bag_text.apply(out_dir, romfs, settings), flush=True)
    print('flags.tsv:', seed_tables.write_flags(out_dir, _ot, romfs), flush=True)
    with open(os.path.join(out_dir, 'settings.json'), 'w') as f:
        s = dict(settings)
        s['excluded-locations'] = sorted(s['excluded-locations'])
        s['absent-locations'] = sorted(s.get('absent-locations', ()))
        s['escape-bottle'] = True  # (runtime/hle/escape.c: older seeds keep three plain bottles)
        s['ghost-item'] = 'ghost' in pets  # (runtime/hle/pets.c: older seeds' Ghost follows Link)
        s['underwater-items'] = True  # (runtime: the sunken heart-piece actors don't hide for an owned heart piece)
        json.dump(s, f, indent=1)
    stage('Building the check tracker', 0.99)
    import tracker
    if settings.get('randomize-environments'):
        import environments
        print('environments:', environments.apply(out_dir, romfs, settings['seed']), 'levels', flush=True)
    if settings.get('tunic-color', 'default') != 'default':
        import cosmetics
        print('tunic:', cosmetics.apply(out_dir, romfs, settings['seed'], settings['tunic-color']), flush=True)
    print('tracker:', tracker.build(out_dir, romfs), flush=True)
    import spoiler_view  # readable spoiler + walkthrough for the in-game Spoiler page
    # the spoiler is named after the seed string (the seed ID): a fixed name survives renaming the seed's folder
    sp = os.path.join(out_dir, f"spoiler_{settings['seed']}.txt")
    if os.path.exists(sp):
        os.replace(sp, os.path.join(out_dir, 'spoiler.txt'))
    print('spoiler view:', spoiler_view.build(out_dir), 'lines', flush=True)
    stage('Done', 1.0)
    print('done', flush=True)


if __name__ == '__main__':
    main()
