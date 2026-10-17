#!/usr/bin/env python3
"""Readable spoiler for a seed: rando/<seed>/spoiler_view.txt, shown in game (runtime/ui/spoiler_tab.mm).

  spoiler_view.py <seed dir> [...]

Sections: settings summary, starting items, a step-by-step walkthrough (playthrough spheres: "with what you have,
go get these"), every item by area, dungeon entrances, shuffled entrances, pots. Seeds made before spheres.json
existed are regenerated into build/ (same seed = same placements) to compute the walkthrough.

Markup read by the game: '# ' section, '## ' sub-heading, '- ' bullet, '  ' continuation, plain text otherwise.
"""
import json
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))

DUNGEONS = {'tail-cave': 'Tail Cave (D1)', 'bottle-grotto': 'Bottle Grotto (D2)', 'key-cavern': 'Key Cavern (D3)',
            'angler-tunnel': "Angler's Tunnel (D4)", 'catfish-maw': "Catfish's Maw (D5)", 'face-shrine': 'Face Shrine (D6)',
            'eagle-tower': "Eagle's Tower (D7)", 'turtle-rock': 'Turtle Rock (D8)', 'color-dungeon': 'Color Dungeon'}
DPREFIX = {'D0': 'Color Dungeon', 'D1': 'Tail Cave', 'D2': 'Bottle Grotto', 'D3': 'Key Cavern', 'D4': "Angler's Tunnel",
           'D5': "Catfish's Maw", 'D6': 'Face Shrine', 'D7': "Eagle's Tower", 'D8': 'Turtle Rock'}
AREAS = {'mabe-village': 'Mabe Village', 'toronbo-shores': 'Toronbo Shores', 'mysterious-woods': 'Mysterious Woods',
         'koholint-prairie': 'Koholint Prairie', 'tabahl-wasteland': 'Tabahl Wasteland', 'ukuku-prairie': 'Ukuku Prairie',
         'sign-maze': 'Signpost Maze', 'goponga-swamp': 'Goponga Swamp', 'taltal-heights': 'Tal Tal Heights',
         'marthas-bay': "Martha's Bay", 'kanalet-castle': 'Kanalet Castle', 'pothole-field': 'Pothole Field',
         'animal-village': 'Animal Village', 'yarna-desert': 'Yarna Desert', 'ancient-ruins': 'Ancient Ruins',
         'rapids-ride': 'Rapids Ride', 'taltal-mountains-east': 'Tal Tal Mountains (east)',
         'taltal-mountains-west': 'Tal Tal Mountains (west)', **DUNGEONS}
ITEMS = {'rooster': 'Flying Rooster (pet)', 'ghost': 'Ghost (pet)', 'marin-companion': 'Marin (pet)',
         'seashell': 'Secret Seashell','heart-piece': 'Piece of Heart', 'heart-container': 'Heart Container',
         'rupee-5': '5 Rupees', 'rupee-20': '20 Rupees', 'rupee-50': '50 Rupees', 'rupee-100': '100 Rupees',
         'rupee-300': '300 Rupees', 'bracelet': 'Power Bracelet', 'feather': "Roc's Feather", 'boots': 'Pegasus Boots',
         'rod': 'Magic Rod', 'powder': 'Magic Powder', 'bomb': 'Bombs', 'bombs': 'Bombs', 'bow': 'Bow',
         'hookshot': 'Hookshot', 'boomerang': 'Boomerang', 'shovel': 'Shovel', 'flippers': 'Flippers',
         'ocarina': 'Ocarina', 'shield': 'Shield', 'sword': 'Sword', 'trap': 'Trap!', 'song-ballad': 'Ballad of the Wind Fish',
         'song-mambo': "Manbo's Mambo", 'song-soul': 'Frog Song of Soul', 'full-moon-cello': 'Full Moon Cello',
         'conch-horn': 'Conch Horn', 'sea-lilys-bell': "Sea Lily's Bell", 'surf-harp': 'Surf Harp',
         'wind-marimba': 'Wind Marimba', 'coral-triangle': 'Coral Triangle', 'evening-calm-organ': 'Evening Calm Organ',
         'thunder-drum': 'Thunder Drum', 'tail-key': 'Tail Key', 'slime-key': 'Slime Key', 'angler-key': 'Angler Key',
         'face-key': 'Face Key', 'bird-key': 'Bird Key', 'magnifying-lens': 'Magnifying Lens', 'yoshi-doll': 'Yoshi Doll',
         'ribbon': 'Ribbon', 'dog-food': 'Dog Food', 'bananas': 'Bananas', 'stick': 'Stick', 'honeycomb': 'Honeycomb',
         'pineapple': 'Pineapple', 'hibiscus': 'Hibiscus', 'letter': 'Letter', 'broom': 'Broom',
         'fishing-hook': 'Fishing Hook', 'necklace': 'Necklace', 'scale': "Mermaid's Scale", 'red-tunic': 'Red Tunic',
         'blue-tunic': 'Blue Tunic', 'golden-leaf': 'Golden Leaf', 'mushroom': 'Sleepy Toadstool',
         'medicine': 'Secret Medicine', 'bottle': 'Bottle', 'zol-trap': 'Trap!', 'bomb-capacity': 'Bomb bag upgrade',
         'arrow-capacity': 'Quiver upgrade', 'powder-capacity': 'Magic Powder upgrade', 'chamber-stone': 'Chamber Stone'}
JUNK = re.compile(r'^(rupee-\d+|trap|zol-trap|arrows?(-\d+)?|bombs?-\d+|powder-\d+)$')


def pretty_item(it):
    base = re.sub(r'\[\d+\]$', '', it)
    m = re.match(r'^(D\d)-(small-key|key|nightmare-key|compass|map|stone-beak|beak)$', base)
    if m:
        kind = {'small-key': 'Small Key', 'key': 'Small Key', 'nightmare-key': 'Nightmare Key', 'compass': 'Compass',
                'map': 'Dungeon Map', 'stone-beak': 'Stone Beak', 'beak': 'Stone Beak'}[m.group(2)]
        return f'{kind} ({DPREFIX[m.group(1)]})'
    m = re.match(r'^(small-key|key|nightmare-key|compass|map|stone-beak)-?(D\d)$', base)
    if m:
        return pretty_item(f'{m.group(2)}-{m.group(1)}')
    if base.startswith('soul-'):  # boss souls (tools/rando/souls.py)
        name = base[5:].replace('-', ' ').title()
        return name + ("' Soul" if name.endswith('s') else "'s Soul")
    return ITEMS.get(base) or base.replace('-', ' ').title()


def pretty_loc(loc):
    m = re.match(r'^(D\d)-(.*)$', loc)
    if m:
        rest = m.group(2)
        pm = re.match(r'^pot-(\d\d[A-H])-(\d+)$', rest)
        if pm:
            return f'{DPREFIX[m.group(1)]}: pot {pm.group(2)} in room {pm.group(1)}'
        return f'{DPREFIX[m.group(1)]}: ' + rest.replace('-', ' ')
    pm = re.match(r'^pot-([A-Za-z0-9]+)-(\d\d[A-H])-(\d+)$', loc)
    if pm:
        name = re.sub(r'(?<!^)(?=[A-Z])', ' ', pm.group(1))
        return f'{name}: pot {pm.group(3)}'
    return loc.replace('-', ' ').capitalize()


def parse_spoiler(path):
    sections, cur = {}, None
    for line in open(path):
        if re.match(r'^[a-z-]+:\s*$', line):
            cur = line.strip()[:-1]
            sections[cur] = []
        elif cur and line.startswith('    '):
            sections[cur].append(line.strip())
    return sections


def ensure_spheres(seed_dir):
    p = os.path.join(seed_dir, 'spheres.json')
    if os.path.exists(p):
        return json.load(open(p))
    req = os.path.join(seed_dir, 'request.json')
    if not os.path.exists(req):
        return None
    tmp = os.path.join(ROOT, 'build', 'spoiler_regen', os.path.basename(seed_dir))
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    py = os.path.join(ROOT, 'third_party', 'LAS-Randomizer', 'venv', 'bin', 'python')
    subprocess.run([py, os.path.join(HERE, 'las_headless.py'), os.path.join(ROOT, 'extracted', 'romfs'), tmp, req],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    src = os.path.join(tmp, 'spheres.json')
    if not os.path.exists(src):
        return None
    shutil.copy(src, p)
    return json.load(open(p))


def build(seed_dir):
    seed = os.path.basename(os.path.normpath(seed_dir))
    sp = os.path.join(seed_dir, 'spoiler.txt')
    if not os.path.exists(sp):  # (seeds made before seed IDs: named after the folder)
        sp = os.path.join(seed_dir, f'spoiler_{seed}.txt')
    if not os.path.exists(sp):
        return None
    S = parse_spoiler(sp)
    settings = json.load(open(os.path.join(seed_dir, 'settings.json'))) if os.path.exists(os.path.join(seed_dir, 'settings.json')) else {}
    loc_area, loc_item = {}, {}
    for sec, lines in S.items():
        if sec in AREAS:
            for l in lines:
                if ':' in l:
                    loc, it = [x.strip() for x in l.split(':', 1)]
                    loc_area[loc], loc_item[loc] = sec, it
    shells_important = bool(settings.get('seashells-important'))
    # shuffled dungeons: "tail-cave -> eagle-tower" = Tail Cave's entrance leads into Eagle's Tower
    entered_via = {}
    for l in S.get('dungeon-entrances', []):
        a, b = [x.strip() for x in l.split('->')]
        if a != b:
            entered_via[b] = a

    def area_name(sec):
        n = AREAS.get(sec, 'Elsewhere')
        if sec in entered_via:
            n += f" - enter through {DUNGEONS[entered_via[sec]].split(' (')[0]}'s entrance"
        return n

    out = [f'# Seed {seed}']
    on = sorted(k for k, v in settings.items() if v is True and k not in ('create-spoiler',))
    out.append('Settings: ' + ', '.join(k.replace('-', ' ') for k in on))
    if 'starting-items' in S:
        out.append('Starting items: ' + ', '.join(pretty_item(i) for i in S['starting-items']))
    sph = ensure_spheres(seed_dir)
    out.append('')
    out.append('# Walkthrough')
    if sph:
        out.append('Each step can be done with what you had after the previous steps (any order within a step).')
        out.append('Only items that open things up are listed; rupees, traps and similar are left out.')
        n = 0
        for s in sph:
            keep = [(l, i) for l, i in s if not JUNK.match(re.sub(r'\[\d+\]$', '', i)) and not l.startswith('starting-')
                    and not re.search(r'(compass|map|beak)', i)
                    and (shells_important or not i.startswith('seashell'))
                    and not i.startswith(('heart-piece', 'heart-container'))]
            if not keep:
                continue
            n += 1
            out.append(f'## Step {n}')
            by_area = {}
            for l, i in keep:
                by_area.setdefault(area_name(loc_area.get(l, '')), []).append((l, i))
            for area in sorted(by_area):
                out.append(f'- {area}:')
                for l, i in sorted(by_area[area], key=lambda x: pretty_loc(x[0])):
                    out.append(f'  {pretty_loc(l)}  ->  {pretty_item(i)}')
        out.append('## Finish')
        out.append('- With all eight instruments, play the Ballad of the Wind Fish at the egg on Tal Tal Heights.')
    else:
        out.append('(No walkthrough: this seed has no request.json to recompute it.)')
    out.append('')
    out.append('# Items by area')
    for sec in S:
        if sec not in AREAS or not S[sec]:
            continue
        out.append(f'## {area_name(sec)}')
        for l in S[sec]:
            if ':' in l:
                loc, it = [x.strip() for x in l.split(':', 1)]
                out.append(f'- {pretty_loc(loc)}  ->  {pretty_item(it)}')
    if S.get('dungeon-entrances'):
        out.append('')
        out.append('# Dungeon entrances')
        for l in S['dungeon-entrances']:
            a, b = [x.strip() for x in l.split('->')]
            out.append(f'- {DUNGEONS.get(a, a)} entrance leads into {DUNGEONS.get(b, b)}')
    ent = os.path.join(seed_dir, f'entrances_{seed}.txt')
    if os.path.exists(ent):
        out.append('')
        out.append('# Entrances')
        mode = None
        for l in open(ent):
            if l.startswith('Entrances'):
                mode = 'in'
                out.append('## Doors (outside door -> interior it now leads into)')
                continue
            if l.startswith('Exits'):
                mode = 'out'
                out.append('## Exits (interior -> where leaving it puts you)')
                continue
            m = re.match(r'^\s+(\S+)\s+->\s+(\S+)', l)
            if m:
                a, b = (x.replace('-', ' ') for x in m.groups())
                out.append(f'- {a}  ->  {b}' if mode == 'in' else f'- leaving {a}  ->  outside {b}')
    with open(os.path.join(seed_dir, 'spoiler_view.txt'), 'w') as f:
        f.write('\n'.join(out) + '\n')
    return len(out)


if __name__ == '__main__':
    for d in sys.argv[1:]:
        print(d, build(d))
