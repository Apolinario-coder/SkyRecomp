#!/usr/bin/env python3
"""Playthrough spheres for a seed (used by las_headless's beatable check and the readable spoiler).

Sphere 1 = every item location reachable with the starting items; the items found there only count from sphere 2
on, and so on until nothing new is reachable. Logic nodes that aren't item locations (regions, rooms, tricks,
enemies) are re-evaluated within a sphere as soon as anything changes, exactly like LAS's own fixpoint.

check(key, access) must answer "is logic node `key` satisfied with `access`" (LAS's ItemShuffler.checkAccess
with the seed's logic level bound in).
"""
import re

INSTRUMENTS = ['full-moon-cello', 'conch-horn', 'sea-lilys-bell', 'surf-harp', 'wind-marimba', 'coral-triangle',
               'evening-calm-organ', 'thunder-drum']


def base_item(item):
    """'heart-piece[13]' -> 'heart-piece' (placements carry an index for counted items)."""
    return re.sub(r'\[\d+\]$', '', item or '')


def spheres(logic_defs, placements, access, check):
    """-> (list of spheres [[(location, item), ...], ...], access after everything, set of reached nodes).
    `access` (token -> count) starts as the settings + starting items and is updated in place."""
    seen = set()
    out = []
    while True:
        found = []
        grew = True
        while grew:  # within a sphere: logic states settle, items wait for the next sphere
            grew = False
            for key, node in logic_defs.items():
                if key in seen or node.get('type') == 'enemy':
                    continue
                try:
                    ok = check(key, access)
                except Exception:
                    ok = False
                if not ok:
                    continue
                seen.add(key)
                if node.get('type') in ('item', 'follower'):
                    access[key] = access.get(key, 0) + 1
                    if placements.get(key):
                        found.append((key, placements[key]))
                else:
                    access[key] = access.get(key, 0) + 1
                grew = True
            for key, node in logic_defs.items():  # enemies, as LAS does: killable -> also [pit] / [heavy]
                if node.get('type') != 'enemy' or key in seen:
                    continue
                if check(key, access):
                    seen.add(key)
                    for k in (key, key + '[pit]', key + '[heavy]'):
                        access[k] = 1
                    grew = True
                    continue
                for v in ('pit', 'heavy'):
                    k = f'{key}[{v}]'
                    if f'condition-{v}' in node and not access.get(k) and check(k, access):
                        access[k] = 1
                        grew = True
        if not found:
            break
        out.append(found)
        for _, item in found:  # LAS counts the indexed name and the plain one (keys, shells, heart pieces)
            access[item] = access.get(item, 0) + 1
            b = base_item(item)
            if b != item:
                access[b] = access.get(b, 0) + 1
    return out, access, seen
