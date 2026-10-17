#!/usr/bin/env python3
"""Cosmetics for a seed: Link's tunic colour.

The green tunic is texture MI_body_alb.0 of actor/Playerlink.bfres (BC1 sRGB 1024x1024, 11 mips); .1 / .2 are the
red / blue tunics (gameplay, left alone). Where .0 and .1 differ is exactly the cloth (tunic, cap, collar ring), so
that difference is the mask: inside it the green pixels get a hue / saturation / value change, keeping the
original shading; everything else (skin, belt, boots) is untouched.

    apply(seed_dir, romfs, seed, choice)   choice: 'default' | 'random' | one of COLOURS
    python3 cosmetics.py <romfs> <out.png> <colour>   preview the recoloured texture

Runs in the LAS venv (bntx_editor, quicktex, Pillow).

No longer offered by the start screen (seeds are made with 'default'): the in-game Cosmetics tab recolours the tunic
live with the same recolour (runtime/nvn/texpatch.c), over a seed's own colour too. Kept for older seeds' requests.
"""
import os
import random
import struct
import sys
from io import BytesIO

from PIL import Image, ImageChops, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'third_party', 'LAS-Randomizer'))

GREEN_HUE = 100  # PIL hue (0-255) of the green tunic
# name: (hue 0-255 or None = keep, saturation factor, value factor)
COLOURS = {
    'Purple': (195, 1.0, 1.1), 'Orange': (20, 1.2, 1.75), 'Yellow': (40, 1.05, 2.15), 'Pink': (232, 0.75, 1.5),
    'Teal': (125, 1.0, 1.0), 'Sky': (140, 0.8, 1.45), 'Brown': (16, 0.75, 0.8), 'Black': (None, 0.15, 0.45),
    'White': (None, 0.06, 2.1), 'Grey': (None, 0.0, 1.1), 'Lime': (70, 1.0, 1.4), 'Magenta': (215, 1.0, 1.15),
}
BODY = 'MI_body_alb.0'
RED = 'MI_body_alb.1'


def _clamp(x):
    return 0 if x < 0 else 255 if x > 255 else int(x)


def recolour(green, red, colour):
    """green / red: RGB images of the tunic textures -> recoloured green tunic (RGB)."""
    hue, sf, vf = COLOURS[colour]
    diff = ImageChops.difference(green, red).convert('L')
    mask = diff.point(lambda d: _clamp((d - 12) * 6)).filter(ImageFilter.GaussianBlur(1.2))
    h, s, v = green.convert('HSV').split()
    if hue is not None:
        dh = hue - GREEN_HUE
        h = h.point(lambda x: (x + dh) % 256)
    s = s.point(lambda x: _clamp(x * sf))
    v = v.point(lambda x: _clamp(x * vf))
    out = Image.merge('HSV', (h, s, v)).convert('RGB')
    return Image.composite(out, green, mask)


def _textures(bntx_data):
    from RandomizerCore.Tools.bntx_editor import bntx_editor
    e = bntx_editor.BNTXEditor()
    e.openFile(bntx_data)
    idx = {t.name: i for i, t in enumerate(e.bntx.textures)}
    img = lambda n: Image.open(BytesIO(e.exportTexByIndex(idx[n]))).convert('RGB')
    return e, idx, img(BODY), img(RED)


def _dds(image):
    import quicktex.dds as qdds
    import quicktex.s3tc.bc1 as bc1
    d = qdds.encode(image, bc1.BC1Encoder(10), 'DXT1')  # full mip chain (11 levels for 1024)
    out = BytesIO()
    out.write(b'DDS ')
    out.write(struct.pack('<7I44x', 124, int(d.flags), d.size[1], d.size[0], d.pitch, d.depth, d.mipmap_count))
    out.write(struct.pack('<2I4s5I', 32, int(d.pf_flags), bytes(d.four_cc, 'ascii'), d.pixel_size, *d.pixel_bitmasks))
    out.write(struct.pack('<4I4x', *d.caps))
    for t in d.textures:
        out.write(t)
    return out.getvalue()


def apply(seed_dir, romfs, seed, choice='random'):
    """Writes <seed_dir>/romfs/region_common/actor/Playerlink.bfres; returns the colour name (or None).
    Only the texture's pixel bytes change: the new image is swizzled exactly like the original (same format, size,
    mips, tiling) and written over the old data inside the embedded BNTX; headers stay byte-identical (re-saving
    the BNTX would change its size and the bfres embeds it at a fixed place)."""
    if not choice or choice == 'default':
        return None
    colour = random.Random(f'{seed}-tunic').choice(sorted(COLOURS)) if choice == 'random' else choice
    if colour not in COLOURS:
        raise ValueError(f'unknown tunic colour {colour!r}')
    src = os.path.join(romfs, 'region_common', 'actor', 'Playerlink.bfres')
    dst = os.path.join(seed_dir, 'romfs', 'region_common', 'actor', 'Playerlink.bfres')
    data = bytearray(open(src, 'rb').read())
    base = data.find(b'BNTX\0\0\0\0')
    if base < 0:
        raise RuntimeError('no BNTX in Playerlink.bfres')
    size = struct.unpack_from('<I', data, base + 0x1C)[0]
    e, idx, green, red = _textures(bytes(data[base:base + size]))
    tex = e.bntx.textures[idx[BODY]]
    fmt, size_, mips = tex.format_, tex.imageSize, list(tex.mipOffsets)  # replace() reuses the texture object
    off = base + struct.unpack_from('<Q', data, base + tex.ptrsAddr)[0]
    assert bytes(data[off:off + size_]) == bytes(tex.data), 'texture data not where expected'
    new = e.bntx.replace(tex, tex.tileMode, True, False, False, True, BytesIO(_dds(recolour(green, red, colour))))
    if not new or new.format_ != fmt or len(new.data) != size_ or list(new.mipOffsets) != mips:
        raise RuntimeError('recoloured tunic does not match the original layout')
    data[off:off + size_] = new.data
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with open(dst, 'wb') as f:
        f.write(data)
    return colour


if __name__ == '__main__':
    romfs, out, colour = sys.argv[1:4]
    from RandomizerCore.Tools.bntx_editor import bfres
    b = bfres.File()
    b.readFromFile(os.path.join(romfs, 'region_common', 'actor', 'Playerlink.bfres'))
    b.extractMainBNTX(out + '.bntx')
    _, _, g, r = _textures(open(out + '.bntx', 'rb').read())
    os.remove(out + '.bntx')
    recolour(g, r, colour).save(out)
