#!/bin/bash
# Frame-series capture: tools/frames.sh <from frame> <count> [every]  -- runs ./build/lasrecomp with the caller's
# LARECOMP_* env and saves every Nth frame from <from> into build/frames/ (cleared first), then stops.
FROM=$1; N=$2; E=${3:-3}
rm -rf build/frames
. "$(dirname "$0")/game_slot.sh"; trap 'rm -rf "$GAME_SLOT"' EXIT  # (waits for 10 GB free)
LARECOMP_NO_AUDIO=${LARECOMP_NO_AUDIO:-1} LARECOMP_SCREENSHOT_EVERY=$E LARECOMP_SCREENSHOT_FROM=$FROM ./build/lasrecomp extracted > build/run_shot.log 2>&1 &
P=$!
for i in $(seq 1 240); do sleep 1; n=$(ls build/frames 2>/dev/null | wc -l); [ "$n" -ge "$N" ] && break; kill -0 $P 2>/dev/null || break; done
kill $P 2>/dev/null; ls build/frames | wc -l
