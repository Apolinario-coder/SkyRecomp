#!/bin/bash
# make smoketest (tools/linux/smoketest.sh [AppImage]): release check of the x86-64 AppImage on this machine, in four
# launches of the AppImage itself (default build/dist/LAS Recomp-<version>-linux-x86_64.AppImage):
#   0. setup, as the start screen's Set up does it: this checkout's extracted/ (exefs, romfs) into an empty data folder,
#      and the game code built from it with the bundled generator and compiler (runtime/gamebuild.c)
#   1. start screen: rendered with the game files found ("Game files ready")
#   2. one game session from a test save: into the game, then warps around the overworld and to every dungeon's
#      entrance, a screenshot after each; the log must show each level loaded and no crash, the screenshots not black
#   3. randomizer: in game, generate a seed from the menu (the bundled helper), switch to it live (Play Now), load the
#      save on it until Link is in a level; then close and delete the seed
# Result: build/smoke/report.txt (PASS / FAIL per check), build/smoke/sheet.png (every screenshot, labelled) and the
# screenshots in build/smoke/shots/. Exit status 0 = ready for release.
# Uses this checkout's extracted/ (as the folder a player picks) and a test save that is never in git: $LARECOMP_SMOKE_SAVE (a save folder with
# save_0/, default ~/.local/share/lasrecomp-smoketest/save). Make one by starting a new game once (any build, data
# folder's save/) and copying that save/ there. Nothing in ~/.local/share/lasrecomp is used or changed.
set -uo pipefail
cd "$(dirname "$0")/../.."
REPO=$PWD
VERSION=$(sed -n 's/^#define LARECOMP_VERSION "\(.*\)"/\1/p' runtime/ui/launcher.mm)
APP=$(realpath "${1:-build/dist/LAS Recomp-$VERSION-linux-x86_64.AppImage}")
SAVE=${LARECOMP_SMOKE_SAVE:-$HOME/.local/share/lasrecomp-smoketest/save}
O=$REPO/build/smoke
[ -x "$APP" ] || { echo "smoketest: no AppImage at $APP (make appimage)"; exit 2; }
[ -d "$SAVE/save_0" ] || { echo "smoketest: no test save in $SAVE (see the top of this script)"; exit 2; }
[ -d extracted/romfs ] || { echo "smoketest: no extracted/ game files in this checkout"; exit 2; }

rm -rf "$O" && mkdir -p "$O/root/build" "$O/shots"
cp -R "$SAVE" "$O/save"
# test runs: no update check, no sound, a data folder of their own (runtime/paths.h), the AppImage run without FUSE
export LARECOMP_DATA_ROOT=$O/root LARECOMP_SAVE_DIR=$O/save LARECOMP_SETTINGS=$O/root/larecomp.ini \
       LARECOMP_NO_UPDATE_CHECK=1 LARECOMP_NO_AUDIO=1 APPIMAGE_EXTRACT_AND_RUN=1
FAILS=0
report() {  # PASS|FAIL, text
    printf '%-4s  %s\n' "$1" "$2" | tee -a "$O/report.txt"
    [ "$1" = PASS ] || FAILS=$((FAILS + 1))
}
dark() { python3 -I -c "
import sys
from PIL import Image, ImageStat
print(1 if sum(ImageStat.Stat(Image.open(sys.argv[1]).convert('L')).mean) < 12 else 0)" "$1"; }

# The AppImage runtime starts the game as a child process: each run gets a process group of its own (setsid), so
# stopping it stops the game too - nothing it uses (the seed) is deleted while it still runs.
start_app() {  # VAR=value...: the AppImage in the data folder, with those settings
    (cd "$O/root" && exec env "$@" setsid "$APP" </dev/null >/dev/null 2>&1) &
    APP_PG=$!
}
app_alive() { ps -e -o pgid=,stat= | awk -v g="$APP_PG" '$1 == g && $2 !~ /^Z/ {f = 1} END {exit !f}'; }
stop_app() {
    kill -TERM -- -"$APP_PG" 2>/dev/null
    for i in $(seq 1 20); do app_alive || break; sleep 0.5; done
    app_alive && kill -KILL -- -"$APP_PG" 2>/dev/null
    while app_alive; do sleep 0.5; done
    wait "$APP_PG" 2>/dev/null
}
wait_app() {  # seconds: status 0 = the game ended by itself in time, else it is stopped
    for i in $(seq 1 "$1"); do app_alive || { wait "$APP_PG" 2>/dev/null; return 0; }; sleep 1; done
    stop_app
    return 1
}

# ---- 0. setup (headless: LARECOMP_LAUNCHER_EXTRACT runs the start screen's Set up, logs the result and exits)
t0=$(date +%s)
start_app LARECOMP_LAUNCHER_EXTRACT="$REPO/extracted"
wait_app 1800
mv "$O/root/lasrecomp.log" "$O/setup.log" 2>/dev/null
lib=$(ls "$O/root/extracted"/lasgame.* 2>/dev/null | head -1)
if grep -q '\[launcher\] setup: .* -> READY' "$O/setup.log" && [ -n "$lib" ]; then
    report PASS "setup: game files and game code built ($(( $(date +%s) - t0 )) s, $(basename "$lib") $(du -m "$lib" | cut -f1) MB)"
else
    report FAIL "setup: $(grep -m1 '\[launcher\] setup:' "$O/setup.log" || echo 'no result') (see build/smoke/setup.log)"
fi

# ---- 1. start screen
# (no terminal: the app logs to lasrecomp.log in its data folder, runtime/platform/linux.c)
start_app LARECOMP_LAUNCHER_SHOT=150 LARECOMP_SHOT_PATH="$O/shots/00_start_screen.png"
wait_app 90
mv "$O/root/lasrecomp.log" "$O/start_screen.log" 2>/dev/null
if [ -f "$O/shots/00_start_screen.png" ] && [ "$(dark "$O/shots/00_start_screen.png")" = 0 ] &&
   ! grep -qE '\*\*\* |FATAL' "$O/start_screen.log"; then
    report PASS "start screen ($(grep -o 'using .*' "$O/start_screen.log" | head -1))"
else
    report FAIL "start screen (see build/smoke/start_screen.log)"
fi

# ---- 2. + 3. game session: load the save (L+R at the title, A through the file menu), first warp 60 frames after
# the game is playable, the rest at fixed frames after that, a screenshot WAIT frames after each
WARPS=(
    "Mabe Village:Field:Field_11C"
    "Goponga Swamp:Field:Field_03E"
    "Face Shrine (outside):Field:Field_07M"
    "Tal Tal Mountains:Field:Field_01O"
    "L1 Tail Cave:Lv01TailCave:Lv01TailCave_08D"
    "L2 Bottle Grotto:Lv02BottleGrotto:Lv02BottleGrotto_08C"
    "L3 Key Cavern:Lv03KeyCavern:Lv03KeyCavern_08B"
    "L4 Angler's Tunnel:Lv04AnglersTunnel:Lv04AnglersTunnel_08D"
    "L5 Catfish's Maw:Lv05CatfishsMaw:Lv05CatfishsMaw_08H"
    "L6 Face Shrine:Lv06FaceShrine:Lv06FaceShrine_08D"
    "L7 Eagle's Tower:Lv07EagleTower:Lv07EagleTower_08B_b"
    "L8 Turtle Rock:Lv08TurtleRock:Lv08TurtleRock_03C"
    "Wind Fish's Egg:Lv09WindFishsEgg:Lv09WindFishsEgg_08D"
    "Color Dungeon:Lv10ClothesDungeon:Lv10ClothesDungeon_08C"
)
START=7000 STEP=700 WAIT=500  # (the save is in play by ~frame 5500; a level loads in well under WAIT frames)
IN=$(for f in $(seq 600 100 6000); do printf '%d:L:12,%d:R:12,%d:A:6,' $f $f $((f + 50)); done)
AT= SHOTS=
for i in "${!WARPS[@]}"; do
    IFS=: read -r _ lvl loc <<< "${WARPS[$i]}"
    f=$((START + i * STEP))
    [ "$i" -gt 0 ] && AT+="$f:$lvl:$loc,"
    SHOTS+="$((f + WAIT)),"
done
IFS=: read -r _ lvl0 loc0 <<< "${WARPS[0]}"
LAST=$((START + (${#WARPS[@]} - 1) * STEP + WAIT + 60))
start_app LARECOMP_NO_LAUNCHER=1 LARECOMP_INPUT="${IN%,}" LARECOMP_WARP_ON_LOAD="$lvl0:$loc0" \
    LARECOMP_WARP_AT="${AT%,}" LARECOMP_SHOT_AT="${SHOTS%,}" LARECOMP_WINDOW_SHOT=$LAST LARECOMP_EXIT_AFTER_SHOT=1
wait_app 600
rc=$?
mv "$O/root/lasrecomp.log" "$O/game.log" 2>/dev/null
L=$O/game.log
if grep -q '\[warp\] jumping to ' "$L"; then report PASS "into the game (save loaded)"
else report FAIL "into the game: the save never got in play (see build/smoke/game.log)"; fi
for i in "${!WARPS[@]}"; do
    IFS=: read -r name lvl loc <<< "${WARPS[$i]}"
    f=$(( i == 0 ? 0 : START + i * STEP ))
    shot=$(printf '%s/root/build/frames/frame_%05d.png' "$O" $((START + i * STEP + WAIT)))
    out=$(printf '%s/shots/%02d_%s.png' "$O" $((i + 1)) "$(echo "$name" | tr -c 'A-Za-z0-9\n' '_')")
    [ -f "$shot" ] && cp "$shot" "$out"
    # loaded = the renderer's periodic line names the level after the jump to it
    if ! grep -q "\[warp\] jumping to $lvl / $loc" "$L"; then report FAIL "$name: warp not taken"
    elif ! sed -n "/\[warp\] jumping to $lvl \/ $loc/,\$p" "$L" | grep -c " level $lvl " >/dev/null; then report FAIL "$name: $lvl never loaded"
    elif [ ! -f "$out" ]; then report FAIL "$name: no screenshot"
    elif [ "$(dark "$out")" = 1 ]; then report FAIL "$name: screenshot is black"
    else report PASS "$name ($lvl / $loc)"; fi
done
if grep -qE '\*\*\* |FATAL' "$L"; then report FAIL "crash: $(grep -m1 -E '\*\*\* |FATAL' "$L")"
elif [ $rc -ne 0 ]; then report FAIL "game session didn't end by itself (stopped after 10 min)"
else report PASS "no crash, clean exit"; fi
fps=$(grep -oE '[0-9.]+ fps' "$L" | awk '{s += $1; n++; if ($1 < m || !m) m = $1} END {if (n) printf "%.1f avg, %.1f min", s / n, m}')
echo "frame rate (2 s samples): $fps" | tee -a "$O/report.txt"

# ---- 4. randomizer: in game, the menu's Randomizer tab generates a seed (the shipped rando-gen helper) and Play Now
# switches to it live (in-place reboot); the save is loaded again on the seed. The session ends once Link is in a
# level on the seed (log), then the seed is deleted. Input keeps loading the save from whatever title screen shows.
# (LARECOMP_RANDO_GEN_SHOTS is what presses Play Now; its menu screenshots are Metal-only, so none here.)
SEED=SmokeSeed
rm -rf "$O/root/build/frames" "$O/root/build"/frame_*.png
rm -rf "$O/save" && cp -R "$SAVE" "$O/save"  # (the warps above saved in the dungeons: start from the house again)
IN=$(for f in $(seq 600 100 40000); do printf '%d:L:12,%d:R:12,%d:A:6,' $f $f $((f + 50)); done)
start_app LARECOMP_NO_LAUNCHER=1 LARECOMP_INPUT="${IN%,}" LARECOMP_MENU_AT=7000 LARECOMP_MENU_TAB=3 \
    LARECOMP_RANDO_GEN_TEST=$SEED LARECOMP_RANDO_GEN_SHOTS="$O/shots/17_seed" LARECOMP_SCREENSHOT_EVERY=120
L=$O/root/lasrecomp.log
in_seed() {  # after the switch: the seed active, then the renderer reports a level (not the title screen, "-")
    sed -n "/\[rando\] seed \"$SEED\" active/,\$p" "$L" 2>/dev/null | grep -cE ' level [A-Za-z0-9_]+ \(' >/dev/null
}
for i in $(seq 1 300); do  # (10 minutes: generation takes about a minute)
    app_alive || break
    if in_seed; then sleep 5; break; fi
    sleep 2
done
last=$(ls "$O/root/build"/frame_*.png 2>/dev/null | tail -1)
[ -n "$last" ] && cp "$last" "$O/shots/19_seed_in_game.png"
stop_app  # (before the seed is deleted: the game reads its files whenever a level loads)
mv "$L" "$O/seed.log" 2>/dev/null
L=$O/seed.log
cp "$O/root/rando/$SEED/generate.log" "$O/seed_generate.log" 2>/dev/null
# generated = the seed's patches load after the switch (a failed generation leaves them missing: "[rando] no ...")
if grep -qE '\[rando\] [0-9]+ code words' "$L" && ! grep -q '\[rando\] no ' "$L"; then
    report PASS "seed generated ($(cat "$O/root/rando/$SEED/seed_id.txt" 2>/dev/null): $(grep -m1 -oE '[0-9]+ code words, [0-9]+ data patches' "$L"))"
else report FAIL "seed generation (see build/smoke/seed_generate.log, seed.log)"; fi
if grep -q "\[reboot\] requested (boot)" "$L" && grep -q "\[rando\] seed \"$SEED\" active" "$L"; then
    report PASS "switched to the seed live (in-place reboot)"
else report FAIL "live switch to the seed (see build/smoke/seed.log)"; fi
lvl=$(sed -n "/\[rando\] seed \"$SEED\" active/,\$p" "$L" | grep -oE ' level [A-Za-z0-9_]+ \(' | head -1 | awk '{print $2}')
if [ -n "$lvl" ] && [ -f "$O/shots/19_seed_in_game.png" ] && [ "$(dark "$O/shots/19_seed_in_game.png")" = 0 ]; then
    report PASS "in game on the seed ($lvl)"
else report FAIL "in game on the seed (see build/smoke/seed.log)"; fi
if grep -qE '\*\*\* |FATAL' "$L"; then report FAIL "crash in the seed session: $(grep -m1 -E '\*\*\* |FATAL' "$L")"; fi
rm -rf "$O/root/rando/$SEED"
if [ ! -e "$O/root/rando/$SEED" ]; then report PASS "seed deleted"; else report FAIL "seed not deleted"; fi

python3 -I - "$O/sheet.png" "$O"/shots/*.png <<'PY'
import os, sys
from PIL import Image, ImageDraw
out, fs = sys.argv[1], sys.argv[2:]
w, h, cols = 400, 225, 4
rows = (len(fs) + cols - 1) // cols
m = Image.new('RGB', (w * cols, h * rows))
for i, f in enumerate(fs):
    im = Image.open(f).convert('RGB').resize((w, h))
    d = ImageDraw.Draw(im)
    label = os.path.basename(f)[3:-4].replace('_', ' ')
    d.rectangle((0, 0, 8 + 7 * len(label), 16), fill=(0, 0, 0))
    d.text((4, 2), label, fill=(255, 230, 80))
    m.paste(im, ((i % cols) * w, (i // cols) * h))
m.save(out)
PY
if [ $FAILS -eq 0 ]; then echo "READY FOR RELEASE: every check passed" | tee -a "$O/report.txt"
else echo "NOT READY: $FAILS check(s) failed" | tee -a "$O/report.txt"; fi
[ $FAILS -eq 0 ]
