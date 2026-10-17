"""Hookshot anything (seed setting "hookshot-anything", runtime/hle/hookshot.c): the hookshot latches onto any solid
surface or object it hits and pulls Link to it, like a hookshot target. Where that crosses a gap that otherwise needs
other items, the logic gets a hookshot alternative.

Only routes checked in game are listed (Link pulled across, standing on the far side, the check collected). The
hookshot reaches 12 units (8 tiles) at hand height: it needs something solid ON the far side (a wall, a plant, a
chest); it flies over open floor, water and stepping stones without catching anything, so flat shores (Key Cavern's
stepping stones, Martha's Bay) gain nothing.
"""

# location / region -> extra alternative OR-ed into its condition-basic (the "hookshot-anything" token is the setting:
# LAS gives every true setting as access)
ROUTES = {
    # Goponga Swamp cave, left chest: hook the back wall across the pit in front of the chest ledge (the feather's
    # jump). Link lands next to a cracked floor tile: he must step off it (as after the jump).
    'goponga-cave-left': 'hookshot & hookshot-anything',
    # Mysterious Woods, Tail Key chest: hook the plants / chest across the column of holes on the east side
    # (break-bush still needed to clear the plants in front of the chest)
    'woods-north-chest': '(hookshot & hookshot-anything & break-bush)',
}


def apply(logic_defs):
    """Adds the routes to logic_defs (before the room shuffle / shuffler read it). Returns the number changed."""
    n = 0
    for name, alt in ROUTES.items():
        node = logic_defs.get(name)
        if not node or 'condition-basic' not in node:
            continue
        node['condition-basic'] = f"({node['condition-basic']}) | ({alt})"
        n += 1
    return n
