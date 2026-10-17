# minibox.py <id> <params...>: Field_04P actor 7 -> mini-boss <id>, plus an AreaEventBox covering the screen wired
# to it (the mini-boss's <Name>Start fires when Link is inside the box, like in its dungeon room)
import sys, os, copy
sys.path.insert(0, 'third_party/LAS-Randomizer')
from RandomizerCore.Tools import leb
tid = int(sys.argv[1], 0)
params = [p.encode() if not p.isdigit() else int(p) for p in sys.argv[2:]]
r = leb.Room(open('rando/DevStory/romfs/region_common/level/Field/Field_04P.leb', 'rb').read())
src = leb.Room(open('extracted/romfs/region_common/level/Lv02BottleGrotto/Lv02BottleGrotto_04B.leb', 'rb').read())
box = copy.deepcopy(src.actors[6])
a = r.actors[7]
a.type = tid
a.parameters = (params + [b''] * 8)[:8]
a.relationships.e = 1
a.rotY = 0
a.switches = [(4, 0)] * 4
st = [x for x in r.actors if x.name.startswith(b'TagPlayerStart')][0]
a.posX, a.posZ = st.posX + 2.0, st.posZ - 2.0
box.posX, box.posY, box.posZ = st.posX, 0.0, st.posZ + 3.0
box.scaleX, box.scaleY, box.scaleZ = 4.0, 2.0, 2.0
r.actors.append(box)
bi = len(r.actors) - 1
box.relationships.section_3 = [7]
box.relationships.y = 1
a.relationships.section_1 = [[[b'', b''], bi]]
a.relationships.x = 1
a.relationships.section_3 = []
a.relationships.y = 0
out = 'rando/DevMini/romfs/region_common/level/Field/Field_04P.leb'
open(out, 'wb').write(r.repack())
print('ok', hex(tid), 'box', bi)
