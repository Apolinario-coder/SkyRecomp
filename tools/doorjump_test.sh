#!/bin/bash
# (Link is warped to the dungeon's entrance (ENTRANCE overrides), then placed at the doorway: a cell's centre can be a pit)
# Walk every mismatched doorway of a generated-dungeon seed (rando/<seed>/door_jumps.tsv) and check Link ends up in
# the neighbouring room: tools/doorjump_test.sh <seed> [first line] [count]. One game run per doorway (warp into the
# cell, LARECOMP_DOORJUMP_TEST starts after the dungeon title card (Link is held while it shows: jumps undone), puts Link 1.2 inside the doorway and pushes the stick outward). Fresh scratch saves only.
# Results: build/doorjump_<seed>.txt (+ a screenshot per doorway in build/doorjump_<seed>/).
SEED=$1; FIRST=${2:-1}; COUNT=${3:-999}
TSV=rando/$SEED/door_jumps.tsv
JOB=${DJ_JOB:-$SEED}  # DJ_JOB=<name>: several jobs of one seed in parallel (own saves, shots, results)
OUT=build/doorjump_$JOB.txt; SHOTS=build/doorjump_$JOB
rm -rf "$SHOTS"; mkdir -p "$SHOTS"; : > "$OUT"
n=0
while IFS=$'\t' read -r level cell side own partner; do
    n=$((n + 1))
    [ $n -lt "$FIRST" ] && continue
    [ $n -ge $((FIRST + COUNT)) ] && break
    case $side in W) stick=STICK_LEFT;; E) stick=STICK_RIGHT;; N) stick=STICK_UP;; *) stick=STICK_DOWN;; esac  # (unused)
    case $level in Lv02*|Lv10*) ent=08C;; Lv03*) ent=08B;; Lv05*) ent=08H;; *) ent=08D;; esac  # dungeon_gen ENTRANCES
    tools/fresh_save.sh "djtest_$JOB" >/dev/null 2>&1  # (per seed: suites of different seeds run in parallel)
    LARECOMP_DOORJUMP_TEST="3100:$cell:$side" LARECOMP_CHEATS=hearts LARECOMP_RANDO_SEED=$SEED \
    LARECOMP_SAVE_DIR="build/djtest_$JOB" LARECOMP_WINDOW=640x360 SHOT_ID="dj_$JOB" \
    LARECOMP_INPUT="400:A:6,520:A:6,900:ZL:8,900:ZR:8,1300:A:6,1600:A:6,1900:A:6,2200:A:6" \
    LARECOMP_WARP_AT="2700:$level:${level}_${ENTRANCE:-$ent}" tools/shot.sh 4000 600 </dev/null 2>/dev/null
    cp "build/window_dj_$JOB.png" "$SHOTS/$cell$side.png"
    res=$(grep '\[doorjump-test\].*->' "build/run_shot_dj_$JOB.log" | tail -1)
    [ -z "$res" ] && res="[doorjump-test] $cell $side -> no result (crash? $(grep -c '\*\*\* signal' "build/run_shot_dj_$JOB.log") signals)"
    echo "$res" | tee -a "$OUT"
done < "$TSV"
# contact sheet: every doorway's last frame (Link must be visible in the room he walked into)
python3 - "$SHOTS" <<'PY'
import os, sys
from PIL import Image, ImageDraw
d = sys.argv[1]
fs = sorted(f for f in os.listdir(d) if f.endswith('.png') and f != 'sheet.png')
if fs:
    w, h, cols = 480, 270, 3
    sheet = Image.new('RGB', (w * cols, h * ((len(fs) + cols - 1) // cols)))
    for i, f in enumerate(fs):
        im = Image.open(os.path.join(d, f)).resize((w, h))
        ImageDraw.Draw(im).text((8, 30), f[:-4], fill=(255, 255, 0))
        sheet.paste(im, ((i % cols) * w, (i // cols) * h))
    sheet.save(os.path.join(d, 'sheet.png'))
PY
echo "$(grep -c PASS "$OUT") pass / $(grep -c -v PASS "$OUT") other -> $OUT"
