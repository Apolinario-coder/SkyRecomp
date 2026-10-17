#!/usr/bin/env python3
"""Lossless reader/writer for the game's FixedHash containers (.lvb levels, .leb rooms).

LAS's RandomizerCore/Tools/fixed_hash.py recomputes name offsets and hashes and loses fields (a .lvb doesn't
round-trip), which matters once we write whole levels. This keeps every stored field and only recomputes
sizes / offsets, so an unmodified file is written back byte for byte (python3 fixedhash.py <files...> checks).

Layout (all little-endian; alignments are to absolute file offsets):
  u8 magic, u8 version, u16 bucket count B, u16 node count, u16 x6, u32 buckets[B + 1]
  -> align 8, u64 entry table size, entries[n] x 16: u16 node index (<= 0xFFED: child FixedHash, >= 0xFFF0: raw),
     u16 name offset, u32 hash, u32 next (byte offset of the next entry in the bucket chain or -1),
     u32 data offset (into the data section)
  u64 size, u32 entry offsets[n]  -> align 8, u64 data size, data section -> align 4, u32 names size, names
Children are stored back to back in the data section; a raw entry is u64 size + bytes, 8-aligned (a trailing
raw block is padded to 8 too). The u64 entry-table size sits at (end of buckets + 3) & -8, so when that end is
4 mod 8 the last "bucket" word is really the low half of the size.
Verified on all 102 .lvb files: 13 byte-identical, 89 identical apart from padding bytes (the originals hold
uninitialised memory there). Rooms (.leb) are written with LAS's leb.py (works in game); this writer is 8 bytes
short on .leb (not needed so far).
"""
import struct
import sys


def _hash(name):
    h = 0
    for c in name:
        h ^= (c + (h >> 2) + (h << 5)) & 0xFFFFFFFF
    return h


class Entry:
    def __init__(self, node, name, h, nxt, data):
        self.node, self.name, self.hash, self.next, self.data = node, name, h, nxt, data

    @property
    def is_hash(self):
        return self.node <= 0xFFED


class FixedHash:
    def __init__(self, data=None, off=0):
        self.entries = []
        if data is not None:
            self._read(data, off)

    def _read(self, d, off):
        self.magic, self.version, nb, self.nodes, self.x6 = struct.unpack_from('<BBHHH', d, off)
        self.buckets = list(struct.unpack_from(f'<{nb + 1}I', d, off + 8))
        eo = ((off + 8 + 4 * (nb + 1) + 3) & -8) + 8
        n = struct.unpack_from('<Q', d, eo - 8)[0] // 16
        oo = eo + 16 * n + 8
        self.entry_offsets = list(struct.unpack_from(f'<{n}I', d, oo))
        ds = ((oo + 4 * n + 7) & -8) + 8
        dsize = struct.unpack_from('<Q', d, ds - 8)[0]
        ns = ((ds + dsize + 3) & -4) + 4
        nsize = struct.unpack_from('<I', d, ns - 4)[0]
        self.names = d[ns:ns + nsize]
        self.end = ns + nsize
        self.data_layout = []  # data offsets as stored (kept when unchanged)
        for i in range(n):
            node, noff, h, nxt, doff = struct.unpack_from('<HHIII', d, eo + 16 * i)
            name = d[ns + noff:d.index(b'\0', ns + noff)] if nsize else b''
            if node <= 0xFFED:
                data = FixedHash(d, ds + doff)
            else:
                size = struct.unpack_from('<Q', d, ds + doff)[0]
                data = d[ds + doff + 8:ds + doff + 8 + size]
            e = Entry(node, name, h, nxt, data)
            e.name_off = noff
            self.entries.append(e)
            self.data_layout.append(doff)

    def write(self, off=0):
        """Bytes of this container placed at absolute file offset `off`."""
        nb = len(self.buckets) - 1
        out = bytearray(struct.pack('<BBHHH', self.magic, self.version, nb, self.nodes, self.x6))
        out += struct.pack(f'<{nb + 1}I', *self.buckets)
        n = len(self.entries)

        def pad_to(buf, align, base=off):
            while (base + len(buf)) % align:
                buf.append(0)
        # the u64 entry-table size sits at (x + 3) & -8 (x = end of the buckets): when x % 8 == 4 it overlaps
        # the last bucket word (that "extra bucket" is the low half of the size)
        x = off + len(out)
        size_pos = (x + 3) & -8
        del out[size_pos - off:]
        pad_to(out, 1)
        while off + len(out) < size_pos:
            out.append(0)
        out += struct.pack('<Q', 16 * n)
        eo = len(out)
        out += b'\0' * (16 * n)
        out += struct.pack('<Q', 4 * n)
        out += struct.pack(f'<{n}I', *[16 * i for i in range(n)])
        pad_to(out, 8)
        out += b'\0' * 8  # data size, patched below
        ds_rel = len(out)
        data = bytearray()
        offs = []
        for e in self.entries:
            if e.is_hash:
                offs.append(len(data))
                data += e.data.write(off + ds_rel + len(data))
            else:
                while (off + ds_rel + len(data)) % 8:
                    data.append(0)
                offs.append(len(data))
                data += struct.pack('<Q', len(e.data)) + e.data
        if self.entries and not self.entries[-1].is_hash:  # a trailing raw block is padded to 8 as well
            while (off + ds_rel + len(data)) % 8:
                data.append(0)
        struct.pack_into('<Q', out, ds_rel - 8, len(data))
        out += data
        pad_to(out, 4)
        # names: keep the stored table (offsets stay valid); append names that aren't in it
        names = bytearray(self.names)
        for e in self.entries:
            if not e.name:
                e.name_off = getattr(e, 'name_off', 0)
                continue
            if getattr(e, 'name_off', None) is None or names[e.name_off:e.name_off + len(e.name) + 1] != e.name + b'\0':
                i = bytes(names).find(e.name + b'\0')
                if i < 0:
                    i = len(names)
                    names += e.name + b'\0'
                e.name_off = i
        out += struct.pack('<I', len(names)) + names
        for i, e in enumerate(self.entries):
            struct.pack_into('<HHIII', out, eo + 16 * i, e.node, e.name_off, e.hash, e.next, offs[i])
        return bytes(out)

    # ---- editing helpers
    def get(self, name):
        for e in self.entries:
            if e.name == name:
                return e
        return None

    def rebuild_index(self):
        """Recompute hashes, buckets and chains after adding / renaming entries (named containers only)."""
        nb = len(self.buckets) - 1
        heads = [0xFFFFFFFF] * nb
        for i, e in enumerate(self.entries):
            e.hash = _hash(e.name)
        for i, e in enumerate(self.entries):  # like the game's files: the latest entry heads its bucket
            b = e.hash % nb
            e.next = heads[b]
            heads[b] = 16 * i
        self.buckets = heads + [self.buckets[-1]]


def same(a, b):
    """Structural equality (padding bytes in the originals are uninitialised memory, not data)."""
    if (a.magic, a.version, a.nodes, a.x6, a.buckets[:-1], a.names) != (b.magic, b.version, b.nodes, b.x6,
                                                                       b.buckets[:-1], b.names):
        return False
    if len(a.entries) != len(b.entries):
        return False
    for x, y in zip(a.entries, b.entries):
        if (x.node, x.name, x.hash, x.next, x.is_hash) != (y.node, y.name, y.hash, y.next, y.is_hash):
            return False
        if x.is_hash and not same(x.data, y.data):
            return False
        if not x.is_hash and x.data != y.data:
            return False
    return True


if __name__ == '__main__':
    exact = struct_ok = bad = 0
    for p in sys.argv[1:]:
        d = open(p, 'rb').read()
        w = FixedHash(d).write()
        if w == d:
            exact += 1
        elif len(w) == len(d) and same(FixedHash(d), FixedHash(w)):
            struct_ok += 1
        else:
            bad += 1
            print('DIFF', p, len(d), len(w))
    print(exact, 'byte-identical,', struct_ok, 'same structure (padding differs),', bad, 'different')
