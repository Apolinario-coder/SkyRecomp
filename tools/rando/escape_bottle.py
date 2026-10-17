#!/usr/bin/env python3
"""Escape Bottle (runtime/hle/escape.c): every randomizer seed starts with the game's third bottle (Bottle index 2) as
a warp item. Tarin hands it over with the starting items (Tarin.bfevfl Event36, where LAS gives them), Link holds the
bottle up and the game's message box names it: an Item.bfevfl entry `EscapeBottle` showing System:EscapeBottle, our
own text in every language's System.msbt (English: nothing of the game's text is copied, only its control tags).

    apply(out_dir, romfs) -> True when Tarin's flow was changed
"""
import glob
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from msbt import Msbt  # noqa: E402

ENTRY = 'EscapeBottle'
_COLOR_ON = b'\x0e\x00\x00\x03\x00\x04\x00\xd9\x45\x6d\xff'  # (colour tag: the game's item-name red / plain white)
_COLOR_OFF = b'\x0e\x00\x00\x03\x00\x04\x00\xed\xed\xed\xff'
_PAGE = b'\x0e\x00\x00\x04\x00\x00\x00'


def text(wait):
    return (b'You got the ' + _COLOR_ON + b'Escape Bottle' + _COLOR_OFF + b'! Use it to\nwarp out of trouble.' + wait +
            _PAGE + b'In a dungeon: back to its entrance.\nAnywhere else: home to Mabe Village.' + wait + b'\0')


def _read(out_dir, romfs, rel):
    for base in (os.path.join(out_dir, 'romfs'), romfs):
        p = os.path.join(base, rel)
        if os.path.exists(p):
            with open(p, 'rb') as f:
                return f.read()
    return None


def apply(out_dir, romfs):
    from RandomizerCore.Tools import event_tools
    import keysanity_text as kt
    ev_dir = os.path.join(out_dir, 'romfs', 'region_common', 'event')
    tp = os.path.join(ev_dir, 'Tarin.bfevfl')
    if not os.path.exists(tp):
        return False
    flow = event_tools.readFlow(tp)
    fc = flow.flowchart
    e36 = event_tools.findEvent(fc, 'Event36')
    nxt = e36.data.nxt.v.name if e36 and e36.data.nxt.v else None
    if nxt is None:
        return False
    event_tools.createActionChain(fc, 'Event36', [
        ('Link', 'GenericItemGetSequenceByKey', {'itemKey': 'Bottle', 'keepCarry': False, 'messageEntry': ENTRY}),
        ('Inventory', 'AddItemByKey', {'itemKey': 'Bottle', 'count': 1, 'index': 2, 'autoEquip': False})], nxt)
    event_tools.writeFlow(tp, flow)
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
        # the box's end-of-page wait: the one the game's own bottle message ends with
        wait = next((r for k, g, r in reversed(kt._tokens(m.texts.get('GetBottle', b''))) if g == kt.WAIT), b'')
        m.set(ENTRY, text(wait))
        out = os.path.join(out_dir, 'romfs', rel)
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(out, 'wb') as f:
            f.write(m.pack())
    return True
