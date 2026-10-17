# roomswap.py <src level/room> <dst level/room> : copy a room into another dungeon's cell, translating positions
import sys, os, struct
sys.path.insert(0, 'third_party/LAS-Randomizer')
from RandomizerCore.Tools import leb
src, dst = sys.argv[1], sys.argv[2]
def cell(name):  # Lv01TailCave_04C -> (x0, z0)
    rc = name.rsplit('_', 1)[1][:3]
    return (ord(rc[2]) - ord('A')) * 15.0, (int(rc[:2]) - 1) * 12.0
sx, sz = cell(src); dx_, dz_ = cell(dst)
dx, dz = dx_ - sx, dz_ - sz
r = leb.Room(open(f'extracted/romfs/region_common/level/{src}.leb', 'rb').read())
for a in r.actors:
    a.posX += dx; a.posZ += dz
# the roof / black room-fade cover is per cell (it lifts while Link is in that room): use the destination's own
orig = leb.Room(open(f'extracted/romfs/region_common/level/{dst}.leb', 'rb').read())
roof = [a for a in orig.actors if a.type == 0x1b6]
if os.environ.get('NOROOF'):
    r.actors = [a for a in r.actors if a.type != 0x1b6]
if os.environ.get('ORIGROOF'):  # the destination cell's own roof actor, as is
    r.actors = [a for a in r.actors if a.type != 0x1b6] + roof
    roof = []
for a in r.actors:
    if a.type == 0x1b6 and roof:
        a.parameters = list(roof[0].parameters)
        print('roof', roof[0].parameters[:3])
# points (rails / paths): float3 position at the start of each 'point' entry
for e in r.fixed_hash.entries:
    if e.name == b'point':
        for p in e.data.entries:
            x, y, z = struct.unpack_from('<3f', p.data, 0)
            p.data = struct.pack('<3f', x + dx, y, z + dz) + p.data[12:]
out = f'rando/DevRoom/romfs/region_common/level/{dst}.leb'
os.makedirs(os.path.dirname(out), exist_ok=True)
open(out, 'wb').write(r.repack())
print('moved by', dx, dz, '->', out)

# textures: the destination level's bntx + what the moved room's model needs from its own dungeon
if len(sys.argv) > 3 and sys.argv[3] == 'tex':
    import re
    sys.path.insert(0, 'tools/rando')
    import bntx_merge
    slv, dlv = src.split('/')[0], dst.split('/')[0]
    model = [a.parameters[0] for a in r.actors if a.type == 0x185][0].decode()
    bf = open(f'extracted/romfs/region_common/map/{model}.bfres', 'rb').read()
    refs = {m.group().decode() for m in re.finditer(rb'MI_[A-Za-z0-9_.]+', bf)}
    base = f'rando/DevRoom/romfs/region_common/map/{dlv}.bntx'
    basedata = open(base, 'rb').read() if os.path.exists(base) else open(f'extracted/romfs/region_common/map/{dlv}.bntx', 'rb').read()
    out = bntx_merge.merge(basedata, [open(f'extracted/romfs/region_common/map/{slv}.bntx', 'rb').read()], only=refs)
    os.makedirs(os.path.dirname(base), exist_ok=True)
    open(base, 'wb').write(out)
    print('bntx', len(out), 'bytes; model', model, len(refs), 'refs')
