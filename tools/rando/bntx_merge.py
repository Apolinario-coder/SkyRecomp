#!/usr/bin/env python3
"""Merge texture archives: a dungeon level loads only map/<Level>.bntx, so a room brought in from another dungeon
needs its textures added to that file (real room models reference their dungeon's textures by name; only the
Chamber "Panel" models embed their own).

    merge(base_bytes, [other_bytes, ...], only=None) -> bytes   (textures whose names are already in the base are
                                                                 skipped; `only`: set of names to take)
    python3 bntx_merge.py <out> <base.bntx> <other.bntx>...

Uses LAS's bntx_editor for parsing / writing and resdic.py for the name dictionary (verified to rebuild the
game's own dictionaries bit for bit). Runs in the LAS venv.
"""
import copy
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', '..', 'third_party', 'LAS-Randomizer'))

import resdic  # noqa: E402
from RandomizerCore.Tools.bntx_editor import bntx_editor  # noqa: E402
from RandomizerCore.Tools.bntx_editor.structs import StringTable  # noqa: E402


def _open(data):
    e = bntx_editor.BNTXEditor()
    e.openFile(data)
    return e


def merge(base, others, only=None):
    e = _open(base)
    b = e.bntx
    have = {t.name for t in b.textures}
    for data in others:
        o = _open(data).bntx
        for t in o.textures:
            if t.name in have or (only is not None and t.name not in only):
                continue
            t = copy.copy(t)
            ent = StringTable.Entry(b.strTbl.endianness)
            ent.string = t.name
            ent.size_ = len(t.name.encode())
            ent.pos = 0
            b.strTbl.entries.append(ent)
            b.strTbl.count = len(b.strTbl.entries)
            t.nameIdx = len(b.strTbl.entries) - 1
            b.textures.append(t)
            have.add(t.name)
    b.texContainer.count = len(b.textures)
    # name dictionary: one entry per texture, in texture order
    d = b.texNameDict
    names = [t.name for t in b.textures]
    tree = resdic.build(names)
    root = d.entries[0]
    d.entries = [root]
    for i, (bit, l, r) in enumerate(tree):
        if i == 0:
            root.referenceBit, root.leftIdx, root.rightIdx = 0xFFFFFFFF, l, r
            continue
        x = StringTable.TexNameDict.Entry(d.endianness)
        x.referenceBit, x.leftIdx, x.rightIdx = bit, l, r
        x.strIdx = b.textures[i - 1].nameIdx
        d.entries.append(x)
    d.count = len(names)
    return e.save()


def copy_renamed(base, pairs):
    """pairs: [(source texture name, new name)] -> bytes. Each new name is (re)made as a copy of the source
    texture (an existing texture with that name is replaced). Used for dungeon-map room images."""
    e = _open(base)
    b = e.bntx
    by = {t.name: t for t in b.textures}
    targets = {new for _, new in pairs}
    copies = []
    for src, new in pairs:
        if src in by:
            t = copy.copy(by[src])
            t.name = new
            copies.append(t)
    keep = [t for t in b.textures if t.name not in targets]
    # rebuild the string table: every texture name once (the file name entry stays first)
    fname = b.strTbl.entries[b.header.nameIdx] if hasattr(b.header, 'nameIdx') else None
    b.strTbl.entries = [x for x in b.strTbl.entries if x.string not in targets]
    b.textures = keep
    for t in keep:
        t.nameIdx = b.strTbl.index(t.name)
    if fname is not None:
        b.header.nameIdx = b.strTbl.index(fname.string)
    for t in copies:
        ent = StringTable.Entry(b.strTbl.endianness)
        ent.string, ent.size_, ent.pos = t.name, len(t.name.encode()), 0
        b.strTbl.entries.append(ent)
        t.nameIdx = len(b.strTbl.entries) - 1
        b.textures.append(t)
    b.strTbl.count = len(b.strTbl.entries)
    b.texContainer.count = len(b.textures)
    d = b.texNameDict
    tree = resdic.build([t.name for t in b.textures])
    root = d.entries[0]
    d.entries = [root]
    for i, (bit, l, r) in enumerate(tree):
        if i == 0:
            root.referenceBit, root.leftIdx, root.rightIdx = 0xFFFFFFFF, l, r
            continue
        x = StringTable.TexNameDict.Entry(d.endianness)
        x.referenceBit, x.leftIdx, x.rightIdx = bit, l, r
        x.strIdx = b.textures[i - 1].nameIdx
        d.entries.append(x)
    d.count = len(b.textures)
    return e.save()


def _rebuild_dict(b):
    """Name dictionary for the current texture list (in texture order)."""
    d = b.texNameDict
    tree = resdic.build([t.name for t in b.textures])
    root = d.entries[0]
    d.entries = [root]
    for i, (bit, l, r) in enumerate(tree):
        if i == 0:
            root.referenceBit, root.leftIdx, root.rightIdx = 0xFFFFFFFF, l, r
            continue
        x = StringTable.TexNameDict.Entry(d.endianness)
        x.referenceBit, x.leftIdx, x.rightIdx = bit, l, r
        x.strIdx = b.textures[i - 1].nameIdx
        d.entries.append(x)
    d.count = len(b.textures)


def subset(sources, only):
    """A texture archive with exactly the textures named in `only`, taken from the first source that has each.
    The level's map bntx is loaded into a fixed-size pool: a generated level keeps only what its room models
    reference (a 78 MB merge ran out of memory on load, std::bad_alloc)."""
    e = _open(sources[0])
    b = e.bntx
    drop = {t.name for t in b.textures if t.name not in only}
    fname = b.strTbl.entries[b.header.nameIdx].string
    b.strTbl.entries = [x for x in b.strTbl.entries if x.string not in drop]
    b.textures = [t for t in b.textures if t.name not in drop]
    for t in b.textures:
        t.nameIdx = b.strTbl.index(t.name)
    b.header.nameIdx = b.strTbl.index(fname)
    b.strTbl.count = len(b.strTbl.entries)
    b.texContainer.count = len(b.textures)
    _rebuild_dict(b)
    return merge(e.save(), sources[1:], only=only)


def patch_in_place(base, set_from, rename=None):
    """Edit a texture archive without re-saving it (LAS's writer breaks archives with many textures - the 1053-
    texture ui/DgnTex.arc - even unchanged): rename textures (same name length) and overwrite pixel data with
    another texture's of identical format / size. rename: {old name: new name}; set_from: {texture: source}.
    The name dictionary is recomputed in place. Returns bytes of the same size."""
    import struct
    rename = rename or {}
    e = _open(base)
    b = e.bntx
    out = bytearray(base)
    by = {t.name: t for t in b.textures}
    for old, new in rename.items():
        t = by[old]
        ent = b.strTbl.entries[t.nameIdx]
        assert len(new.encode()) == ent.size_, (old, new)
        out[ent.pos + 2:ent.pos + 2 + ent.size_] = new.encode()
        ent.string = new
        t.name = new
    by = {t.name: t for t in b.textures}
    for dst, src in set_from.items():
        td, ts = by[dst], by[src]
        assert (td.format_, td.width, td.height, td.numMips, td.imageSize) == \
               (ts.format_, ts.width, ts.height, ts.numMips, ts.imageSize), (dst, src)
        od = struct.unpack_from('<Q', base, td.ptrsAddr)[0]
        os_ = struct.unpack_from('<Q', base, ts.ptrsAddr)[0]
        out[od:od + td.imageSize] = base[os_:os_ + ts.imageSize]
    if rename:
        d = b.texNameDict
        tree = resdic.build([t.name for t in b.textures])
        for i, (bit, l, r) in enumerate(tree):
            struct.pack_into('<IHH', out, d.pos + 8 + 16 * i, bit & 0xFFFFFFFF, l, r)
    return bytes(out)


if __name__ == '__main__':
    out, base, *others = sys.argv[1:]
    res = merge(open(base, 'rb').read(), [open(p, 'rb').read() for p in others])
    open(out, 'wb').write(res)
    print(out, len(_open(res).bntx.textures), 'textures', len(res), 'bytes')
