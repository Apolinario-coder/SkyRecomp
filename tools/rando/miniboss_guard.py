"""Overworld copies of a mini-boss whose own flow gives a check (Lanmola: LAS puts Yarna Desert's item in
Lanmola.bfevfl, mod_generator.lanmolaChanges) must not give it again when beaten: each copy's switch 2 = a new global
flag (OverworldMinibossMarker) that the runtime sets when it wakes one (runtime/hle/miniboss.c, settings.json
"miniboss-marker-flag"); the flow asks CheckSawTheFirstEvent(switchIndex 2) before the item get - the original
(no switch 2: 0) gets the item as before, a copy (the marker set: 1) goes straight past it.

    apply(out_dir, romfs, leb, event_tools, oead_tools, settings) -> summary
"""
import os

# mini-boss type -> (event flow, actor name in it, the event LAS's item get follows, the event after it, the original's
# room in the Field)
GUARDS = {
    0x57: ('Lanmola.bfevfl', 'Lanmola', 'Event34', 'Event9', 'Field_13O'),
}
MARKER = 'OverworldMinibossMarker'


def apply(out_dir, romfs, leb, event_tools, oead_tools, settings):
    common_out = os.path.join(out_dir, 'romfs', 'region_common')
    common_in = os.path.join(romfs, 'region_common')

    def path(rel):
        p = os.path.join(common_out, rel)
        return p if os.path.exists(p) else os.path.join(common_in, rel)

    originals = set()  # (the original's own key: he keeps his item)
    for t, (_, _, _, _, room_name) in GUARDS.items():
        van = leb.Room(open(os.path.join(common_in, 'level', 'Field', f'{room_name}.leb'), 'rb').read())
        originals |= {a.key for a in van.actors if a.type == t}
    field = os.path.join(common_out, 'level', 'Field')
    rooms = []
    for f in sorted(os.listdir(field)) if os.path.isdir(field) else []:
        if not f.endswith('.leb'):
            continue
        room = leb.Room(open(os.path.join(field, f), 'rb').read())
        if any(a.type in GUARDS and a.key not in originals for a in room.actors):
            rooms.append((f, room))
    if not rooms:
        return 'no copies'
    # the marker flag (after every other flag: existing indices stay)
    rel = 'datasheets/GlobalFlags.gsheet'
    sheet = oead_tools.readSheet(path(rel))
    have = {str(v['symbol']): int(v['index']) for v in sheet['values']}
    if MARKER in have:
        idx = have[MARKER]
    else:
        idx = max(i for i in have.values() if i < 2048) + 1
        if idx >= 2048:
            raise RuntimeError('out of GlobalFlags indices for the overworld mini-boss marker')
        sheet['values'].append({'symbol': MARKER, 'index': idx})
        os.makedirs(os.path.join(common_out, 'datasheets'), exist_ok=True)
        oead_tools.writeSheet(os.path.join(common_out, rel), sheet)
    settings['miniboss-marker-flag'] = idx
    kinds, copies = set(), 0
    for f, room in rooms:
        for a in room.actors:
            if a.type in GUARDS and a.key not in originals:
                sw = list(a.switches)
                sw[2] = (1, idx)
                a.switches = sw
                kinds.add(a.type)
                copies += 1
        with open(os.path.join(field, f), 'wb') as fh:
            fh.write(room.repack())
    for t in sorted(kinds):
        name, actor, before, after, _ = GUARDS[t]
        flow = event_tools.readFlow(path(f'event/{name}'))
        fc = flow.flowchart
        first = event_tools.findEvent(fc, before).data.nxt.v.name  # (LAS's item get, inserted after `before`)
        sw = event_tools.createSwitchEvent(fc, actor, 'CheckSawTheFirstEvent', {'switchIndex': 2},
                                           {0: first, 1: after})
        event_tools.insertEventAfter(fc, before, sw)
        os.makedirs(os.path.join(common_out, 'event'), exist_ok=True)
        event_tools.writeFlow(os.path.join(common_out, f'event/{name}'), flow)
    return f'{copies} copies, marker flag {idx}'
