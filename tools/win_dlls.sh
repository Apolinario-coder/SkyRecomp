#!/bin/bash
# usage: tools/win_dlls.sh <exe> <dir>...: the DLLs the exe imports, directly or through other DLLs, that are in one of
# the dirs (searched in order), one path per line, for `make dist`. Others are Windows' own (System32) or vulkan-1.dll,
# which every Vulkan driver installs. Read from the import tables (objdump), unlike ldd without starting the exe, so it
# works on machines without a GPU driver (CI).
set -euo pipefail
exe=$1; shift
todo=("$exe")
seen=" vulkan-1.dll "
while [ ${#todo[@]} -gt 0 ]; do
    f=${todo[0]}; todo=("${todo[@]:1}")
    for n in $(${OBJDUMP:-objdump} -p "$f" | sed -n 's/^[[:space:]]*DLL Name: //p' | tr -d '\r'); do
        lc=$(printf '%s' "$n" | tr '[:upper:]' '[:lower:]')
        case $seen in *" $lc "*) continue ;; esac
        seen="$seen$lc "
        for d in "$@"; do
            if [ -f "$d/$n" ]; then echo "$d/$n"; todo+=("$d/$n"); break; fi
        done
    done
done
