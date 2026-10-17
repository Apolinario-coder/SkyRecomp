#!/usr/bin/env python3
"""Write a dungeon level (.lvb) for any layout of top-down rooms (generated dungeons with new shapes).

A level holds (decoded 2026-10-08, notes/PLAN.md):
  area   one per room cell, named after the room: AABB (x0,-3.1,z0)-(x1,6,z1), a second AABB (y to 3.15),
         +0x30 offset of "level/<Lv>/<room>.leb" in the names section, CRC32(name) at +0x34, +0x3C zone index,
         +0x3E row (0-based), +0x3F column
  node   bounding-volume tree over the areas: AABB, u32 count<<16 | first area, u32 right<<16 | left (-1 = leaf);
         nodes in pre-order, areas ordered so every node's areas are contiguous
  zone   camera regions: u32 id, centre xyz, half extents xyz, camera params, then (+0x9c) u32 1, u8 type,
         u8 0, u8 count, u8 area indices[] (0xFF padded), BGM / ambience / environment names
  tagPlayerStart  named spawn points (name + xyz + rotation); staticObject  dungeon-map objects (doors, chests)
Everything not listed is copied from the level the layout is built in (base .lvb).

    build(base_lvb, rooms, starts=None, static=True) -> bytes
        rooms: [{'name': 'Lv01TailCave_04B', 'unit': 0, 'area_data': <the room's area record from its source
                 level>, 'fixed': side-view rooms keep the record as is, 'zone_data': their zone record}, ...]
                 (cells of one unit share a camera zone; area records are moved to the cell and renamed),
                 'path': the room file to load (default level/<Lv>/<name>.leb; a cell the game's RomFS has no file
                 for must point at an existing file name - the seed overrides that file's content),
                 'keep_zone': the zone id to keep (unchanged rooms: their actors carry it)
"""
import copy
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import fixedhash  # noqa: E402

LEAF = 3


def cell_box(name):
    rc = name.rsplit('_', 1)[1][:3]
    x0, z0 = (ord(rc[2]) - ord('A')) * 15.0, (int(rc[:2]) - 1) * 12.0
    return x0, z0, x0 + 15.0, z0 + 12.0


def _bvh(items):
    """items: [(name, x0, z0, x1, z1)] -> (ordered names, nodes [(aabb6, count, first, left, right)])"""
    order, nodes = [], []

    def rec(its):
        i = len(nodes)
        nodes.append(None)
        x0 = min(t[1] for t in its); z0 = min(t[2] for t in its)
        x1 = max(t[3] for t in its); z1 = max(t[4] for t in its)
        first = len(order)
        if len(its) <= LEAF:
            order.extend(t[0] for t in its)
            nodes[i] = ((x0, -3.1, z0, x1, 6.0, z1), len(its), first, -1, -1)
            return i
        axis = 1 if (x1 - x0) >= (z1 - z0) else 2
        its = sorted(its, key=lambda t: (t[axis] + t[axis + 2]) / 2)
        h = len(its) // 2
        left = rec(its[:h])
        right = rec(its[h:])
        nodes[i] = ((x0, -3.1, z0, x1, 6.0, z1), len(its), first, left, right)
        return i
    rec(items)
    return order, nodes


def build(base, rooms, starts=None, static=True, keep_tree=False):
    fh = fixedhash.FixedHash(base)
    area_e = fh.get(b'area').data
    node_e = fh.get(b'node').data
    zone_e = fh.get(b'zone').data
    tmpl_area = area_e.entries[0]
    top_zones = [e for e in zone_e.entries if e.data[0x9c + 4] == 3]
    tmpl_zone = top_zones[0] if top_zones else zone_e.entries[0]

    def box(r):  # 'fixed' rooms (side-view etc.) keep their area record as is
        if r.get('fixed'):
            x0, _, z0, x1, _, z1 = struct.unpack_from('<6f', r['area_data'], 0)
            return x0, z0, x1, z1
        return cell_box(r['name'])
    by_name = {r['name']: r for r in rooms}
    items = [(r['name'],) + box(r) for r in rooms]
    order, nodes = _bvh(items)
    if keep_tree:  # (debug) the base level's own tree and area order
        order = [e.name.decode() for e in area_e.entries]
        nodes = None
    idx = {n: i for i, n in enumerate(order)}

    # areas
    new_areas = []
    for n in order:
        r = by_name[n]
        d = bytearray(r.get('area_data') or tmpl_area.data)  # the room's own record (from its source level)
        if not r.get('fixed'):
            ox, _, oz = struct.unpack_from('<3f', d, 0)
            x0, z0, x1, z1 = cell_box(n)
            dx, dz = x0 - ox, z0 - oz
            for o in (0, 0x18):  # both boxes moved to the target cell
                bx0, by0, bz0, bx1, by1, bz1 = struct.unpack_from('<6f', d, o)
                struct.pack_into('<6f', d, o, bx0 + dx, by0, bz0 + dz, bx1 + dx, by1, bz1 + dz)
            struct.pack_into('<I', d, 0x34, zlib.crc32(n.encode()))
        e = copy.copy(tmpl_area)
        e.name, e.data, e.name_off = n.encode(), bytes(d), None
        new_areas.append(e)
    # names: "level/<Lv>/<room>.leb\0<room>\0" per area; the area record's +0x30 is the offset of its .leb path
    lv = order[0].rsplit('_', 1)[0] if order else ''
    names = bytearray()
    for e in new_areas:
        n = e.name.decode()
        path_off = len(names)
        path = by_name[n].get('path') or f'level/{n.rsplit("_", 1)[0] if "_" in n else lv}/{n}.leb'
        names += path.encode() + b'\0'
        e.name_off = len(names)
        names += e.name + b'\0'
        d = bytearray(e.data)
        struct.pack_into('<I', d, 0x30, path_off)
        e.data = bytes(d)
    area_e.entries = new_areas
    area_e.names = bytes(names)
    area_e.rebuild_index()

    # BVH
    tmpl_node = node_e.entries[0]
    new_nodes = list(node_e.entries) if nodes is None else []
    nodes = nodes or []
    for aabb, count, first, left, right in nodes:
        d = bytearray(tmpl_node.data)
        struct.pack_into('<6f', d, 0, *aabb)
        struct.pack_into('<I', d, 0x18, (count << 16) | first)
        struct.pack_into('<I', d, 0x1C, 0xFFFFFFFF if left < 0 else ((right << 16) | left))
        e = copy.copy(tmpl_node)
        e.data = bytes(d)
        new_nodes.append(e)
    node_e.entries = new_nodes

    # camera zones: one per unit
    units = {}
    for r in rooms:
        units.setdefault(r.get('unit', r['name']), []).append(r['name'])
    new_zones = []
    # zones are written sorted by id (the game looks them up by id): kept ids (unchanged rooms) and new ids mixed
    def unit_zid(kv):
        r0 = by_name[kv[1][0]]
        if r0.get('keep_zone') is not None:
            return r0['keep_zone']
        return kv[0] if isinstance(kv[0], int) else 1 << 30
    unit_items = sorted(units.items(), key=lambda kv: (unit_zid(kv), str(kv[0])))
    for zi, (u, names) in enumerate(unit_items):
        boxes = [box(by_name[n]) for n in names]
        x0 = min(b[0] for b in boxes); z0 = min(b[1] for b in boxes)
        x1 = max(b[2] for b in boxes); z1 = max(b[3] for b in boxes)
        r0 = by_name[names[0]]
        zd = r0.get('zone_data')  # the room's own zone record (camera settings, environment, music)
        d = bytearray(zd if zd else tmpl_zone.data)
        # 'keep_zone': rooms whose files are unchanged (anchors, side-view rooms) keep their zone id - their actors
        # record it (actor roomID)
        zid = r0['keep_zone'] if r0.get('keep_zone') is not None else (u if isinstance(u, int) and u >= 300 else 300 + zi)
        struct.pack_into('<I', d, 0, zid)
        if not r0.get('fixed'):  # side-view zones keep their own camera box
            cy, hy = struct.unpack_from('<f', d, 8)[0], struct.unpack_from('<f', d, 20)[0]
            struct.pack_into('<6f', d, 4, (x0 + x1) / 2, cy, (z0 + z1) / 2, (x1 - x0) / 2, hy, (z1 - z0) / 2)
        ids = sorted(idx[n] for n in names)[:12]
        tail = bytearray(d[0x9c:0xb0])
        tail[6] = len(ids)
        tail[7:19] = bytes(ids + [0xFF] * (12 - len(ids)))
        d[0x9c:0xb0] = tail
        e = copy.copy(tmpl_zone)
        e.data = bytes(d)
        new_zones.append(e)
    zone_e.entries = new_zones
    # area record +0x3C = index of the zone holding it (the level indexes a per-zone list with it: a stale value
    # from the source level throws std::out_of_range), +0x3E / +0x3F = the cell's row (0-based) and column
    zone_of = {}
    for zi, (u, names) in enumerate(unit_items):
        for n in names:
            zone_of[n] = zi
    for e in area_e.entries:
        n = e.name.decode()
        d = bytearray(e.data)
        struct.pack_into('<H', d, 0x3C, zone_of.get(n, 0))
        if not by_name[n].get('fixed'):
            rc = n.rsplit('_', 1)[1][:3]
            d[0x3E] = int(rc[:2]) - 1
            d[0x3F] = ord(rc[2]) - ord('A')
            # floor margin: some rooms' boxes start at y -0.1 (Face Shrine 04A/05A); Link settling after a door jump
            # dips to -0.14 -> outside every room -> the game puts him back at the entrance. Never above -0.5.
            for off in (0x04, 0x1C):
                if struct.unpack_from('<f', d, off)[0] > -0.5:
                    struct.pack_into('<f', d, off, -0.5)
        e.data = bytes(d)

    if starts is not None:
        st = fh.get(b'tagPlayerStart').data
        tmpl = st.entries[0]
        new = []
        for name, (x, y, z, rot) in starts.items():
            e = copy.copy(tmpl)
            e.name, e.name_off = name.encode(), None
            e.data = struct.pack('<4f', x, y, z, rot) + tmpl.data[16:]
            new.append(e)
        st.entries = new
        st.names = b''
        st.rebuild_index()
    if not static:
        fh.get(b'staticObject').data.entries = []
    return fh.write()


if __name__ == '__main__':
    # self-test: rebuild a level from its own areas, keep starts / static objects
    base = open(sys.argv[1], 'rb').read()
    fh = fixedhash.FixedHash(base)
    names = [e.name.decode() for e in fh.get(b'area').data.entries]
    zones = fh.get(b'zone').data.entries
    unit_of = {}
    for zi, z in enumerate(zones):
        n = z.data[0x9c + 6]
        for a in z.data[0x9c + 7:0x9c + 7 + n]:
            unit_of[names[a]] = zi
    areas = {e.name.decode(): e.data for e in fh.get(b'area').data.entries}
    rooms = []
    for n in names:
        zi = unit_of.get(n)
        r = {'name': n, 'area_data': areas[n]}
        if zi is not None:
            r['unit'] = struct.unpack_from('<I', zones[zi].data, 0)[0]  # keep zone ids (static objects use them)
            r['zone_data'] = zones[zi].data
            r['fixed'] = zones[zi].data[0x9c + 4] != 3  # side-view: keep area + zone as they are
        else:
            r['unit'] = n
        rooms.append(r)
    out = build(base, rooms, keep_tree=len(sys.argv) > 3)
    open(sys.argv[2], 'wb').write(out)
    print('ok', len(rooms), 'rooms', len(out), 'bytes')
