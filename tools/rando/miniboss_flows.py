#!/usr/bin/env python3
"""Mini-bosses outside their arenas (enemy randomizer "mini-bosses in the enemy pool").

A mini-boss stays dormant until its <Name>Start event flow has run. That flow belongs to its dungeon arena: it
waits for the room scroll (Camera.WaitZeldaScroll) and walks Link in from the zone's entry side
(Link.MoveZoneEnterDirection) - on a Field screen neither ever finishes, so the flow never ends and the mini-boss
never wakes (hypothesis 2026-10-08). field_ready() turns those steps into zero-length waits.

    python3 miniboss_flows.py <romfs> <out romfs> <Name>...   (e.g. Hinox)
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'third_party', 'LAS-Randomizer'))

BLOCKING = {'WaitZeldaScroll', 'MoveZoneEnterDirection'}


def field_ready(flow):
    """Replaces the arena-only steps of every event in the flow by Timer.Wait(0). Returns how many."""
    import evfl
    from RandomizerCore.Tools import event_tools as et
    fc = flow.flowchart
    timer = et.findActor(fc, 'Timer')
    et.addActorAction(timer, 'Wait')
    n = 0
    for e in fc.events:
        d = e.data
        if type(d).__name__ != 'ActionEvent' or d.actor_action.v.v not in BLOCKING:
            continue
        d.actor = evfl.util.make_rindex(timer)
        d.actor.set_index(et.invertList(fc.actors))
        d.actor_action = evfl.util.make_rindex(timer.find_action('Wait'))
        d.actor_action.set_index(et.invertList(timer.actions))
        d.params = evfl.container.Container()
        d.params.data = {'time': 0.0}
        n += 1
    return n


if __name__ == '__main__':
    from RandomizerCore.Tools import event_tools as et
    romfs, out = sys.argv[1:3]
    for name in sys.argv[3:]:
        flow = et.readFlow(os.path.join(romfs, 'region_common', 'event', name + '.bfevfl'))
        n = field_ready(flow)
        dst = os.path.join(out, 'region_common', 'event', name + '.bfevfl')
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        et.writeFlow(dst, flow)
        print(name, n, 'steps replaced')
