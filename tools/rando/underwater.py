#!/usr/bin/env python3
"""Underwater checks: the five sunken Heart Pieces and the two sunken Secret Seashells hold randomized items, shown with
the item's own model, and Link still dives for them (as in LAS-Randomizer's 0.5 dev build, which this follows).

Only the game's heart-piece actor (ItemHeartPiece, 0xB0) can be dived for (the SinkingSword actor LAS uses for dry
heart pieces ignores a diving Link), so the sunken spots keep it and its Npc.gsheet entry takes everything from the
actor's own parameters ($n = parameter n, a feature of the game's datasheets):
  $1 / $2  model file / model name of the item it holds
  $3       entry point in SinkingSword.bfevfl run on pickup (actor "SinkingSword": Destroy, then the item get)
  $4       flag set on pickup; the actor only appears while it's off (layout condition)
  $5       'true' when it holds a Secret Seashell (the Seashell Sensor rings for it)
The sunken seashells (ItemSecretSeashell 0x87) become heart-piece actors the same way. Items.gsheet's HeartPiece uses a
copy of the original entry (PatchHeartPiece) so a heart piece held up anywhere else keeps its model. The runtime skips
the item actor's "already in the inventory?" check for heart-piece actors in seeds made with this (settings.json
"underwater-items"; tools/patches.py UNDERWATER_OWNED_SITE): with a randomized item a heart piece owned elsewhere
mustn't hide it - the flag does. Below the water only models whose materials have the shader option under_water are
seen (the heart piece's do); in these seeds every material counts as one (tools/patches.py UNDERWATER_VIEW_SITE).

    apply(mods, out_dir, romfs, leb, event_tools, oead_tools, item_get, data) -> spoiler lines
"""
import os

# location -> (level folder, room, actor type it has in vanilla)
SPOTS = {
    'bay-passage-sunken': ('MadBattersWellEnter', 'MadBattersWellEnter_01B', 0xB0),
    'river-crossing-cave': ('AnimalVillageEnter', 'AnimalVillageEnter_01B', 0xB0),
    'kanalet-moat-south': ('Field', 'Field_08J', 0xB0),
    'south-bay-sunken': ('Field', 'Field_15J', 0xB0),
    'taltal-east-drop': ('EagleTowerExit', 'EagleTowerExit_01A', 0xB0),
    'kanalet-moat-north': ('Field', 'Field_04I', 0x87),
    'taltal-sunken': ('Field', 'Field_02M', 0x87),
}
# flags: the heart pieces use LAS's own (data.HEART_FLAGS, made by flags.py); the seashells get two new ones
SHELL_FLAGS = {'kanalet-moat-north': 'KanaletSunkenShellGet', 'taltal-sunken': 'TaltalSunkenShellGet'}


def _level_dir(romfs, level):
    for d in (level, level.rstrip('0123456789')):
        if os.path.isdir(os.path.join(romfs, 'region_common', 'level', d)):
            return d
    return level


def apply(mods, out_dir, romfs, leb, event_tools, oead_tools, item_get, data):
    common_out = os.path.join(out_dir, 'romfs', 'region_common')
    common_in = os.path.join(romfs, 'region_common')

    def path(rel):
        p = os.path.join(common_out, rel)
        return p if os.path.exists(p) else os.path.join(common_in, rel)

    def out(rel):
        p = os.path.join(common_out, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        return p

    # flags for the seashells (after every other flag: existing indices stay)
    sheet = oead_tools.readSheet(path('datasheets/GlobalFlags.gsheet'))
    names = {str(v['symbol']) for v in sheet['values']}
    nxt = max(int(v['index']) for v in sheet['values'] if int(v['index']) < 2048) + 1
    for f in SHELL_FLAGS.values():
        if f not in names:
            if nxt >= 2048:
                raise RuntimeError('out of GlobalFlags indices for the sunken seashells')
            sheet['values'].append({'symbol': f, 'index': nxt})
            nxt += 1
    oead_tools.writeSheet(out('datasheets/GlobalFlags.gsheet'), sheet)

    flow = event_tools.readFlow(path('event/SinkingSword.bfevfl'))
    spoiler = []
    for name, (level, room_name, vtype) in SPOTS.items():
        if name not in mods.placements:
            continue
        flag = SHELL_FLAGS.get(name) or data.HEART_FLAGS[name]
        item_key, item_index, model_path, model_name = mods.getItemInfo(name, mods.trap_models)
        if item_key[:3] == 'Rup':
            get_anim = event_tools.createActionEvent(flow.flowchart, 'Inventory', 'AddItemByKey',
                                                     {'itemKey': item_key, 'count': 1, 'index': item_index,
                                                      'autoEquip': False})
        else:
            get_anim = item_get.insertItemGetAnimation(flow.flowchart, item_key, item_index)
        event_tools.addEntryPoint(flow.flowchart, name)
        event_tools.createActionChain(flow.flowchart, name, [
            ('SinkingSword', 'Destroy', {}),
            ('EventFlags', 'SetFlag', {'symbol': flag, 'value': True})], get_anim)

        rel = f'level/{_level_dir(romfs, level)}/{room_name}.leb'
        with open(path(rel), 'rb') as fh:
            room = leb.Room(fh.read())
        acts = [a for a in room.actors if a.type == vtype]
        if not acts:
            raise RuntimeError(f'{name}: no actor {vtype:#x} in {room_name}')
        act = acts[0]
        act.type = 0xB0
        if model_name not in ('HeartPiece', 'HeartContainer'):
            act.posY += 0.25  # (1/6 tile up, a bit smaller: other models sit lower than a heart piece)
            act.scaleX = act.scaleY = act.scaleZ = 0.75
        while len(act.parameters) < 8:
            act.parameters.append(b'')
        act.parameters[1] = bytes(model_path, 'utf-8')
        act.parameters[2] = bytes(model_name, 'utf-8')
        act.parameters[3] = bytes(name, 'utf-8')
        act.parameters[4] = bytes(flag, 'utf-8')
        act.parameters[5] = b'true' if item_key == 'Seashell' else b'false'
        if model_name in data.MODEL_SIZES:
            s = data.MODEL_SIZES[model_name]
            act.scaleX *= s
            act.scaleY *= s
            act.scaleZ *= s
        if model_name in data.MODEL_ROTATIONS:
            act.rotY = data.MODEL_ROTATIONS[model_name]
        with open(out(rel), 'wb') as fh:
            fh.write(room.repack())
        spoiler.append(f'{name}: {mods.placements[name]}')
    event_tools.writeFlow(out('event/SinkingSword.bfevfl'), flow)

    # Npc.gsheet: ItemHeartPiece from its parameters; PatchHeartPiece = the original, for heart pieces held up elsewhere
    npcs = oead_tools.readSheet(path('datasheets/Npc.gsheet'))
    vals = npcs['values']
    hp = next(v for v in vals if v['symbol'] == 'ItemHeartPiece')
    patch = None
    if not any(v['symbol'] == 'PatchHeartPiece' for v in vals):
        patch = oead_tools.parseStruct(hp)  # (a copy of the original, appended after the edit: an append moves the
        patch['symbol'] = 'PatchHeartPiece'  # array, so `hp` would no longer point into it)
    hp['graphics']['path'] = '$1'
    hp['graphics']['model'] = '$2'
    hp['eventInfo'] = {'eventAsset': 'SinkingSword.bfevfl', 'actorName': 'SinkingSword'}
    hp['eventTriggers'][0]['entryPoint'] = '$3'
    hp['layoutConditions'].append({'category': 1, 'parameter': '$4', 'layoutID': -1})
    hp['shellSensor'].append({'category': 9, 'parameter': '$5'})
    if patch is not None:
        vals.append(oead_tools.dictToStruct(patch))
    oead_tools.writeSheet(out('datasheets/Npc.gsheet'), npcs)
    items = oead_tools.readSheet(path('datasheets/Items.gsheet'))
    for it in items['values']:
        if it['symbol'] == 'HeartPiece':
            it['npcKey'] = 'PatchHeartPiece'
    oead_tools.writeSheet(out('datasheets/Items.gsheet'), items)
    return spoiler
