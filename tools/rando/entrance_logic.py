#!/usr/bin/env python3
"""Logic for the LAS Recomp entrance randomizer (single-entrance interiors), on top of LAS-Randomizer's logic.yml.

LAS reachability is a fixpoint over named nodes, so the shuffle is expressed as new nodes:
  door-<id>    you can stand outside door <id> and get through it: outer_region & outer_extra & door_req
  inside-<id>  you are inside the interior that door <id> led to in vanilla: OR of door-<d> over every door d
               that now leads into it
Every LAS location inside that interior moves to region inside-<id> with the condition it has once you're inside
(tools/rando/entrance_data.yml `inside`). Decoupled exits add the reverse edges: leaving interior I puts you
outside door c, so region outer_region(c) also becomes reachable from inside-I.

Pool (v1): `single` doors that are land doors, recommended for shuffling, not reached through a pocket, and not
Marin & Tarin's house (the game starts inside it). Dungeons use LAS's own `shuffle-dungeons`.

Connector caves (part of tier 2, own pool: cave doors lead into cave doors): see connector_pool() / apply_connector_logic().
"""
import os
import random

import yaml

HERE = os.path.dirname(os.path.abspath(__file__))
KEEP = {'marin-tarin-house'}  # game start
# conditions elsewhere that mean "you can get inside this interior"
INSIDE_TOKENS = {('christine-grateful', 'mr-write-house'): 'mr-write-house'}


def load(path=os.path.join(HERE, 'entrance_data.yml')):
    with open(path) as f:
        return yaml.safe_load(f)


def pool(ed, closed=()):
    """closed: outer regions whose doors stay vanilla. Decoupled exits with the desert shut (open-desert off: Marin in
    the pool, the Walrus asleep) must not drop Link behind the Walrus - he couldn't walk back out, and the Marin gate
    would be skipped - so the desert's two doors (Lanmola cave stairs, Animal Village cave wall) aren't shuffled then."""
    out = []
    for d in ed['doors']:
        if d['type'] != 'single' or d['id'] in KEEP or d.get('outer_region') in closed:
            continue
        if d.get('water') or d.get('pocket') or d.get('outer_extra') or d.get('shuffle_recommended') is False:
            continue
        out.append(d)
    return out


def shuffle(doors, decoupled, rng):
    """entry: door id -> interior (vanilla door id) it now leads into; exit: interior -> door id it exits to."""
    ids = [d['id'] for d in doors]
    targets = ids[:]
    rng.shuffle(targets)
    entry = dict(zip(ids, targets))
    if decoupled:
        outs = ids[:]
        rng.shuffle(outs)
        exit_ = dict(zip(ids, outs))
    else:
        exit_ = {t: d for d, t in entry.items()}
    return entry, exit_


def _and(*parts):
    parts = [p for p in parts if p]
    return ' & '.join(f'({p})' for p in parts) if parts else None


def _or_into(node, extra):
    """region node condition |= extra (a region without a condition is always reachable already)."""
    c = node.get('condition-basic')
    if c is None:
        return
    node['condition-basic'] = f'({c}) | ({extra})'


def apply_logic(logic, doors, entry, exit_, extra_inside=None):
    """extra_inside: {vanilla door id: [(location, cond)]} for added locations (e.g. item pots) in interiors."""
    by_id = {d['id']: d for d in doors}
    for d in doors:
        logic[f"door-{d['id']}"] = {'type': 'region',
                                     'condition-basic': _and(d['outer_region'], d.get('outer_extra'), d.get('door_req'))}
    into = {}
    for d, t in entry.items():
        into.setdefault(t, []).append(d)
    for t, ds in into.items():
        logic[f'inside-{t}'] = {'type': 'region', 'condition-basic': ' | '.join(f'door-{d}' for d in ds)}
    moved = 0
    for t in into:
        for it in list(by_id[t].get('inside') or []) + [{'location': l, 'cond': c} for l, c in (extra_inside or {}).get(t, [])]:
            loc = logic.get(it['location'])
            if not loc:
                continue
            loc['region'] = f'inside-{t}'
            for k in ('condition-advanced', 'condition-glitched', 'condition-hell'):
                loc.pop(k, None)  # those were written against vanilla outer access; basic-only is never looser
            if it.get('cond'):
                loc['condition-basic'] = str(it['cond'])
            else:
                loc.pop('condition-basic', None)
            moved += 1
    for (loc, token), door in INSIDE_TOKENS.items():
        if loc in logic and door in by_id and f'inside-{door}' in logic:
            c = logic[loc].get('condition-basic', '')
            logic[loc]['condition-basic'] = ' '.join(f'inside-{door}' if w == token else w for w in c.split())
    # leaving interior I through door c puts Link outside c (coupled: the door he came in by)
    for t, c in exit_.items():
        region = by_id[c]['outer_region']
        if region in logic and f'inside-{t}' in logic:
            _or_into(logic[region], f'inside-{t}')
    return moved


# ---- Connector caves (part of tier 2). A connector is one interior with 2-3 doors (entrance_data.yml `connectors`: nodes
# inside, directed edges between them with conditions, items at nodes). Every cave door is shuffled among the cave
# doors: entering door d leads into the cave spot of door entry[d] (node of that door), and the cave's zone near
# door t leads outside door exit_[t]. Logic nodes: conn-<cave>-<node> = OR of the doors leading into a door of that
# node + edges from the other nodes; a door in a `pocket` (an outside area vanilla only reaches through a connector)
# stands in pocket-<name>, reachable only by leaving a cave there (+ any outdoor way in, POCKETS).
# Only caves whose vanilla route LAS logic takes in known places are pooled: REGION_PATCHES rewrites those places
# (asserting the original text) so no region stays reachable "through" a cave that now leads elsewhere. Left out:
# the Tal Tal rainbow / five-chest / path-to-D7 caves (eagle-tower, owl / chest pockets), Kanalet Castle (roof),
# the Martha's Bay cliff cave (LAS `plains` takes the west bay shore through it), the bay-batter tunnel, the D4
# connector and the raft return (low confidence), the Catfish's Maw dive (water).
CONNECTORS = ('quadruplet-house', 'goponga-cave', 'graveyard-cave', 'kanalet-tunnel', 'river-crossing-cave',
              'fire-cave', 'taltal-crossing-cave', 'mountain-access', 'papahl-cave', 'd6-connector', 'richard-villa',
              'woods-crossing-cave')
# pocket name -> (logic region, outdoor way in or None, (region it drops to, condition) or None)
POCKETS = {
    'taltal-east-left-chest-ledge': ('pocket-taltal-east-left-chest-ledge', None, None),
    'papahl-ledge': ('pocket-papahl-ledge', None, ('taltal-east-0', None)),  # the ledge drops back down
    'outside-d6': ('pocket-outside-d6', None, ('river', 'flippers')),        # jump into the water
    'toadstool-clearing': ('pocket-toadstool-clearing', 'swamp & (feather | rooster)', None),  # (LADXR)
    'pothole-field': ('pothole', None, None),  # LAS region pothole = the field behind Richard's villa
}
# cave -> [(node, condition-basic LAS has, condition-basic without the vanilla route through that cave or None
# for "only through shuffled cave doors")]; the node's advanced / glitched / hell conditions are dropped
REGION_PATCHES = {
    'mountain-access': [
        ('taltal-east-0', '(wasteland & break-crystal & (break-obstacle | rooster) & bracelet) | '
                          '(taltal-east-1 & flippers & heights & angler-key)', '(taltal-east-1 & flippers & heights & angler-key)'),
        ('wasteland', '(woods & (feather | bracelet | rooster | hookshot)) | (taltal-east-0 & (break-obstacle | rooster) & '
                      'bracelet) | (heights & bracelet) | (swamp & bracelet) | (plains & bracelet)',
         '(woods & (feather | bracelet | rooster | hookshot)) | (heights & bracelet) | (swamp & bracelet) | (plains & bracelet)')],
    'taltal-crossing-cave': [('taltal-west-0', 'taltal-east-1 & (hookshot | rooster) & break-bush', None)],
    'fire-cave': [('taltal-west-2', 'taltal-west-1 & shield:2 & bombs', None)],
    'kanalet-tunnel': [('kanalet-outside', '(bridge & break-bush & feather) | (plains & open-kanalet & break-bush)',
                        '(plains & open-kanalet & break-bush)')],
    'river-crossing-cave': [('animal-village', '(plains & (hookshot | break-obstacle)) | (river & cross-water)',
                             '(plains & hookshot) | (river & cross-water)')],
    'd6-connector': [('D6-8D', '(river & face-key & (hookshot | rooster)) | (D6-3A & ((ocarina & song-mambo) | bombs | '
                               'shoot-arrows | boomerang | rod))',
                      '(pocket-outside-d6 & face-key) | (D6-3A & ((ocarina & song-mambo) | bombs | shoot-arrows | '
                      'boomerang | rod))')],
    'richard-villa': [('pothole', 'plains & golden-leaf:5', None)],
}


def connector_pool(ed, names=CONNECTORS):
    """Cave doors of the pooled connectors (entrance_data door dicts + 'connector' / 'node' / 'pocket_region')."""
    by_id = {d['id']: d for d in ed['doors']}
    out = []
    for name in names:
        c = ed['connectors'][name]
        assert not c.get('special'), name
        for did in c['doors']:
            d = dict(by_id[did], connector=name, node=c['door_nodes'][did])
            p = (d.get('pocket') or {}).get('name')
            d['pocket_region'] = POCKETS[p][0] if p else None
            out.append(d)
    return out


def node_region(d):
    return f"conn-{d['connector']}-{d['node']}"


def pocket_screens(cdoors):
    """Field room -> pocket region for the screens of pocket doors (the zone's file and its real GB screen)."""
    out = {}
    for d in cdoors:
        if d['pocket_region']:
            out[d['field_room']] = d['pocket_region']
            g = int(str(d.get('gb_screen') or '-1'), 0)
            if g >= 0:
                out[f'Field_{g // 16 + 1:02d}{chr(65 + g % 16)}'] = d['pocket_region']
    return out


def connector_levels(cdoors):
    """level -> logic region for cave levels that are one node (pots inside them follow their cave)."""
    nodes = {}
    for d in cdoors:
        nodes.setdefault(d['dest_level'], set()).add(node_region(d))
    return {lv: next(iter(n)) for lv, n in nodes.items() if len(n) == 1}


def apply_connector_logic(logic, ed, cdoors, entry, exit_, extra_inside=None):
    """Connector caves into LAS's logic (before apply_logic: it ORs house exits into regions patched here).
    extra_inside: {logic region: [(location, cond)]} for added locations (item pots) inside caves."""
    by_id = {d['id']: d for d in cdoors}
    names = sorted({d['connector'] for d in cdoors})
    terms = {}  # region -> extra OR terms
    built = set()  # regions whose condition is only those terms (new nodes, regions now only through caves)

    def add(region, term):
        terms.setdefault(region, []).append(term)
        if region.startswith(('conn-', 'pocket-')):
            built.add(region)

    for name in names:
        for node, old, new in REGION_PATCHES.get(name, ()):
            assert logic[node].get('condition-basic') == old, (node, logic[node].get('condition-basic'))
            for k in ('condition-advanced', 'condition-glitched', 'condition-hell', 'condition-trick'):
                logic[node].pop(k, None)
            if new is None:
                logic[node].pop('condition-basic')
                built.add(node)
            else:
                logic[node]['condition-basic'] = new
    for d in cdoors:
        outer = d['pocket_region'] or _and(d['outer_region'], d.get('outer_extra'))
        logic[f"door-{d['id']}"] = {'type': 'region', 'condition-basic': _and(outer, d.get('door_req'))}
    for d, t in entry.items():  # door d leads into the cave spot of door t
        add(node_region(by_id[t]), f'door-{d}')
    moved = 0
    for name in names:
        c = ed['connectors'][name]
        for e in c['edges']:
            pairs = [(e['from'], e['to'])] + ([(e['to'], e['from'])] if e.get('bidirectional') else [])
            for a, b in pairs:
                src = f'conn-{name}-{a}'
                add(f'conn-{name}-{b}', _and(src, e.get('cond')) if e.get('cond') else src)
        for it in c['items']:
            moved += _move(logic, it['location'], f"conn-{name}-{it['node']}", it.get('cond'))
    for region, its in (extra_inside or {}).items():
        for loc, cond in its:
            moved += _move(logic, loc, region, cond)
    # pockets: their outdoor locations, outdoor ways in, drops
    for d in cdoors:
        p = d.get('pocket')
        if not p:
            continue
        region, way_in, drop = POCKETS[p['name']]
        if region not in logic:
            logic[region] = {'type': 'region'}
            if way_in:
                add(region, way_in)
            if drop:
                add(drop[0], _and(region, drop[1]))
        for it in p.get('items') or []:
            if logic.get(it['location'], {}).get('region') != region:
                moved += _move(logic, it['location'], region, it.get('cond'))
    # leaving the cave through the zone of door t puts Link outside door exit_[t]
    for t, c in exit_.items():
        dc = by_id[c]
        if dc['pocket_region']:
            add(dc['pocket_region'], node_region(by_id[t]))
        else:  # (a rock / gap between the door and its region blocks the way back too: outer_extra)
            add(dc['outer_region'], _and(node_region(by_id[t]), dc.get('outer_extra')))
    for region, ts in terms.items():
        node = logic.setdefault(region, {'type': 'region'})
        c = node.get('condition-basic')
        if c is None and region not in built:
            continue  # a LAS region without a condition: always reachable
        node['condition-basic'] = ' | '.join(f'({x})' for x in ([c] if c else []) + ts)
    return moved


def reach_all(logic, item_defs, settings):
    """Logic nodes reachable with every item of the pool (basic conditions, LAS's semantics; followers vanilla).
    A cave shuffle where this loses a location is re-rolled (las_headless): cave doors can close loops that no
    item opens (e.g. a cave whose doors all stand in pockets reached only through each other)."""
    import re
    import playthrough
    access = {k: 1 for k, v in settings.items() if v is True}
    for k, d in item_defs.items():
        if d.get('quantity'):
            access[k] = int(d['quantity'])
    access.update({'bow-wow': 1, 'rooster': 1})
    code = {}

    def ev(cond, acc):
        c = code.get(cond)
        if c is None:
            py = re.sub(r'([a-zA-Z0-9\-\[\]]+)(:(\d+))?', lambda m: f'h("{m.group(1)}", {m.group(3) or 1})', cond)
            c = code[cond] = compile(py.replace('|', ' or ').replace('&', ' and ').replace('!', ' not '), '<c>', 'eval')
        return eval(c, {'h': lambda k, n: acc.get(k, 0) >= n})

    def check(key, acc):
        m = re.match(r'(.+)\[(\w+)\]$', key)
        if m:
            node = logic[m.group(1)]
            return ev(node.get('condition-basic') or 'true', acc) or ev(node[f'condition-{m.group(2)}'], acc)
        node = logic[key]
        if node.get('type') in ('item', 'follower') and not acc.get(node.get('region')):
            return False
        return 'condition-basic' not in node or node['condition-basic'] is None or ev(node['condition-basic'], acc)

    access['true'] = 1
    _, _, seen = playthrough.spheres(logic, {}, access, check)
    return seen


def _move(logic, location, region, cond):
    loc = logic.get(location)
    if not loc:
        return 0
    loc['region'] = region
    for k in ('condition-advanced', 'condition-glitched', 'condition-hell'):
        loc.pop(k, None)  # basic-only is never looser
    if cond:
        loc['condition-basic'] = str(cond)
    else:
        loc.pop('condition-basic', None)
    return 1


def spoiler(doors, entry, exit_):
    by_id = {d['id']: d for d in doors}
    lines = ['Entrances (door -> interior it leads into):']
    for d in sorted(entry):
        lines.append(f'  {d:32} -> {entry[d]:32} ({by_id[entry[d]]["dest_level"]})')
    if any(exit_[t] != d for d, t in entry.items()):
        lines.append('Exits (interior -> outside of door):')
        for t in sorted(exit_):
            lines.append(f'  {t:32} -> {exit_[t]}')
    return '\n'.join(lines)


if __name__ == '__main__':
    ed = load()
    ds = pool(ed)
    print(len(ds), 'doors in the pool')
    e, x = shuffle(ds, False, random.Random(1))
    print(spoiler(ds, e, x)[:800])


def apply_rooms(doors, entry, exit_, read_room, write_room, zones_path=os.path.join(HERE, 'loading_zones.json')):
    """Rewrite loading zones: door d gets the entry params of interior entry[d]; every zone in interior t that
    leads back outside gets the return params of interior exit_[t] (which put Link outside that door)."""
    import json
    import re
    zones = json.load(open(zones_path))

    def base(r):
        m = re.match(r'(.+_\d\d[A-Z])', r)
        return m.group(1) if m else r

    by_id = {d['id']: d for d in doors}
    field = {}  # door id -> params of its field zone
    returns = {}  # door id -> [(room, index)] interior zones leading back to its screen
    ret_params = {}
    for d in doors:
        z = next(z for z in zones if z['room'] == d['field_room'] and z['index'] == d['zone_index'])
        field[d['id']] = list(z['params'])
        if d['type'] == 'connector':  # several doors of one cave return to one screen: the zone named per door
            rz = [d['return_zone']] + list(d.get('other_return_zones') or [])
            returns[d['id']] = [(r['room'], r['index']) for r in rz]
            ret_params[d['id']] = list(next(z for z in zones if (z['room'], z['index']) == returns[d['id']][0])['params'])
            continue
        rs =[z for z in zones if z['level'] == d['dest_level'] and z['params'][0] == 'Field'
              and base(z['params'][1]) == base(d['return_zone']['target'] if d.get('return_zone') else d['field_room'])]
        returns[d['id']] = [(z['room'], z['index']) for z in rs]
        if rs:
            ret_params[d['id']] = list(rs[0]['params'])
    edits = {}
    for d, t in entry.items():
        edits.setdefault(by_id[d]['field_room'], {})[by_id[d]['zone_index']] = field[t]
    for t, c in exit_.items():
        if c not in ret_params:
            continue
        for room, idx in returns[t]:
            edits.setdefault(room, {})[idx] = ret_params[c]
    for room_name, ze in edits.items():
        room = read_room(room_name)
        zs = [a for a in room.actors if a.type == 0x190]
        for idx, params in ze.items():
            if idx < len(zs):
                for i, p in enumerate(params):
                    zs[idx].parameters[i] = p.encode('utf-8') if isinstance(p, str) else p
        write_room(room_name, room)
    return len(edits)


def verify(seed_dir, romfs):
    """Re-read the seed's rooms and check every shuffled door / interior exit against the spoiler mapping."""
    import json
    import re
    import sys
    import types
    las = os.path.normpath(os.path.join(HERE, '..', '..', 'third_party', 'LAS-Randomizer'))
    sys.modules.setdefault('PySide6', types.ModuleType('PySide6'))
    sys.modules.setdefault('PySide6.QtCore', types.ModuleType('PySide6.QtCore'))
    sys.path.insert(0, las)
    from RandomizerCore.Tools import leb
    ed = load()
    doors = {d['id']: d for d in pool(ed) + connector_pool(ed)}
    sp = open(next(os.path.join(seed_dir, f) for f in os.listdir(seed_dir) if f.startswith('entrances_'))).read()
    entry = dict(re.findall(r'^  (\S+)\s+-> (\S+)\s+\(', sp, re.M))
    exits = dict(re.findall(r'^  (\S+)\s+-> (\S+)$', sp.split('Exits')[1], re.M)) if 'Exits' in sp else {t: d for d, t in entry.items()}
    zones = json.load(open(os.path.join(HERE, 'loading_zones.json')))
    vz = {(z['room'], z['index']): z['params'] for z in zones}

    def room(name):
        lv = re.match(r'(.+)_\d\d[A-Z]', name).group(1)
        for base in (os.path.join(seed_dir, 'romfs', 'region_common', 'level'), os.path.join(romfs, 'region_common', 'level')):
            p = os.path.join(base, lv, name + '.leb')
            if os.path.exists(p):
                return [[x.decode() if isinstance(x, bytes) else x for x in a.parameters] for a in leb.Room(open(p, 'rb').read()).actors if a.type == 0x190]
    bad = 0
    for d, t in entry.items():
        got = room(doors[d]['field_room'])[doors[d]['zone_index']][:2]
        want = vz[(doors[t]['field_room'], doors[t]['zone_index'])][:2]
        if got != want:
            bad += 1
            print('door', d, 'has', got, 'want', want)
    for t, c in exits.items():
        rz = doors[c].get('return_zone')
        tz = doors[t].get('return_zone')
        if not rz or not tz:
            continue
        got = room(tz['room'])[tz['index']][:2]
        want = vz[(rz['room'], rz['index'])][:2]
        if got != want:
            bad += 1
            print('exit of', t, 'has', got, 'want', want)
    print(f'{len(entry)} doors, {len(exits)} exits checked, {bad} mismatches')
    return bad
