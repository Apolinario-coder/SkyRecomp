#!/usr/bin/env python3
"""Minimal MSBT (MsgStdBn) reader/writer for Link's Awakening (UTF-8 text, version 3): labels (LBL1), attributes
(ATR1, kept as is) and strings (TXT2, raw bytes incl. control tags). Enough to add / edit messages in a seed.

    m = Msbt(open(path, 'rb').read()); m.texts['SmallKey'] -> bytes; m.set('Label', b'text'); m.pack() -> bytes
    python3 msbt.py <file> [filter]   list labels and texts
"""
import struct
import sys


def _hash(label, slots):
    h = 0
    for c in label.encode():
        h = (h * 0x492 + c) & 0xFFFFFFFF
    return h % slots


class Msbt:
    def __init__(self, data):
        assert data[:8] == b'MsgStdBn', 'not an msbt'
        self.bom = data[8:10]
        self.enc = data[0xC]
        self.version = data[0xD]
        nsec = struct.unpack_from('<H', data, 0xE)[0]
        self.sections = []  # (magic, payload) in order
        off = 0x20
        for _ in range(nsec):
            magic = data[off:off + 4]
            size = struct.unpack_from('<I', data, off + 4)[0]
            self.sections.append((magic, data[off + 0x10:off + 0x10 + size]))
            off = (off + 0x10 + size + 15) & ~15
        sec = dict(self.sections)
        # labels -> index
        lbl = sec[b'LBL1']
        self.slots = struct.unpack_from('<I', lbl, 0)[0]
        self.labels = {}
        for i in range(self.slots):
            n, o = struct.unpack_from('<II', lbl, 4 + i * 8)
            for _ in range(n):
                ln = lbl[o]
                name = lbl[o + 1:o + 1 + ln].decode()
                idx = struct.unpack_from('<I', lbl, o + 1 + ln)[0]
                self.labels[name] = idx
                o += 1 + ln + 4
        txt = sec[b'TXT2']
        cnt = struct.unpack_from('<I', txt, 0)[0]
        offs = list(struct.unpack_from(f'<{cnt}I', txt, 4)) + [len(txt)]
        self.strings = [txt[offs[i]:offs[i + 1]] for i in range(cnt)]
        self.atr = sec.get(b'ATR1')

    @property
    def texts(self):
        return {name: self.strings[i] for name, i in self.labels.items()}

    def set(self, label, raw):
        """raw: bytes including the terminating NUL (UTF-8 text with the game's control tags)."""
        if not raw.endswith(b'\0'):
            raw += b'\0'
        if label in self.labels:
            self.strings[self.labels[label]] = raw
            return
        self.labels[label] = len(self.strings)
        self.strings.append(raw)
        if self.atr is not None:  # one attribute record per string: copy the last one's size of zeros
            n, size = struct.unpack_from('<II', self.atr, 0)
            self.atr = struct.pack('<II', n + 1, size) + self.atr[8:8 + n * size] + b'\0' * size + self.atr[8 + n * size:]

    def pack(self):
        # LBL1
        buckets = [[] for _ in range(self.slots)]
        for name, idx in sorted(self.labels.items(), key=lambda x: x[1]):
            buckets[_hash(name, self.slots)].append((name, idx))
        head = struct.pack('<I', self.slots)
        body = b''
        base = 4 + self.slots * 8
        for b in buckets:
            head += struct.pack('<II', len(b), base + len(body))
            for name, idx in b:
                nb = name.encode()
                body += bytes([len(nb)]) + nb + struct.pack('<I', idx)
        lbl = head + body
        # TXT2
        n = len(self.strings)
        offs, o = [], 4 + 4 * n
        for s in self.strings:
            offs.append(o)
            o += len(s)
        txt = struct.pack(f'<I{n}I', n, *offs) + b''.join(self.strings)
        out_secs = []
        for magic, payload in self.sections:
            if magic == b'LBL1':
                payload = lbl
            elif magic == b'TXT2':
                payload = txt
            elif magic == b'ATR1' and self.atr is not None:
                payload = self.atr
            out_secs.append((magic, payload))
        body = b''
        for magic, payload in out_secs:
            body += magic + struct.pack('<I', len(payload)) + b'\0' * 8 + payload
            body += b'\xab' * ((16 - len(body) % 16) % 16)
        hdr = b'MsgStdBn' + self.bom + b'\0\0' + bytes([self.enc, self.version]) + struct.pack('<H', len(out_secs))
        hdr += b'\0\0' + struct.pack('<I', 0x20 + len(body)) + b'\0' * 10
        return hdr + body


def readable(raw):
    """Text with control tags shown as <tag>."""
    out, i = '', 0
    while i < len(raw):
        if raw[i] == 0x0E and i + 6 <= len(raw):  # tag: 0E group(2) type(2) size(2) params
            size = struct.unpack_from('<H', raw, i + 5)[0] if False else raw[i + 5] | (raw[i + 6] << 8) if i + 7 <= len(raw) else 0
            out += '<tag>'
            i += 7 + size
            continue
        if raw[i] == 0:
            break
        j = i
        while j < len(raw) and raw[j] not in (0, 0x0E):
            j += 1
        out += raw[i:j].decode('utf-8', 'replace')
        i = j
    return out


if __name__ == '__main__':
    m = Msbt(open(sys.argv[1], 'rb').read())
    f = sys.argv[2] if len(sys.argv) > 2 else ''
    for name, raw in sorted(m.texts.items()):
        t = readable(raw)
        if f.lower() in (name + t).lower():
            print(f'{name:40} {t!r}')
