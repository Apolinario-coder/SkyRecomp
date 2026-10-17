#!/usr/bin/env python3
"""Keysanity get-item text: a dungeon item found outside its dungeon says which dungeon it is for, in the game's own
message box ("You got a small key for Tail Cave!").

The get-item message is chosen before the item is added (Link.GenericItemGetSequenceByKey runs the Item.bfevfl
entry `messageEntry`, default = the item key, then Inventory.AddItemByKey gives it with its dungeon index), so the
index has to be known in the flow:
  - flows with constant gives (NPCs, standing items, pots, ...: item_get.insertItemGetAnimation): the get event's
    messageEntry becomes Ks<Item><index>;
  - flows that give their arguments (TreasureBox: itemKey / itemIndex from the chest): a CompareString / CompareInt
    chain in front of the get event picks the Ks entry (LAS patches CompareInt to test equality).
Item.bfevfl gets the Ks entries (Dialog.Show System:Ks<Item><index>), every language's System.msbt the messages,
built from that file's own get-item text and Place.msbt's dungeon name (nothing of the game's text lives here).
English: the item sentence + " for <dungeon>!"; other languages keep their text and add a page with the dungeon name.
Items in their own dungeon (index -1) keep the vanilla message.

    apply(out_dir, romfs, placements) -> number of messages added (0: no dungeon item outside its dungeon)
"""
import glob
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'third_party', 'LAS-Randomizer'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from msbt import Msbt  # noqa: E402

# LAS item name prefix -> (Items.gsheet key, System.msbt get message)
ITEMS = {'key-': ('SmallKey', 'GetSmallKey'), 'nightmare-key-': ('NightmareKey', 'GetBossKey'),
         'compass-': ('Compass', 'GetCompass'), 'map-': ('DungeonMap', 'GetMap'),
         'stone-beak-': ('StoneBeak', 'GetStoneBeak')}
GET_MSG = dict(ITEMS.values())
INDEXES = (0, 1, 2, 3, 4, 5, 6, 7, 9)  # dungeon index (upstream LAS dev convention): 0-7 = D1-D8, 9 = Color Dungeon


def place_label(index):
    return 'ClothesDungeon_map' if index == 9 else f'Lv{index + 1}Dungeon_map'


def entry(key, index):
    return f'Ks{key}{index}'


def used(placements):
    """{(item key, dungeon index)} of the dungeon items given with an explicit index"""
    out = set()
    for loc, idx in placements.get('indexes', {}).items():
        item = placements.get(loc)
        if not isinstance(item, str) or idx not in INDEXES:
            continue
        for prefix in sorted(ITEMS, key=len, reverse=True):  # (nightmare-key- before key-)
            if item.startswith(prefix):
                out.add((ITEMS[prefix][0], idx))
                break
    return out


# ---- message text (raw MSBT UTF-8 with control tags: 0E group type size params)
def _tokens(raw):
    out, i = [], 0
    while i < len(raw) and raw[i] != 0:
        if raw[i] == 0x0E:
            group, typ, size = struct.unpack_from('<HHH', raw, i + 1)
            out.append(('tag', (group, typ), raw[i:i + 7 + size]))
            i += 7 + size
        else:
            j = i
            while j < len(raw) and raw[j] not in (0, 0x0E):
                j += 1
            out.append(('text', None, raw[i:j]))
            i = j
    return out


COLOR, PAGE, WAIT = (0, 3), (0, 4), (1, 4)


def compose(get_raw, name_raw, english):
    t = _tokens(get_raw)
    name = b''.join(r for k, g, r in _tokens(name_raw))  # (not rstrip: a tag can end in zero bytes)
    wait = next((r for k, g, r in reversed(t) if g == WAIT), None)
    colors = [i for i, (k, g, r) in enumerate(t) if g == COLOR]
    if english and wait and len(colors) >= 2:
        on, off = t[colors[0]][2], t[colors[1]][2]
        head = b''.join(r for k, g, r in t[:colors[1] + 1])  # "You got a <small key>"
        shown = sum(len(r.decode('utf-8', 'replace')) for k, g, r in t[:colors[1] + 1] if k == 'text')
        sep = b' for ' if shown + len(name.decode('utf-8', 'replace')) + 6 <= 40 else b'\nfor '  # (box line ~40)
        return head + sep + on + name + off + b'!' + wait + b'\0'
    # other languages: their own text, then a page with the dungeon name
    if t and t[-1][1] == WAIT:
        t = t[:-1]
    body = b''.join(r for k, g, r in t)
    on, off = (t[colors[0]][2], t[colors[1]][2]) if len(colors) >= 2 else (b'', b'')
    wait = wait or b''
    return body + wait + b'\x0e\x00\x00\x04\x00\x00\x00' + on + name + off + wait + b'\0'


def _read(out_dir, romfs, rel):
    for base in (os.path.join(out_dir, 'romfs'), romfs):
        p = os.path.join(base, rel)
        if os.path.exists(p):
            with open(p, 'rb') as f:
                return f.read()
    return None


def write_messages(out_dir, romfs, combos):
    n = 0
    for sysp in sorted(glob.glob(os.path.join(romfs, 'region*', '*', 'message', 'System.msbt'))):
        rel = os.path.relpath(sysp, romfs)
        lang = rel.split(os.sep)[1]
        sysm = Msbt(_read(out_dir, romfs, rel))
        place = Msbt(_read(out_dir, romfs, rel.replace('System.msbt', 'Place.msbt'))).texts
        for key, idx in sorted(combos):
            get, name = sysm.texts.get(GET_MSG[key]), place.get(place_label(idx))
            if get is None or name is None:
                continue
            sysm.set(entry(key, idx), compose(get, name, lang.endswith('en')))
            n += 1
        out = os.path.join(out_dir, 'romfs', rel)
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(out, 'wb') as f:
            f.write(sysm.pack())
    return n


# ---- event flows
def _is(ev, actor, action):
    d = ev.data
    return (type(d).__name__ == 'ActionEvent' and d.actor.v.identifier.name == actor
            and d.actor_action.v.v == action)


def _retarget(fc, old, new, skip):
    """point everything that leads to `old` at `new` (except the events in `skip`: the new chain itself)"""
    for ev in fc.events:
        if ev in skip:
            continue
        d, t = ev.data, type(ev.data).__name__
        if t in ('ActionEvent', 'SubFlowEvent', 'JoinEvent') and d.nxt.v is old:
            d.nxt.v = new
        elif t == 'SwitchEvent':
            for c in d.cases.values():
                if c.v is old:
                    c.v = new
        elif t == 'ForkEvent':
            for f in d.forks:
                if f.v is old:
                    f.v = new
    for ep in fc.entry_points:
        if ep.main_event.v is old:
            ep.main_event.v = new


def patch_flow(flow, combos, event_tools):
    """set the Ks message entries in one flowchart; returns the number of get events changed"""
    import evfl
    fc = flow.flowchart
    keys = {k for k, i in combos}
    n = 0
    for ev in list(fc.events):
        if not _is(ev, 'Link', 'GenericItemGetSequenceByKey'):
            continue
        nxt = ev.data.nxt.v
        if nxt is None or not _is(nxt, 'Inventory', 'AddItemByKey'):
            continue
        p, q = ev.data.params.data, nxt.data.params.data
        key, idx = q.get('itemKey'), q.get('index')
        if not isinstance(key, evfl.common.Argument) and not isinstance(idx, evfl.common.Argument):
            if p.get('itemKey') == key and (key, idx) in combos:  # constant give
                p['messageEntry'] = entry(key, idx)
                n += 1
            continue
        if not (isinstance(key, evfl.common.Argument) and isinstance(idx, evfl.common.Argument)):
            continue
        # argument give (chests): itemKey == K and itemIndex == i -> a get event with the Ks entry
        before = {id(e) for e in fc.events}
        head = ev.name
        for k in sorted(keys):
            idx_head = ev.name
            for i in sorted(i for kk, i in combos if kk == k):
                get = event_tools.createActionEvent(fc, 'Link', 'GenericItemGetSequenceByKey',
                                                    {'itemKey': k, 'keepCarry': p.get('keepCarry', False),
                                                     'messageEntry': entry(k, i)}, nxt.name)
                idx_head = event_tools.createSwitchEvent(fc, 'FlowControl', 'CompareInt',
                                                         {'value1': str(idx), 'value2': i}, {0: get, 1: idx_head})
            head = event_tools.createSwitchEvent(fc, 'FlowControl', 'CompareString',
                                                 {'value1': str(key), 'value2': k}, {0: idx_head, 1: head})
        new = {e for e in fc.events if id(e) not in before}
        _retarget(fc, ev, event_tools.findEvent(fc, head), new)
        n += 1
    return n


def apply(out_dir, romfs, placements):
    combos = used(placements)
    if not combos:
        return 0
    from RandomizerCore.Tools import event_tools
    ev_dir = os.path.join(out_dir, 'romfs', 'region_common', 'event')
    changed = 0
    for p in sorted(glob.glob(os.path.join(ev_dir, '*.bfevfl'))):
        if os.path.basename(p) == 'Item.bfevfl':
            continue
        flow = event_tools.readFlow(p)
        c = patch_flow(flow, combos, event_tools)
        if c:
            event_tools.writeFlow(p, flow)
            changed += c
    # Item.bfevfl: one entry per (item, dungeon) showing its message
    ip = os.path.join(ev_dir, 'Item.bfevfl')
    if not os.path.exists(ip):
        os.makedirs(ev_dir, exist_ok=True)
        with open(os.path.join(romfs, 'region_common', 'event', 'Item.bfevfl'), 'rb') as f, open(ip, 'wb') as g:
            g.write(f.read())
    flow = event_tools.readFlow(ip)
    for key, idx in sorted(combos):
        if event_tools.findEntryPoint(flow.flowchart, entry(key, idx)) is None:
            event_tools.addEntryPoint(flow.flowchart, entry(key, idx))
            event_tools.createActionChain(flow.flowchart, entry(key, idx),
                                          [('Dialog', 'Show', {'message': f'System:{entry(key, idx)}'})])
    event_tools.writeFlow(ip, flow)
    msgs = write_messages(out_dir, romfs, combos)
    print(f'keysanity text: {len(combos)} item/dungeon pairs, {changed} get events, {msgs} messages', flush=True)
    return msgs


if __name__ == '__main__':  # preview: keysanity_text.py <romfs> [lang]  (prints the composed texts)
    from msbt import readable
    romfs, lang = sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else 'regionUS/USen'
    s = Msbt(open(os.path.join(romfs, lang, 'message', 'System.msbt'), 'rb').read()).texts
    pl = Msbt(open(os.path.join(romfs, lang, 'message', 'Place.msbt'), 'rb').read()).texts
    for key, msg in GET_MSG.items():
        for idx in INDEXES:
            print(f'{entry(key, idx):18}', repr(readable(compose(s[msg], pl[place_label(idx)], lang.endswith('en')))))
