#!/usr/bin/env python3
"""Found bombs ("Bombs must be found", LAS shuffle-bombs): their get-item message says what they are, in the game's own
message box - "You found your Bomb Bag! Now you can carry bombs again." (the game's own text, "You got a bomb! Way to
go!", reads like a single bomb).

With shuffle-bombs the only Bomb that shows a get-item message is the found one (shops, the crane and the guards give
bombs without one), so every Link.GenericItemGetSequenceByKey of itemKey Bomb in the seed's flows gets the messageEntry
`BombBag`: an Item.bfevfl entry showing System:BombBag, our own text in every language's System.msbt (only the game's
control tags are reused, as escape_bottle.py).

Link holds up a bag, not a bomb: the get sequence shows the Magic Powder sack (its itemKey only picks the model and the
animation; the Inventory.AddItemByKey after it still gives the bombs), and flag unused0431 is set around it so the
runtime tints that sack bomb blue (runtime/hle/escape.c bomb_bag_tint) - real Magic Powder keeps its own colour.

    apply(out_dir, romfs, settings) -> number of get events changed (0: not a found-bombs seed)
"""
import glob
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from msbt import Msbt  # noqa: E402

ENTRY = 'BombBag'
FLAG = 'unused0431'  # set while the Bomb Bag is held up (GlobalFlags index 431: runtime/hle/escape.c BOMB_BAG_FLAG)
_COLOR_ON = b'\x0e\x00\x00\x03\x00\x04\x00\xd9\x45\x6d\xff'  # (the game's item-name red / plain white)
_COLOR_OFF = b'\x0e\x00\x00\x03\x00\x04\x00\xed\xed\xed\xff'


def text(wait):
    return b'You found your ' + _COLOR_ON + b'Bomb Bag' + _COLOR_OFF + b'! Now you can\ncarry bombs again.' + wait + b'\0'


def _read(out_dir, romfs, rel):
    for base in (os.path.join(out_dir, 'romfs'), romfs):
        p = os.path.join(base, rel)
        if os.path.exists(p):
            with open(p, 'rb') as f:
                return f.read()
    return None


def apply(out_dir, romfs, settings):
    if not settings.get('shuffle-bombs'):
        return 0
    from RandomizerCore.Tools import event_tools
    import keysanity_text as kt  # (_retarget, _tokens)
    ev_dir = os.path.join(out_dir, 'romfs', 'region_common', 'event')
    n = 0
    for p in sorted(glob.glob(os.path.join(ev_dir, '*.bfevfl'))):
        flow = event_tools.readFlow(p)
        fc = flow.flowchart
        changed = False
        for e in list(fc.events):
            d = e.data
            try:
                act, prm = d.actor_action.v.v, d.params.data
            except AttributeError:
                continue
            if act == 'GenericItemGetSequenceByKey' and prm.get('itemKey') == 'Bomb':
                prm['messageEntry'] = ENTRY
                prm['itemKey'] = 'MagicPowder'  # (the sack held up; tinted while FLAG is set)
                # FLAG on before the get sequence, off after it
                off = event_tools.createActionEvent(fc, 'EventFlags', 'SetFlag', {'symbol': FLAG, 'value': False},
                                                   d.nxt.v.name if d.nxt.v else None)
                on = event_tools.createActionEvent(fc, 'EventFlags', 'SetFlag', {'symbol': FLAG, 'value': True}, e.name)
                on_ev, off_ev = event_tools.findEvent(fc, on), event_tools.findEvent(fc, off)
                kt._retarget(fc, e, on_ev, {on_ev})
                d.nxt.v = off_ev
                changed = True
                n += 1
        if changed:
            event_tools.writeFlow(p, flow)
    if not n:
        return 0
    ip = os.path.join(ev_dir, 'Item.bfevfl')
    if not os.path.exists(ip):
        with open(os.path.join(romfs, 'region_common', 'event', 'Item.bfevfl'), 'rb') as f, open(ip, 'wb') as g:
            g.write(f.read())
    flow = event_tools.readFlow(ip)
    if event_tools.findEntryPoint(flow.flowchart, ENTRY) is None:
        event_tools.addEntryPoint(flow.flowchart, ENTRY)
        event_tools.createActionChain(flow.flowchart, ENTRY, [('Dialog', 'Show', {'message': f'System:{ENTRY}'})])
    event_tools.writeFlow(ip, flow)
    for sysp in sorted(glob.glob(os.path.join(romfs, 'region*', '*', 'message', 'System.msbt'))):
        rel = os.path.relpath(sysp, romfs)
        m = Msbt(_read(out_dir, romfs, rel))
        # the box's end-of-page wait: the one the game's own bomb message ends with
        wait = next((r for k, g, r in reversed(kt._tokens(m.texts.get('GetBomb', b''))) if g == kt.WAIT), b'')
        m.set(ENTRY, text(wait))
        out = os.path.join(out_dir, 'romfs', rel)
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(out, 'wb') as f:
            f.write(m.pack())
    return n
