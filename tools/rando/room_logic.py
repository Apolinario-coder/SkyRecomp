#!/usr/bin/env python3
"""Item logic for shuffled dungeons (Dungeon room shuffle: Coherent / Chaos, tools/rando/dungeon_gen.py).

LAS's dungeon logic describes the vanilla layouts: a few "room" regions per dungeon named after cells (D1-8D,
D1-6F...) and each check gated by one of them plus key counts and what the check itself needs. With rooms moved
that no longer holds, so for every shuffled dungeon this module builds the logic from the generated layout:

  graph      cells of the layout; edges = doorways that connect (neighbours whose doorways line up, door jumps -
             Coherent and Chaos, directed for Chaos), plus the stairs links (side-view passages; stairs rooms are
             anchors and never move)
  doors      only a room's OWN door actors count (a "(mirror)" door stood in the old neighbour, which isn't there):
             lock -> the dungeon's small keys, boss door -> its Nightmare Key, cracked wall -> bombs, shutter ->
             something to fight with / hit switches (approximate), pot in the doorway -> break-pot, water in the
             doorway -> flippers, crystal -> break-crystal; one-way doors pass one way (actor halves 0x1A3 / 0x1A4); statues
             and chests in a doorway are left out (impassable for the logic: safe, never relied on)
  keys       a lock needs ALL of the dungeon's small keys (always solvable: the keys sit before every lock); the
             number of small keys per dungeon = the number of locks in its new layout
  cells      each cell's requirement = OR over paths from the entrance of AND of the door requirements, computed
             as a fixed point in minimal sum-of-products form (LAS evaluates conditions recursively, so cyclic
             region references aren't possible - the finished, loop-free condition goes into the logic)
  regions    LAS's cell regions (Dn-XY) are redefined as "reach cell XY of the new layout" (macros elsewhere use them)
  checks     reach any cell holding the check's room (Coherent duplicates count, also in other dungeons) AND what
             the check needs inside its room: its vanilla condition with the region / key tokens taken out (where
             such a token sat inside an OR, the OR becomes an AND - stricter, never weaker). A check whose room isn't
             used anywhere is excluded (junk). Dungeon items go to the dungeon a check physically is in now.

Not modelled yet (stricter or approximate): what a room needs to cross it between two doorways (pits, water,
blocks inside the room), the exact enemies / switches behind each shutter, key counts along paths.
"""
import json
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))

LEVELS = {
    'Lv10ClothesDungeon': (0, 'color-dungeon'), 'Lv01TailCave': (1, 'tail-cave'), 'Lv02BottleGrotto': (2, 'bottle-grotto'),
    'Lv03KeyCavern': (3, 'key-cavern'), 'Lv04AnglersTunnel': (4, 'angler-tunnel'), 'Lv05CatfishsMaw': (5, 'catfish-maw'),
    'Lv06FaceShrine': (6, 'face-shrine'), 'Lv07EagleTower': (7, 'eagle-tower'), 'Lv08TurtleRock': (8, 'turtle-rock'),
}
SHUTTER = 'sword | shoot-arrows | bombs | rod | boomerang | hookshot'  # fight the room's enemies / hit its switches
DOOR_REQ = {'cracked': 'bombs', 'potdoor': 'break-pot', 'blocked:water': 'flippers', 'blocked:crystal': 'break-crystal',
            'blocked:block': None, 'shutter': 'rs-shutter'}
IMPASSABLE = {'blocked:statue', 'blocked:chest'}
STEP = {'N': (0, -1), 'S': (0, 1), 'W': (-1, 0), 'E': (1, 0)}
OPP = {'N': 'S', 'S': 'N', 'W': 'E', 'E': 'W'}
MAX_TERMS = 48
EXIT_REQ = {}  # catalog room -> frozenset of atoms leaving it needs (mini-boss souls: tools/rando/souls.py exit_req)


# location subtypes LAS shuffles (shuffler.py: every other item location keeps its vanilla content)
RANDOMIZED_SUBTYPES = ('chest', 'boss', 'enemy', 'drop', 'npc', 'standing', 'overworld-statue', 'dungeon-statue')


def cell_pos(cell):  # '06C' -> (col 2, row 6)
    return ord(cell[2]) - ord('A'), int(cell[:2])


def cell_name(col, row):
    return f'{row:02d}{chr(ord("A") + col)}'


# ---------------------------------------------------------------- conditions: parse, strip, print
TOK = re.compile(r'\s*(\(|\)|&|\||!|[^\s()&|!]+)')


def top_level_or(s):
    """'a | (b & c) | d' -> ['a', '(b & c)', 'd'] (alternatives outside parentheses)."""
    out, depth, cur = [], 0, ''
    for ch in s:
        depth += ch == '('
        depth -= ch == ')'
        if ch == '|' and depth == 0:
            out.append(cur)
            cur = ''
        else:
            cur += ch
    return out + [cur] if cur.strip() else out


def parse(s):
    toks = [t for t in TOK.findall(s or '') if t.strip()]
    pos = [0]

    def peek():
        return toks[pos[0]] if pos[0] < len(toks) else None

    def take():
        pos[0] += 1
        return toks[pos[0] - 1]

    def expr():
        n = [term()]
        while peek() == '|':
            take()
            n.append(term())
        return n[0] if len(n) == 1 else ('|', n)

    def term():
        n = [factor()]
        while peek() == '&':
            take()
            n.append(factor())
        return n[0] if len(n) == 1 else ('&', n)

    def factor():
        t = take()
        if t == '!':
            return ('!', [factor()])
        if t == '(':
            e = expr()
            take()  # ')'
            return e
        return ('atom', t)
    if not toks:
        return None
    return expr()


def strip(node, drop):
    """Removes atoms for which drop(atom) is true (treated as satisfied). Returns (node or None, stripped?). An OR
    whose branches lost something becomes an AND of its branches (strict)."""
    if node is None:
        return None, False
    kind = node[0]
    if kind == 'atom':
        return (None, True) if drop(node[1]) else (node, False)
    if kind == '!':
        sub, s = strip(node[1][0], drop)
        return (('!', [sub]) if sub else None), s
    parts, any_s = [], False
    for c in node[1]:
        sub, s = strip(c, drop)
        any_s |= s
        if sub is not None:
            parts.append(sub)
    if kind == '|' and any_s:
        kind = '&'
    if not parts:
        return None, any_s
    return (parts[0] if len(parts) == 1 else (kind, parts)), any_s


def falsify(node, bad):
    """The condition with the atoms for which bad(atom) is true taken as never satisfied (None = never)."""
    if node is None:
        return None
    kind = node[0]
    if kind == 'atom':
        return None if bad(node[1]) else node
    if kind == '!':
        return node
    parts = [falsify(c, bad) for c in node[1]]
    if kind == '&':
        return None if any(p is None for p in parts) else ('&', parts)
    parts = [p for p in parts if p is not None]
    return None if not parts else parts[0] if len(parts) == 1 else ('|', parts)


def show(node):
    if node is None:
        return ''
    kind = node[0]
    if kind == 'atom':
        return node[1]
    if kind == '!':
        return '!' + paren(node[1][0])
    return f' {kind} '.join(paren(c) if c[0] in ('|', '&') and c[0] != kind else show(c) for c in node[1])


def paren(n):
    return show(n) if n[0] in ('atom', '!') else '(' + show(n) + ')'


def dnf_str(terms):
    if not terms:
        return None
    out = []
    for t in sorted(terms, key=lambda t: (len(t), sorted(t))):
        out.append(' & '.join(f'({a})' if '|' in a else a for a in sorted(t)) if t else '')
    out = [o for o in out if o]
    return ' | '.join(f'({o})' for o in out) if len(out) > 1 else (out[0] if out else None)


# ---------------------------------------------------------------- the dungeon graph
def own_doors(cat, room, side, slot):
    """Types of the room's own door actors at that doorway (no '(mirror)')."""
    out = []
    if isinstance(slot, str):  # wide opening ('w0-9'): no door actors
        return out
    for d in cat.get(room, {}).get('doors', []):
        if len(d) > 3 and d[0] == side and abs(d[3] - slot) <= 1 and not d[2].endswith('(mirror)'):
            out.append(d[2])
    return out


ONEWAY_OUT, ONEWAY_IN = 0x1A3, 0x1A4  # ObjOnewayDoor halves: the room Link leaves through it / the room he enters
                                     # (Bottle Grotto 05B 0x1A3 -> Hinox room 04B 0x1A4, as LAS's logic goes)


_lock_cache = {}


def lock_flag(romfs, room, side):
    """The 'opened' flag of the room's own ObjLockDoor (0xED) on that side, or None. A vanilla locked door is two
    actors - one in each room - sharing this flag, so one key opens both halves: locks are counted by flag."""
    if room not in _lock_cache:
        import sys
        sys.path.insert(0, os.path.join(HERE, '..', '..', 'third_party', 'LAS-Randomizer'))
        from RandomizerCore.Tools import leb
        import rooms as _rooms
        out = []
        try:
            r = leb.Room(open(_rooms._leb_path(romfs, room), 'rb').read())
            col, row = cell_pos(room.rsplit('_', 1)[1])
            x0, z0 = 15.0 * col, 12.0 * (row - 1)
            for a in r.actors:
                if a.type != 0xED:
                    continue
                lx, lz = a.posX - x0, a.posZ - z0
                d = {'W': lx, 'E': 15.0 - lx, 'N': lz, 'S': 12.0 - lz}
                sd = min(d, key=d.get)
                flag = next((i for t, i in a.switches if t == 1), None)
                out.append((sd, flag))
        except (OSError, ValueError):
            pass
        _lock_cache[room] = out
    for sd, flag in _lock_cache[room]:
        if sd == side:
            return flag
    return None


_block_cache = {}


def lock_blocks(romfs, room):
    """The 'opened' flags of the room's keyhole blocks (ObjLockBlock 0x102): each takes a small key. What a block
    guards inside its room isn't known, so the logic asks for it for the room's checks and for leaving the room
    through its other doorways (entering stays free)."""
    if room not in _block_cache:
        import sys
        sys.path.insert(0, os.path.join(HERE, '..', '..', 'third_party', 'LAS-Randomizer'))
        from RandomizerCore.Tools import leb
        import rooms as _rooms
        out = []
        try:
            r = leb.Room(open(_rooms._leb_path(romfs, room), 'rb').read())
            for a in r.actors:
                if a.type == 0x102:
                    out.append(next((i for t, i in a.switches if t == 1), None) or f'{room}-{len(out)}')
        except (OSError, ValueError):
            pass
        _block_cache[room] = out
    return _block_cache[room]


def door_terms(types, n, entering=False, room_types=(), lock_id=''):
    """frozenset of requirement atoms for passing these door types, or None if impassable. entering: going INTO the
    room that owns the doors (else leaving it). A one-way door passes one way: out of the room with the 0x1A3 half,
    into the room with the 0x1A4 half."""
    req = set()
    for t in types:
        if t == 'oneway':
            if (ONEWAY_IN if entering else ONEWAY_OUT) in room_types:
                continue
            return None
        if t in IMPASSABLE:
            return None
        if t == 'lock':
            req.add(f'LOCK|{n}|{lock_id}')  # (-> key-Dn:k, k by the worst-case key rule: key_counts)
        elif t == 'bossdoor':
            req.add(f'nightmare-key-D{n}')
        elif DOOR_REQ.get(t):
            req.add(DOOR_REQ[t])
    return frozenset(req)


def stairs_links(romfs, level, cat=None):
    """[(grid room, grid room)] joined through side-view passages under the stairs (side-view room = not a grid room
    of the catalog)."""
    import sys
    sys.path.insert(0, os.path.join(HERE, '..', '..', 'third_party', 'LAS-Randomizer'))
    from RandomizerCore.Tools import leb
    d = os.path.join(romfs, 'region_common', 'level', level)
    down, up = {}, {}  # grid -> side-view room; side-view -> grid
    for f in sorted(os.listdir(d)):
        if not f.endswith('.leb'):
            continue
        room = f[:-4]
        r = leb.Room(open(os.path.join(d, f), 'rb').read())
        for a in r.actors:
            n = a.name.decode(errors='ignore')
            if not a.parameters or not isinstance(a.parameters[0], bytes):
                continue
            target = a.parameters[0].decode(errors='ignore')
            if n.startswith('AreaDownstairs'):
                down.setdefault(room, set()).add(target)
            elif n.startswith('AreaUpstairs'):
                up.setdefault(room, set()).add(target)
    if cat is None:
        cat = json.load(open(os.path.join(HERE, 'room_catalog.json')))
    side = {r for r in set(up) | set(down) | {t for v in list(down.values()) + list(up.values()) for t in v} if r not in cat}
    parent = {s: s for s in side}

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x
    for s in side:  # side-view rooms next to each other (a row, or Bottle Grotto's 3-row area 04D-06E): one passage
        m = re.search(r'_(\d\d)([A-P])$', s)
        if m:
            for nb in (f'{s[:m.start()]}_{m.group(1)}{chr(ord(m.group(2)) + 1)}',
                       f'{s[:m.start()]}_{int(m.group(1)) + 1:02d}{m.group(2)}'):
                if nb in parent:
                    parent[find(s)] = find(nb)
    comp = {}
    for a, ts in list(down.items()) + list(up.items()):  # stairs either way between a grid room and a passage
        for t in ts:
            if a in parent and t not in parent:
                comp.setdefault(find(a), set()).add(t)
            elif t in parent and a not in parent:
                comp.setdefault(find(t), set()).add(a)
    links = []
    for a, ts in list(down.items()) + list(up.items()):  # stairs straight from one grid room to another (Key Cavern's
        for t in ts:                                      # floors: 04B <-> 04G, 04C <-> 04H)
            if a in cat and t in cat and a != t:
                links.append(tuple(sorted((a, t))))
    for gs in comp.values():
        gs = sorted(g for g in gs if g not in side)  # (grid rooms only)
        for i in range(len(gs)):
            for j in range(i + 1, len(gs)):
                links.append((gs[i], gs[j]))
    return sorted(set(links))


def rooms_neighbour(room, side):
    import rooms
    return rooms.neighbour(room, side)


def level_graph(cat, romfs, level, layout, jump_rows, openings):
    """{cell: [(cell, frozenset req)]} directed edges; lock count."""
    n = LEVELS[level][0]
    pos = {cell_pos(c): c for c in layout}
    edges = {c: [] for c in layout}
    flags = set()  # distinct locks: one per 'opened' flag (a vanilla door's two halves share it)
    for cell, room in layout.items():
        for d in cat.get(room, {}).get('doors', []):
            if len(d) > 3 and d[2] == 'lock':
                flags.add(lock_flag(romfs, room, d[0]) or (room, d[0]))
    for room in set(layout.values()):
        flags.update(('block', b) for b in lock_blocks(romfs, room))
    locks = len(flags)
    jumped = set()
    for row in jump_rows:
        if row[0] != level:
            continue
        cell, side, own = row[1], row[2], int(row[3])
        if len(row) > 6:  # Chaos: (level, cell, side, own, partner, tcell, tside, ...)
            target = row[5]
        else:  # Coherent jump: to the neighbour
            c, r = cell_pos(cell)
            dc, dr = STEP[side]
            target = pos.get((c + dc, r + dr))
        jumped.add((cell, side, own))
        if target not in layout:
            continue
        req = door_terms(own_doors(cat, layout[cell], side, own), n, room_types=cat[layout[cell]].get('types', ()),
                         lock_id=lock_flag(romfs, layout[cell], side) or f'{cell}{side}{own}')
        if req is not None:
            edges[cell].append((target, req))
    for cell, room in layout.items():
        op = openings(cat, room)
        c, r = cell_pos(cell)
        for side in 'NSWE':
            nb = pos.get((c + STEP[side][0], r + STEP[side][1]))
            if nb is None:
                continue
            theirs = {x[0] for x in openings(cat, layout[nb])[OPP[side]]}
            # original neighbours at home: connected as in the game, even where the doorway model doesn't line up
            # (asked from both sides: a room whose doorway tiles hold its neighbour's door lists no opening there)
            home_pair = room == f'{level}_{cell}' and layout[nb] == f'{level}_{nb}' and \
                (rooms_neighbour(room, side) == layout[nb] or rooms_neighbour(layout[nb], OPP[side]) == room)
            slots = [x[0] for x in op[side]]
            if home_pair:  # (either room's doorway: the door's own tiles can hide it on one side)
                slots += [t for t in theirs if t not in slots]
            for slot in slots:
                if (cell, side, slot) in jumped or (slot not in theirs and not home_pair):
                    continue
                req_out = door_terms(own_doors(cat, room, side, slot), n, room_types=cat[room].get('types', ()),
                                     lock_id=lock_flag(romfs, room, side) or f'{cell}{side}{slot}')
                req_in = door_terms(own_doors(cat, layout[nb], OPP[side], slot), n, entering=True,
                                    room_types=cat[layout[nb]].get('types', ()),
                                    lock_id=lock_flag(romfs, layout[nb], OPP[side]) or f'{nb}{OPP[side]}{slot}')
                if req_out is None or req_in is None:
                    continue
                edges[cell].append((nb, req_out | req_in))
    for cell, room in layout.items():  # leaving a room with keyhole blocks: they may guard the way (see lock_blocks)
        bl = frozenset(f'LOCK|{n}|block{b}' for b in lock_blocks(romfs, room))
        if bl:
            edges[cell] = [(t, r | bl) for t, r in edges[cell]]
    for a, b in stairs_links(romfs, level, cat):
        ca, cb = a.rsplit('_', 1)[1], b.rsplit('_', 1)[1]
        if layout.get(ca) == a and layout.get(cb) == b:  # (stairs rooms stay home)
            edges[ca].append((cb, frozenset()))
            edges[cb].append((ca, frozenset()))
    for cell, room in layout.items():  # leaving a mini-boss room (not into its own unit): its soul (EXIT_REQ)
        if EXIT_REQ.get(room):
            edges[cell] = [(t, r if EXIT_REQ.get(layout.get(t)) == EXIT_REQ[room] else r | EXIT_REQ[room])
                           for t, r in edges[cell]]
    return edges, locks


def key_counts(edges, start):
    """{lock atom: keys it needs} by the worst-case key rule: a lock needs one key more than every OTHER lock that
    can be reached (and opened, wasting a key) without going through it. Safe for any order the player opens doors
    in; keys may sit behind earlier locks (the all-keys-before-any-lock rule made some layouts unfillable)."""
    locks = {a for outs in edges.values() for _, r in outs for a in r if a.startswith('LOCK|')}
    out = {}
    for L in locks:
        seen, todo, met = {start}, [start], set()
        while todo:
            a = todo.pop()
            for b, r in edges[a]:
                if L in r:
                    continue
                met |= {x for x in r if x.startswith('LOCK|')}
                if b not in seen:
                    seen.add(b)
                    todo.append(b)
        out[L] = len(met) + 1
    return out


def resolve_keys(terms, kc, n, total):
    """LOCK atoms of each term -> one key-Dn:k (the largest k of the term's locks, at most the dungeon's keys)."""
    out = set()
    for t in terms:
        ks = [kc.get(a, total) for a in t if a.startswith('LOCK|')]
        rest = {a for a in t if not a.startswith('LOCK|')}
        if ks:
            rest.add(f'key-D{n}:{min(max(ks), total)}')
        out.add(frozenset(rest))
    return out


def reach_terms(edges, start, base):
    """{cell: set of frozensets} minimal sum-of-products of what reaching the cell needs."""
    req = {c: set() for c in edges}
    req[start] = {frozenset([base])}
    changed = True
    while changed:
        changed = False
        for a, outs in edges.items():
            for b, er in outs:
                for t in list(req[a]):
                    nt = t | er
                    if any(o <= nt for o in req[b]):
                        continue
                    req[b] = {o for o in req[b] if not nt <= o}
                    req[b].add(nt)
                    if len(req[b]) > MAX_TERMS:  # keep the cheapest
                        req[b] = set(sorted(req[b], key=lambda t: (len(t), sorted(t)))[:MAX_TERMS])
                    changed = True
    return req


# ---------------------------------------------------------------- applying it to LAS's logic
def apply(cat, romfs, layouts, jump_rows, logic_defs, item_defs, settings, extra_locations=None):
    """Rewrites logic_defs / item_defs for the shuffled dungeons. extra_locations: {name: room} (pots...).
    Returns a summary string. Sets settings['dungeon-of'] (location -> 'Dn') and adds unreachable checks to
    settings['excluded-locations']."""
    import dungeon_gen
    # (dungeon_gen names cells like rooms, 'Lv01TailCave_08D'; here cells are '08D')
    layouts = {lv: {c.rsplit('_', 1)[1]: room for c, room in L.items()} for lv, L in layouts.items()}
    lr = json.load(open(os.path.join(HERE, 'location_rooms.json')))
    rooms_at = {}  # room -> [(level, cell)]
    cell_expr = {}  # (level, cell) -> condition string
    lock_count = {}
    logic_defs['rs-shutter'] = {'type': 'interaction', 'condition-basic': SHUTTER}
    boss_cell = {}
    junk_delta = [0]
    kcs = {}
    cell_keys = {}
    check_keys = {}  # location -> fewest keys reaching it takes
    for level, layout in layouts.items():
        n, region = LEVELS[level]
        # a dungeon region defined by its own cells (Face Shrine: "D6-8D | D6-3A"): the cells now hang off the region,
        # so the region becomes the way in from outside - the entrance cell's vanilla alternatives that don't go
        # through the dungeon itself (seed RapidBowWow994: no D6 check was reachable)
        rc = logic_defs.get(region, {}).get('condition-basic', '')
        if re.search(rf'\bD{n}-\d', rc):
            start0 = dungeon_gen.ENTRANCES[level].rsplit('_', 1)[1]
            ent = logic_defs.get(f'D{n}-{start0.lstrip("0")}', {}).get('condition-basic', '')
            outside = [a.strip() for a in top_level_or(ent) if not re.search(rf'\bD{n}-\d', a)]
            if outside:
                logic_defs[region]['condition-basic'] = ' | '.join(f'({a})' for a in outside)
        for cell, room in layout.items():
            rooms_at.setdefault(room, []).append((level, cell))
            for d in cat.get(room, {}).get('doors', []):
                if len(d) > 3 and d[2] == 'bossdoor':  # the boss sits behind it
                    c, r = cell_pos(cell)
                    dc, dr = STEP[d[0]]
                    boss_cell[level] = cell_name(c + dc, r + dr)
        edges, locks = level_graph(cat, romfs, level, layout, jump_rows, dungeon_gen.openings)
        start = dungeon_gen.ENTRANCES[level].rsplit('_', 1)[1]
        terms = reach_terms(edges, start, region)
        kc = key_counts(edges, start)
        kcs[level] = (kc, n)
        nlocks = len(kc)
        locks = max(locks, nlocks)  # (keys: at least one per lock the layout can meet)
        lock_count[n] = locks
        for cell, t in terms.items():
            cell_expr[(level, cell)] = dnf_str(resolve_keys(t, kc, n, locks))
            ks = [min(max([kc.get(a, locks) for a in x if a.startswith('LOCK|')] or [0]), locks) for x in t
                  if not any(a.startswith('nightmare-key') for a in x)]
            if ks:
                cell_keys[(level, cell)] = min(ks)  # (fewest keys any way in; the fill check below)
        # LAS's cell regions of this dungeon: "reach that cell of the new layout"
        for k, v in logic_defs.items():
            if v.get('type') == 'room' and re.match(rf'D{n}-\d\d?[A-P]$', k):
                cell = k.split('-', 1)[1].rjust(3, '0')
                logic_defs[k]['condition-basic'] = cell_expr.get((level, cell)) or f'nightmare-key-D{n} & !nightmare-key-D{n}'
        # small keys: as many as the new layout has locks (the pool keeps its size: extra keys replace junk, fewer
        # keys leave room for junk - LAS has exactly one item per location)
        if f'key-D{n}' in item_defs:  # exactly one per lock (user: no spare keys)
            delta = locks - item_defs[f'key-D{n}']['quantity']
            item_defs[f'key-D{n}']['quantity'] = locks
            junk_delta[0] += delta
    # balance the pool (LAS: one item per location): take junk for keys added, give junk back for keys removed. Only
    # what no check could still claim as its vanilla content is taken (the Color Dungeon's rupees stay vanilla and
    # need their 5-rupee items: seed RapidBowWow994)
    claimed = {}
    for v in logic_defs.values():
        if v.get('type') == 'item' and v.get('content'):
            claimed[v['content']] = claimed.get(v['content'], 0) + 1
    left = junk_delta[0]
    while left > 0:
        spare = sorted((k for k, d in item_defs.items() if k.startswith(('rupee-', 'chamber-stone'))
                        and d.get('quantity', 0) - claimed.get(k, 0) > 0),
                       key=lambda k: claimed.get(k, 0) - item_defs[k]['quantity'])
        if not spare:
            break
        item_defs[spare[0]]['quantity'] -= 1
        left -= 1
    if left < 0 and 'rupee-20' in item_defs:
        item_defs['rupee-20']['quantity'] -= left
    shuffled = {LEVELS[lv][0] for lv in layouts}
    # events and interactions that name vanilla cells of a shuffled dungeon: D<n>-clear = reach the boss arena of the
    # new layout + what the event needs there; others (ammo from a dungeon room's pots) lose those alternatives - the
    # room at that cell is another one now. Then vanilla key counts (D0-clear: key-D0:3; the new layout can have fewer
    # keys - seed DevWarp2: the Color Dungeon clear unreachable): every key of the new layout, the safe bound
    cell_tok = re.compile(r'D(\d)-\d\d?[A-P]$')
    for k, v in logic_defs.items():
        if v.get('type') in ('item', 'room') or k == 'face-shrine':
            continue
        m = re.match(r'D(\d)-clear$', k)
        if m and int(m.group(1)) in shuffled:
            lv = next(l for l, (i, _) in LEVELS.items() if i == int(m.group(1)))
            arena = cell_expr.get((lv, boss_cell.get(lv)))
            if arena:
                rest, _ = strip(parse(v.get('condition-basic', '')), lambda a: cell_tok.match(a) or
                                re.match(r'key-D\d(:\d+)?$', a))
                v['condition-basic'] = f'({arena}) & ({show(rest)})' if rest else arena
            continue
        for key in ('condition-basic', 'condition-advanced', 'condition-glitched', 'condition-hell'):
            c = v.get(key)
            if c and any(int(x) in shuffled for x in re.findall(r'\bD(\d)-\d\d?[A-P]\b', c)):
                n2 = falsify(parse(c), lambda a: bool(cell_tok.match(a)) and int(cell_tok.match(a).group(1)) in shuffled)
                v[key] = show(n2) if n2 else 'nightmare-key-D0 & !nightmare-key-D0'  # (never)
    for k, v in logic_defs.items():
        if v.get('type') in ('item', 'room'):
            continue
        for key in ('condition-basic', 'condition-advanced', 'condition-glitched', 'condition-hell'):
            c = v.get(key)
            if c and re.search(r'key-D\d:\d+', c):
                v[key] = re.sub(r'key-D(\d):\d+', lambda m: f'key-D{m.group(1)}:{lock_count[int(m.group(1))]}'
                                if int(m.group(1)) in shuffled and lock_count.get(int(m.group(1))) else m.group(0), c)
    region_tok = {k for k, v in logic_defs.items() if v.get('type') in ('room', 'region')}

    def drop(atom):
        return atom in region_tok or re.match(r'key-D\d(:\d+)?$', atom) is not None

    def remap_cells(cond):
        """Cell names of shuffled dungeons (D8-4C) in a vanilla condition -> reaching the room vanilla has there,
        wherever it is now. (condition, room gone from every layout?)"""
        gone = [False]

        def vanilla_room(m):
            x = int(m.group(1))
            if x not in shuffled:
                return m.group(0)
            xl = next(l for l, (i, _) in LEVELS.items() if i == x)
            ps = [cell_expr[p] for p in rooms_at.get(f'{xl}_{m.group(2).rjust(3, "0")}', []) if cell_expr.get(p)]
            gone[0] |= not ps
            return '(' + ' | '.join(f'({e})' for e in ps) + ')' if ps else m.group(0)
        return re.sub(r'\bD(\d)-(\d\d?[A-P])\b', vanilla_room, cond), gone[0]

    dungeon_of = settings.setdefault('dungeon-of', {})
    excluded = settings.setdefault('excluded-locations', set())
    absent = settings.setdefault('absent-locations', set())  # (their rooms aren't in the layout: not in the game)
    rewritten = unused = fallback = 0
    targets = {k: (v.get('room'), v.get('level')) for k, v in lr.items()}
    for k, room in (extra_locations or {}).items():
        targets[k] = (room, room.rsplit('_', 1)[0])
    for loc, v in list(logic_defs.items()):
        if v.get('type') != 'item' or loc not in targets:
            continue
        room, level = targets[loc]
        m = re.match(r'D(\d)-', loc)
        home = int(m.group(1)) if m else (LEVELS[level][0] if level in LEVELS else None)
        if home not in shuffled:
            continue
        if room is None and v.get('subtype') == 'boss':
            lv = next(l for l, (i, _) in LEVELS.items() if i == home)
            if lv in boss_cell:
                room = layouts[lv].get(boss_cell[lv])
        cond = v.get('condition-basic', '')
        if room is None:  # no room known (owl statues, Color Dungeon rupees...): vanilla condition, where its cell
            fallback += 1  # names (D8-4C) mean the room vanilla has there, wherever it is now (seed DevRL_chaos_off:
            n = home       # above-turtle-rock read "whatever room sits at 4C")
            hl = next(l for l, (i, _) in LEVELS.items() if i == n)
            c2, gone = remap_cells(cond)
            if gone:  # (its room isn't in any layout: never reachable)
                excluded.add(loc), absent.add(loc)
                unused += 1
                continue
            c2 = re.sub(rf'key-D{n}:\d+', f'key-D{n}:{lock_count.get(n, 0)}' if lock_count.get(n) else '', c2)
            v['condition-basic'] = re.sub(r'&\s*&', '&', c2).strip(' &') or LEVELS[hl][1]
            for key in ('condition-advanced', 'condition-glitched', 'condition-hell'):
                v.pop(key, None)
            continue
        places = rooms_at.get(room, [])
        def with_blocks(p):  # a check in a room with keyhole blocks: they may guard it
            e = cell_expr.get(p)
            bl = lock_blocks(romfs, room)
            if not e or not bl or p[0] not in kcs:
                return e
            kc, n = kcs[p[0]]
            k = max(kc.get(f'LOCK|{n}|block{b}', 1) for b in bl)
            return f'({e}) & key-D{n}:{k}'
        reach = [with_blocks(p) for p in places if cell_expr.get(p)]
        ck = [cell_keys[p] for p in places if p in cell_keys]
        if ck:
            k = min(ck)
            bl = lock_blocks(romfs, room)
            p0 = next((p for p in places if p in cell_keys and cell_keys[p] == k), None)
            if bl and p0 and p0[0] in kcs:
                kc0, n0 = kcs[p0[0]]
                k = max([k] + [kc0.get(f'LOCK|{n0}|block{b}', 1) for b in bl])
            check_keys[loc] = k
        if not reach and any(p[1] == room.rsplit('_', 1)[1] and p[0] == room.rsplit('_', 1)[0] for p in places):
            fallback += 1  # a room at home the graph can't enter (no doorways: Bottle Grotto 03C, reached by a fall):
            continue       # its vanilla condition, on the redefined cell regions
        if not reach:
            excluded.add(loc), absent.add(loc)
            unused += 1
            continue
        local, _ = strip(parse(cond), drop)
        r = reach[0] if len(reach) == 1 else ' | '.join(f'({x})' for x in reach)
        v['condition-basic'] = f'({r}) & ({show(local)})' if local else r
        for key in ('condition-advanced', 'condition-glitched', 'condition-hell'):
            v.pop(key, None)  # (the vanilla tricks describe the vanilla layout)
        lvl0 = next((p for p in places if LEVELS[p[0]][0] == home), places[0])[0]
        dungeon_of[loc] = f'D{LEVELS[lvl0][0]}'
        rewritten += 1
    # item locations the loop doesn't own (no room listed, not a dungeon name) that sit in a dungeon cell
    # (above-turtle-rock: "D8-4C")
    for loc, v in logic_defs.items():
        if v.get('type') != 'item' or loc in dungeon_of or loc in excluded:
            continue
        cond = v.get('condition-basic', '')
        if not any(int(x) in shuffled for x in re.findall(r'\bD(\d)-\d\d?[A-P]\b', cond)):
            continue
        c2, gone = remap_cells(cond)
        if gone:
            excluded.add(loc), absent.add(loc)
            unused += 1
            continue
        v['condition-basic'] = c2
        for key in ('condition-advanced', 'condition-glitched', 'condition-hell'):
            v.pop(key, None)
        rewritten += 1
    # can each dungeon be filled? (keysanity off: its keys go into its own checks.) Key j has to sit in a check that
    # needs at most j-1 keys - else the caller plans another layout
    if settings.get('keysanity', 'off') == 'off':
        need_k = {}  # 'Dn' -> [keys each check of the dungeon needs]
        for loc, d in dungeon_of.items():
            v = logic_defs.get(loc, {})
            if loc in excluded or v.get('type') != 'item' or loc not in check_keys or vanilla_check(loc, v, settings):
                continue
            need_k.setdefault(d, []).append(check_keys[loc])
        for level, (kc, n) in kcs.items():
            total = lock_count[n]
            ks = sorted(need_k.get(f'D{n}', []) + [total] * roomless_checks(settings).get(n, 0))
            for j in range(1, total + 1):
                if sum(1 for k in ks if k <= j - 1) < j:
                    settings['room-logic-unfillable'] = (f'{level}: key {j} of {total} has '
                                                         f'{sum(1 for k in ks if k <= j - 1)} checks to go in')
                    break
            else:
                if len(ks) < total + dungeon_items(settings):  # (room for the Nightmare Key, Map, Compass, Beak too)
                    settings['room-logic-unfillable'] = f'{level}: {len(ks)} checks for {total + dungeon_items(settings)} dungeon items'
    return (f'{len(layouts)} dungeons, {rewritten} checks rewritten, {unused} in unused rooms (excluded), '
            f'{fallback} without a room (vanilla condition), locks {dict(sorted(lock_count.items()))}')


def boss_door_ok(cat, romfs, level, layout, rows):
    """The generator's check (Coherent / Chaos): without passing a boss door, every cell is reachable except the boss
    arena and the instrument room (DevRL_chaos_off: Turtle Rock's entrance was paired straight into the room before
    the boss door - the whole dungeon sat behind the Nightmare Key). layout: dungeon_gen's ({'Lv.._08D': room})."""
    import dungeon_gen
    L = {c.rsplit('_', 1)[1]: r for c, r in layout.items()}
    edges, _ = level_graph(cat, romfs, level, L, rows, dungeon_gen.openings)
    start = dungeon_gen.ENTRANCES[level].rsplit('_', 1)[1]
    def reach(boss_doors):
        seen, todo = {start}, [start]
        while todo:
            a = todo.pop()
            for b, r in edges[a]:
                if b not in seen and (boss_doors or not any(x.startswith('nightmare-key') for x in r)):
                    seen.add(b)
                    todo.append(b)
        return seen
    behind = reach(True) - reach(False)  # (cells unreachable either way aren't this check's business)
    allowed = set()
    n = LEVELS[level][0]
    lr = json.load(open(os.path.join(HERE, 'location_rooms.json')))
    inst = (lr.get(f'D{n}-instrument') or {}).get('room')
    for cell, room in L.items():
        if room == inst:
            allowed.add(cell)
        for d in cat.get(room, {}).get('doors', []):
            if len(d) > 3 and d[2] == 'bossdoor':
                c, r = cell_pos(cell)
                allowed.add(cell_name(c + STEP[d[0]][0], r + STEP[d[0]][1]))
    # beyond the boss arena (the instrument room, the Color Dungeon's reward room): reached through the arena
    todo = [c for c in allowed if c in behind]
    while todo:
        a = todo.pop()
        for b, _ in edges.get(a, []):
            if b in behind and b not in allowed:
                allowed.add(b)
                todo.append(b)
    return not (behind - allowed)


INSTRUMENTS = ('full-moon-cello', 'conch-horn', 'sea-lilys-bell', 'surf-harp', 'wind-marimba', 'coral-triangle',
               'evening-calm-organ', 'thunder-drum')


def vanilla_check(loc, v, settings):
    """A check LAS keeps vanilla (RandomizerCore/shuffler.py run()): no dungeon item can go there. (An unshuffled
    instrument counted as room for one: DevGrid1's Tail Cave had 7 checks for 8 items - LAS crashed placing them.)"""
    if v.get('content') in INSTRUMENTS and not settings.get('shuffle-instruments'):
        return True
    if loc.startswith('D0-rupee-') and not settings.get('blupsanity'):
        return True
    return v.get('subtype') not in ('chest', 'boss', 'enemy', 'drop', 'npc', 'standing', 'overworld-statue',
                                    'dungeon-statue', 'underwater')


def dungeon_items(settings):
    """How many items besides the small keys stay in their dungeon: Nightmare Key, Map, Compass, Stone Beak, less
    what the dungeon-items setting sends anywhere."""
    return 4 - {'mc': 2, 'mcb': 3, 'stone-beak': 1}.get(settings.get('dungeon-items', 'none'), 0)


def lr_vanilla(k, v, settings):
    """vanilla_check() for a location_rooms.json entry (it has no content): LAS keeps it vanilla / deletes it."""
    if k.endswith('-instrument') and not settings.get('shuffle-instruments'):
        return True
    if v.get('subtype') == 'dungeon-statue' and not settings.get('owl-dungeon-gifts'):
        return True  # (LAS deletes the owl statues' checks then)
    return k.startswith('D0-rupee-') and not settings.get('blupsanity')


def roomless_checks(settings):
    """{dungeon number: checks LAS counts in that dungeon (by name) that have no room (owl statues...)} - they keep
    their vanilla condition, which needs every key of the new layout."""
    lr = json.load(open(os.path.join(HERE, 'location_rooms.json')))
    excluded = settings.get('excluded-locations', ())
    out = {}
    for k, v in lr.items():
        m = re.match(r'D(\d)-', k)
        if lr_vanilla(k, v, settings):
            continue
        if m and not v.get('room') and v.get('subtype') != 'boss' and k not in excluded:
            out[int(m.group(1))] = out.get(int(m.group(1)), 0) + 1
    return out


def fill_ok(cat, romfs, level, layout, rows, settings=None):
    """The generator's other check (keysanity off: a dungeon's keys go into its own checks): key j needs a check that
    takes at most j-1 keys (by the worst-case key rule) - the first lock reached needs as many checks before it as
    locks reachable from the start. layout: dungeon_gen's. (Only checks with a known room count; apply() checks the
    finished logic again.)"""
    import dungeon_gen
    L = {c.rsplit('_', 1)[1]: r for c, r in layout.items()}
    edges, locks = level_graph(cat, romfs, level, L, rows, dungeon_gen.openings)
    start = dungeon_gen.ENTRANCES[level].rsplit('_', 1)[1]
    kc = key_counts(edges, start)
    total = max(locks, len(kc))
    n = LEVELS[level][0]
    cell_k = {}
    for cell, terms in reach_terms(edges, start, 'in').items():
        ks = [max([kc.get(a, total) for a in t if a.startswith('LOCK|')] or [0]) for t in terms
              if not any(a.startswith('nightmare-key') for a in t)]
        if ks:
            cell_k[cell] = min(ks)
    lr = json.load(open(os.path.join(HERE, 'location_rooms.json')))
    per_room = {}
    for k, v in lr.items():
        if v.get('room') and v.get('subtype') != 'boss' and not lr_vanilla(k, v, settings or {}):
            per_room[v['room']] = per_room.get(v['room'], 0) + 1
    ks = []
    for room, count in per_room.items():
        cells = [c for c, r in L.items() if r == room and c in cell_k]
        if not cells:
            continue
        k = min(cell_k[c] for c in cells)
        bl = lock_blocks(romfs, room)
        if bl:
            k = max([k] + [kc.get(f'LOCK|{n}|block{b}', 1) for b in bl])
        ks += [k] * count
    settings = settings or {}
    ks += [total] * roomless_checks(settings).get(n, 0)
    return (all(sum(1 for k in ks if k <= j - 1) >= j for j in range(1, total + 1)) and
            len(ks) >= total + dungeon_items(settings))
