#!/usr/bin/env bash
# Freeze report: runs Sky Recomp with its diagnostics on (guest thread dumps every 3 s, button texture log), samples
# its memory and the GPU's every 5 s, and when you press Enter with the game frozen records what each thread is doing:
# its CPU use and, with gdb, a backtrace of every thread taken twice 3 s apart (a thread stuck in a loop shows in
# both, busy). Everything goes to ~/skyrecomp-freeze.txt.
#   tools/linux/freeze_report.sh [the AppImage or build/skyrecomp]
# gdb (sudo pacman -S gdb / sudo apt install gdb) attaches through sudo: a running game can't be traced otherwise.
set -u
app=${1:-$(ls -t ~/Applications/*.AppImage build/dist/*.AppImage 2>/dev/null | grep -i sky | head -1)}
if [ -z "$app" ] || [ ! -x "$app" ]; then echo "Pass the game's path: $0 ~/Applications/<Sky Recomp>.AppImage"; exit 1; fi
log=${LARECOMP_DATA_ROOT:-$HOME/.local/share/skyrecomp}/skyrecomp.log  # (stderr isn't a terminal: the game logs here)
out=$HOME/skyrecomp-freeze.txt
mem=$(mktemp)

LARECOMP_DUMP_THREADS=1 LARECOMP_TEXPATCH_LOG=1 "$app" >/dev/null 2>&1 &
pid=
for _ in $(seq 30); do pid=$(pgrep -n -x skyrecomp) && break; sleep 1; done
if [ -z "$pid" ]; then echo "The game didn't start."; exit 1; fi
echo "Game running (pid $pid). Play until it freezes, then come back here and press Enter."

vram() { nvidia-smi --query-gpu=memory.used,memory.total --format=csv,noheader 2>/dev/null | head -1; }
field() { awk -v k="$1:" '$1 == k {print $2 / 1024 " MB"}' "/proc/$pid/status" 2>/dev/null; }
(while kill -0 "$pid" 2>/dev/null; do
    echo "$(date +%T) rss $(field VmRSS) swap $(field VmSwap) vram $(vram)"
    sleep 5
done) >"$mem" &

while kill -0 "$pid" 2>/dev/null; do read -r -t 1 && break; done
running=0
if kill -0 "$pid" 2>/dev/null; then
    running=1
    command -v gdb >/dev/null && sudo -v
else
    echo "The game exited; recording its log."
fi
{
    echo "=== freeze report, $(date), kernel $(uname -r), $app"
    echo "=== memory every 5 s"; cat "$mem"
    if [ "$running" = 1 ]; then
        echo "=== now"; grep -E '^(VmRSS|VmSwap|Threads):' "/proc/$pid/status"; free -m; vram
        echo "=== CPU per thread"; top -H -b -n 1 -w 200 -p "$pid" | head -60
        if command -v gdb >/dev/null; then
            for i in 1 2; do
                echo "=== backtraces ($i of 2)"
                sudo gdb -p "$pid" -batch -ex 'set pagination off' -ex 'thread apply all bt 30' 2>&1 |
                    grep -vE '^\[New LWP|^warning:|^\[Thread debugging'
                sleep 3
            done
        else
            echo "=== threads (no gdb: name, state, kernel wait)"
            for t in /proc/"$pid"/task/*; do echo "$(cat "$t/comm") $(awk '{print $3}' "$t/stat") $(cat "$t/wchan")"; done
        fi
    fi
    echo "=== log: button textures"; grep '\[texpatch\]' "$log" | grep -v 'apply:' | head -600
    echo "=== log: last lines"; grep -v '\[vk\] present' "$log" | tail -500
} >"$out" 2>&1
rm -f "$mem"
echo "Wrote $out: send me that file. (The game is still running if it froze; close it when you like.)"
