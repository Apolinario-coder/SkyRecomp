"""Walls of the Field screens for mini-bosses that roam them (runtime/hle/miniboss.c: Gohma's moves end at a wall,
as at its arena's walls): rando/<seed>/roam.tsv, one line per screen -

    write(out_dir, leb, romfs) -> summary  (the seed's screens; unedited ones from the game's romfs)

x0, z0 (the grid's corner), tile size, columns, rows, then one character per tile ('#' blocked, '.' open), row by row.
"""
import os


def write(out_dir, leb, romfs):
    field = os.path.join(out_dir, 'romfs', 'region_common', 'level', 'Field')
    vanilla = os.path.join(romfs, 'region_common', 'level', 'Field')
    rows = []
    for f in sorted(os.listdir(vanilla)):
        if not f.endswith('.leb'):
            continue
        src = os.path.join(field, f) if os.path.exists(os.path.join(field, f)) else os.path.join(vanilla, f)
        room = leb.Room(open(src, 'rb').read(), edit_grid=True)
        i = room.grid.info
        cells = ''.join('#' if (t.flags1['containscollision'] or t.flags1['deepwaterlava'] or t.flags3['iswaterlava'])
                        else '.' for t in room.grid.tilesdata)
        rows.append(f'{i.x_coord:g}\t{i.z_coord:g}\t{i.tile_size:g}\t{i.room_width}\t{i.room_height}\t{cells}\n')
    with open(os.path.join(out_dir, 'roam.tsv'), 'w') as fh:
        fh.writelines(rows)
    return f'{len(rows)} screens'
