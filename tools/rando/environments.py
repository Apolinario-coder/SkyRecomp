#!/usr/bin/env python3
"""Randomized environments (cosmetic): each room's environment preset (lighting / fog / atmosphere: the zone's
`room_type` string at +0xB0 in the level's .lvb) is replaced by another preset of the same kind (top-down or
side-view). Ported from LAS-Randomizer dev (Owen-Splat, MIT; RandomizerCore/Fixes/levels.py); the preset lists below
are theirs (presets known to crash when swapped are left out there and here).

    apply(seed_dir, romfs, seed)  -> number of levels changed
"""
import os
import random
import sys
import types

HERE = os.path.dirname(os.path.abspath(__file__))
LAS = os.path.normpath(os.path.join(HERE, '..', '..', 'third_party', 'LAS-Randomizer'))


TOPDOWN_ENVIRONMENTS = (
    "AncientRuins",
    "AncientRuins2",
    "AncientRuinsDark",
    "Banana",
    "Bear",
    "BedRoom",
    "Cemetery",
    "Christine",
    "Crane",
    "Cucco",
    "Default",
    "Desert",
    "Dog",
    "DreamShrine",
    "DrWright",
    "DungeonSmall",
    "DungeonSmallTalTal",
    "DungeonSmallUnderground",
    "DungeonSmallWater",
    "DungeonSmallWaterDark",
    "Event1",
    "Event2",
    "EventEnding",
    "EventStairs",
    "Fairy",
    "Field",
    "FieldAncientRuins",
    "FieldBowWow",
    "FieldKanalet",
    "FishingBoat",
    "FishingPond",
    "Ghost",
    "Goponga",
    "GreatFairy",
    "HolyEgg",
    "ItemGet",
    "KanaletCastle",
    "Library",
    "Lv01Altar",
    "Lv01Base",
    "Lv02Altar",
    "Lv02Base",
    "Lv02Dark",
    "Lv02DoorOpen",
    "Lv02Teresa",
    "Lv03Altar",
    "Lv03Base",
    "Lv04Altar",
    "Lv04Base",
    "Lv05Altar",
    "Lv05Base",
    "Lv06Altar",
    "Lv06Base",
    "Lv06Dark",
    "Lv07Altar",
    "Lv07Base",
    "Lv08Altar",
    "Lv08Base",
    "Lv08Base2",
    "Lv08Dark",
    "Lv08Treasurebox",
    "Lv09Base",
    "Lv09Base2",
    "Lv09Base3",
    "Lv09BaseDark",
    "Lv10Base",
    "Lv11Base",
    "Madam",
    "MadBattersWell",
    "Magic",
    "MagicDark",
    "MagicField",
    # "MagicPowderHouse", # crashes if the env name is different, would need to edit the actual environment data
    "MamuCave",
    "MamuCaveDark",
    "MarinTarin",
    "MoriblinCave",
    "MysteriousForest",
    "Photo",
    "Quaduplet",
    "Rabbit",
    "RapidFlow",
    "RapidsRide",
    "Richard",
    "Schule",
    "Seashell",
    "Shop",
    "TalTalMountain",
    "TelBox",
    "ToronboShores",
    "Tracy",
    "Ulrira",
    "Underwater",
    "Zora"
)

SIDESCROLLER_ENVIRONMENTS = (
    "Angler",
    "KanaletCastleEnter",
    "Lv01Side",
    "Lv02Side",
    "Lv02Side2",
    "Lv03Side",
    "Lv04Side",
    "Lv04Side2",
    "Lv04Side3",
    "Lv05Side",
    "Lv05Side2",
    "Lv06Side",
    "Lv07Side",
    "Lv08Side",
    "Lv08Side2",
    "Lv08Side3",
    "MamboCave",

)

def apply(seed_dir, romfs, seed):
    sys.modules.setdefault('PySide6', types.ModuleType('PySide6'))
    sys.modules.setdefault('PySide6.QtCore', types.ModuleType('PySide6.QtCore'))
    if LAS not in sys.path:
        sys.path.insert(0, LAS)
    from RandomizerCore.Tools import lvb
    rng = random.Random(f'{seed}-environments')
    base = os.path.join(romfs, 'region_common', 'level')
    changed = 0
    for lv in sorted(os.listdir(base)):
        src = os.path.join(seed_dir, 'romfs', 'region_common', 'level', lv, lv + '.lvb')
        if not os.path.exists(src):
            src = os.path.join(base, lv, lv + '.lvb')
        if not os.path.exists(src):
            continue
        level = lvb.Level(open(src, 'rb').read())
        edited = False
        for z in level.zones:
            if z.room_type in TOPDOWN_ENVIRONMENTS:
                z.room_type = rng.choice(TOPDOWN_ENVIRONMENTS)
                edited = True
            elif z.room_type in SIDESCROLLER_ENVIRONMENTS:
                z.room_type = rng.choice(SIDESCROLLER_ENVIRONMENTS)
                edited = True
        if edited:
            out = os.path.join(seed_dir, 'romfs', 'region_common', 'level', lv)
            os.makedirs(out, exist_ok=True)
            open(os.path.join(out, lv + '.lvb'), 'wb').write(level.repack())
            changed += 1
    return changed
