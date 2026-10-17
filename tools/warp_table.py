"""Generate runtime/ui/warp_data.h: every spawn point (TagPlayerStart, actor 0x18f) in the game's levels,
grouped for the menu's Warp tab.

Run with the LAS-Randomizer venv (it provides the LEB parser):
    third_party/LAS-Randomizer/venv/bin/python tools/warp_table.py
"""
import collections
import glob
import json
import os
import sys
import types

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LAS = os.path.join(ROOT, 'third_party', 'LAS-Randomizer')
LEVELS = os.path.join(ROOT, 'extracted', 'romfs', 'region_common', 'level')
OUT = os.path.join(ROOT, 'runtime', 'ui', 'warp_data.h')

sys.modules['PySide6'] = types.ModuleType('PySide6')
sys.modules['PySide6.QtCore'] = types.ModuleType('PySide6.QtCore')
sys.path.insert(0, LAS)
_cwd = os.getcwd()
os.chdir(LAS)
from RandomizerCore.Tools import leb  # noqa: E402
os.chdir(_cwd)

TAG_PLAYER_START, AREA_LEVEL_OPEN = 0x18F, 0x190

DUNGEONS = {
    'Lv01TailCave': "Level 1: Tail Cave",
    'Lv02BottleGrotto': "Level 2: Bottle Grotto",
    'Lv03KeyCavern': "Level 3: Key Cavern",
    'Lv04AnglersTunnel': "Level 4: Angler's Tunnel",
    'Lv05CatfishsMaw': "Level 5: Catfish's Maw",
    'Lv06FaceShrine': "Level 6: Face Shrine",
    'Lv07EagleTower': "Level 7: Eagle's Tower",
    'Lv08TurtleRock': "Level 8: Turtle Rock",
    'Lv09WindFishsEgg': "Wind Fish's Egg",
    'Lv10ClothesDungeon': "Color Dungeon",
}

PLACES = {
    'AnglersTunnelExit': "Angler's Tunnel exit cave",
    'AnimalVillageCave': "Animal Village cave",
    'AnimalVillageEnter': "Southern Face Shrine passage",
    'ArmosShrine': "Southern Face Shrine (Armos)",
    'ArmosShrineCave': "Armos Maze cave",
    'BananaHouse': "Sale's house O' bananas",
    'BearHouse': "Chef Bear's house",
    'BedRoom': "Mabe Village bedroom",
    'BoomerangCave': "Boomerang (Goriya) cave",
    'CatfishsMawEnter': "Catfish's Maw entrance",
    'ChristineHouse': "Christine's house",
    'CraneGame': "Trendy Game",
    'CuccoHouse': "Henhouse (Tal Tal)",
    'DanpeiHouse': "Dampe's shack",
    'DogHouse': "Bow-wow's dog house",
    'DrWrightHouse': "Mr. Write's house",
    'DreamShrine': "Dream Shrine",
    'EagleKeyCave': "Eagle's Tower key cave",
    'EagleTowerExit': "Eagle's Tower exit",
    'FaceShrineEnter': "Face Shrine passage",
    'LanmolaCave': "Yarna Desert cave (Lanmola)",
    'UnderGrave': "Cemetery cave",
    'FairyFountain01': "Great Fairy (east)",
    'FairyFountain02': "Great Fairy (Tal Tal)",
    'FairyFountain03': "Great Fairy (Ukuku)",
    'FishingBoat': "Fisherman's boat",
    'FishingPond': "Fishing pond",
    'FlyingCuccoCave': "Flying Rooster cave",
    'GhostHouse': "Seaside house (ghost)",
    'GopongaCave': "Goponga Swamp cave",
    'HiddenZoraHouse': "Hidden Zora's house",
    'KanaletCastle': "Kanalet Castle",
    'KanaletCastleEnter': "Kanalet Castle passage",
    'KanaletCastleSouthCave': "Kanalet Castle south cave",
    'Library': "Library",
    'MadBattersWell01': "Mad Batter (Martha's Bay)",
    'MadBattersWell02': "Mad Batter (Mysterious Woods)",
    'MadBattersWell03': "Mad Batter (Tal Tal)",
    'MadBattersWellEnter': "Mad Batter passage (bay)",
    'MadamHouse': "Madam MeowMeow's house",
    'MagicPowderHouse': "Witch's hut",
    'MamboCave': "Manbo's cave",
    'MamuCave': "Mamu's cave",
    'MarinTarinHouse': "Marin & Tarin's house",
    'MarthaUkuku': "Ukuku Prairie cave",
    'MermaidStatue': "Mermaid statue cave",
    'MoriblinCave': "Moblin hideout",
    'MysteriousWoodsCave01': "Mysterious Woods cave",
    'MysteriousWoodsCave02': "Mysterious Woods hollow",
    'QuadupletHouse': "Quadruplets' house",
    'RabbitBigHouse': "Animal Village (rabbits' house)",
    'RapidsRideExit': "Rapids Ride exit",
    'RapidsRideTicket': "Rapids Ride hut",
    'RichardCave': "Pothole field cave",
    'RichardHouse': "Richard's villa",
    'SchuleDonavitchHouse': "Schule Donavitch's house",
    'SeashellHouse': "Seashell Mansion",
    'Shop': "Town Tool Shop",
    'Tamaranch01': "Mt. Tamaranch cave (east)",
    'Tamaranch02': "Mt. Tamaranch cave (Tal Tal)",
    'Tamaranch03': "Mt. Tamaranch cave (far east)",
    'Tamaranch04': "Tal Tal Mountain cave (west)",
    'Tamaranch05': "Mt. Tamaranch cave (north)",
    'TelephoneBox01_Ukuku1': "Phone booth (Ukuku Prairie)",
    'TelephoneBox02_Mebe': "Phone booth (Mabe Village)",
    'TelephoneBox03_Kanalet': "Phone booth (Kanalet)",
    'TelephoneBox04_AnimalVillage': "Phone booth (Animal Village)",
    'TelephoneBox05_TurtleRock': "Phone booth (Turtle Rock)",
    'TelephoneBox06_Goponga': "Phone booth (Goponga Swamp)",
    'TelephoneBox07_Ukuku2': "Phone booth (Signpost Maze)",
    'TelephoneBox08_Martha': "Phone booth (Martha's Bay)",
    'TownWell': "Mabe Village well",
    'TracyHouse': "Crazy Tracy's house",
    'TurtleRockEnter': "Turtle Rock passage",
    'UkukuCave01': "Ukuku Prairie cave (west)",
    'UkukuCave02': "Ukuku Prairie cave (east)",
    'UlriraHouse': "Ulrira's house",
}

SKIP_LEVELS = ('Panel', 'Ending', 'EventMarin')  # Chamber Dungeons, ending and cutscene-only levels
SKIP_KINDS = {'Ending', 'MarinEventJump1', 'MarinEventJump2', 'MarinEventJump3', 'EventMarinJump',
              'PanelRewardIn', 'PanelRewardUI', 'FM_Restart', 'FishingMode'}
SKIP_LOCATORS = {'TestSetOnRaft'}

KIND_NAMES = {  # TagPlayerStart's 2nd parameter: how Link appears
    'DefaultDownStairsOut': 'stairs', 'DefaultUpStairsOut': 'stairs', 'DefaultUpDigStairsOut': 'stairs',
    'DefaultDownDigStairsOut': 'stairs', 'LadderDown': 'ladder', 'LadderUp': 'ladder', 'FallIn': 'drop in',
    'DungeonWarpIn': 'warp', 'OcarinaWarpDungeonIn': 'Manbo warp', 'DungeonIn': 'entrance',
    'DungeonInShort': 'entrance', 'EntranceIn': 'door', 'FallInRot': 'drop in', 'OcarinaWarpInWait': 'Manbo warp',
    'FieldWarpIn': 'warp hole',
}

WARP_NAMES = {
    'Warp_TalTalMountain': 'Warp hole: Tal Tal Mountain', 'Warp_TalTalHeights': 'Warp hole: Tal Tal Heights',
    'Warp_UkukuPrairie': 'Warp hole: Ukuku Prairie', 'Warp_EastOfTheBay': 'Warp hole: east of the bay',
    'Warp_TalTalMtWest': "Manbo's Mambo: Tal Tal Mountains", 'Warp_TalTalMtEastEast': "Manbo's Mambo: Tal Tal (east)",
    'Warp_DanpeiHouse': "Manbo's Mambo: Dampe's shack", 'Warp_MambogaPond': "Manbo's Mambo: Manbo's pond",
    'Warp_ShellHouseEast': "Manbo's Mambo: Seashell Mansion", 'Warp_MarthasBayWest': "Manbo's Mambo: Martha's Bay",
}

FIELD_NAMES = {  # overworld spawns no door leads to
    'Field_01K_d': 'Out of the waterfall', 'Field_07D': 'Raccoon (Tarin) clearing', 'Field_09B': 'Beach path',
}

# Overworld (Field) regions on the 16x16 room grid: rows 01-16 top to bottom, columns A-P left to right.
REGIONS = [  # (name, rows, cols); first match wins
    ("Turtle Rock", (1, 2), 'ABCD'),
    ("Tal Tal Mountain Range", (1, 2), 'EFGHIJKLMNOP'),
    ("Goponga Swamp", (3, 4), 'ABCDE'),
    ("Tal Tal Heights", (3, 4), 'FGHIJKLMNOP'),
    ("Mysterious Woods", (5, 7), 'ABCDEFG'),
    ("Cemetery", (8, 8), 'DEFGH'),
    ("Kanalet Castle", (5, 8), 'HIJKL'),
    ("Face Shrine & Rapids", (5, 10), 'MNOP'),
    ("Mabe Village", (9, 12), 'ABCD'),
    ("Southern Face Shrine", (11, 12), 'KLMNOP'),
    ("Ukuku Prairie", (8, 12), 'EFGHIJKL'),
    ("Toronbo Shores", (13, 16), 'ABCDEF'),
    ("Animal Village", (13, 14), 'LMN'),
    ("Yarna Desert", (13, 16), 'OP'),
    ("Martha's Bay", (13, 16), 'GHIJKLMN'),
]


def region(room):  # 'Field_11C' -> 'Mabe Village'
    code = room.split('_')[1]
    row, col = int(code[:2]), code[2]
    for name, (r0, r1), cols in REGIONS:
        if r0 <= row <= r1 and col in cols:
            return name
    return 'Koholint'


def s(x):
    return x.decode() if isinstance(x, bytes) else x


def main():
    spawns, zones = [], collections.defaultdict(set)  # zones[(level, locator)] = {source levels}
    for lv in sorted(os.listdir(LEVELS)):
        for f in sorted(glob.glob(os.path.join(LEVELS, lv, '*.leb'))):
            room = os.path.basename(f)[:-4]
            for a in leb.Room(open(f, 'rb').read()).actors:
                p = [s(x) for x in a.parameters]
                if a.type == TAG_PLAYER_START:
                    spawns.append((lv, room, p[0], p[1]))
                elif a.type == AREA_LEVEL_OPEN and p[0]:
                    zones[(p[0], p[1])].add(lv)

    groups = [[], [], []]  # (area, label, level, locator)
    for lv, room, loc, kind in spawns:
        if lv.startswith(SKIP_LEVELS) or kind in SKIP_KINDS or loc in SKIP_LOCATORS or not loc:
            continue
        if lv == 'Field':
            code = room.split('_')[1]
            if loc in WARP_NAMES or loc in FIELD_NAMES:
                label = WARP_NAMES.get(loc) or FIELD_NAMES[loc]
            else:
                src = sorted(x for x in zones.get(('Field', loc), ()) if x != 'Field')
                names = [DUNGEONS.get(x) or PLACES.get(x) or x for x in src]
                names = list(dict.fromkeys(names))
                label = ('Outside ' + ' / '.join(names[:2])) if names else (KIND_NAMES.get(kind) or kind or 'Spawn')
            groups[0].append((region(room), f'{label} ({code})', 'Field', loc))
            continue
        g = 2 if lv in DUNGEONS else 1
        area = DUNGEONS.get(lv) or PLACES.get(lv) or lv
        code = room[len(lv) + 1:] if room.startswith(lv + '_') else room
        from_field = 'Field' in zones.get((lv, loc), ())
        if from_field:
            label = 'Entrance'
        elif loc == 'OcarinaWarpPoint':
            label = "Manbo's Mambo point"
        else:
            label = 'Room ' + code
            k = KIND_NAMES.get(kind)
            if k:
                label += f' ({k})'
        # dungeon warp-tile arrivals and event-only starts run arrival events that expect their own setup:
        # jumping there directly throws in the event code (performance sweep, 2026-10-08) -> not offered
        if loc.endswith('_Warp') or lv.startswith('Event') or loc in ('DanpeiHouse_01A_b', 'DanpeiHouse_01A_c',
                                                                 'Lv07EagleTower_05E'):
            continue
        groups[g].append((area, label, lv, loc))

    # disambiguate repeated labels in an area: add the room code, then a counter
    for g in groups:
        for step in range(2):
            seen = collections.Counter((e[0], e[1]) for e in g)
            for i, (area, label, lv, loc) in enumerate(g):
                if seen[(area, label)] > 1:
                    if step == 0 and lv != 'Field':
                        code = loc[len(lv) + 1:] if loc.startswith(lv + '_') else loc
                        g[i] = (area, f'{label} ({code})' if code not in label else label, lv, loc)
                    elif step == 1:
                        g[i] = (area, label, lv, loc)
            if step == 1:
                count = collections.Counter()
                for i, (area, label, lv, loc) in enumerate(g):
                    if seen[(area, label)] > 1:
                        count[(area, label)] += 1
                        if count[(area, label)] > 1:
                            g[i] = (area, f'{label} #{count[(area, label)]}', lv, loc)

    region_order = [r[0] for r in REGIONS] + ['Koholint']
    groups[0].sort(key=lambda e: (region_order.index(e[0]), e[1]))
    groups[1].sort(key=lambda e: (e[0].lower(), e[1] != 'Entrance', e[1]))
    dorder = list(DUNGEONS.values())
    groups[2].sort(key=lambda e: (dorder.index(e[0]), e[1] != 'Entrance', e[1]))

    def q(x):
        return json.dumps(x)  # C-compatible for ASCII strings

    lines = ['// Generated by tools/warp_table.py from the level files (TagPlayerStart actors). Do not edit.',
             '#pragma once', '',
             'struct WarpSpot { int group; const char *area, *label, *level, *locator; };',
             'static const char *const kWarpGroups[] = {"Overworld", "Houses & caves", "Dungeons"};',
             'static const WarpSpot kWarpSpots[] = {']
    n = 0
    for gi, g in enumerate(groups):
        for area, label, lv, loc in g:
            lines.append(f'    {{{gi}, {q(area)}, {q(label)}, {q(lv)}, {q(loc)}}},')
            n += 1
    lines += ['};', '']
    open(OUT, 'w').write('\n'.join(lines))
    print(f'{n} spawn points -> {OUT} ({[len(g) for g in groups]})')


if __name__ == '__main__':
    main()
