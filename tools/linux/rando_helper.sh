#!/bin/bash
# tools/linux/rando_helper.sh <x86_64|aarch64> (inside the build container of that arch, tools/linux/appimage.sh): the
# seed generator and the game code generator frozen by PyInstaller (tools/rando/rando_gen.spec, tools/lasgen.spec, as
# `make rando-helper` / `make gen-helper`) with Ubuntu 22.04's Python 3.12, into build/linux/<arch>/helper/rando-gen
# and helper/lasgen. On arm64 oead and keystone-engine have no wheels on PyPI:
# pip builds them from source (slow under QEMU; kept in build/linux/<arch>/wheels for the next run).
set -euo pipefail
cd "$(dirname "$0")/../.."
L=$PWD/build/linux/$1
V=$L/rando-venv
if [ ! -f "$V/.ok" ]; then
    rm -rf "$V" && python3.12 -m venv "$V"
    "$V/bin/python" -m pip install -q --upgrade pip
    "$V/bin/python" -m pip wheel -q -w "$L/wheels" --find-links "$L/wheels" \
        -r third_party/LAS-Randomizer/requirements.txt pyinstaller==6.22.3
    # keystone-engine's setup.py tags what it builds manylinux1_<arch>, which pip rejects off x86 (no manylinux1 there)
    for w in "$L/wheels"/keystone_engine-*-manylinux1_aarch64.whl; do
        [ -f "$w" ] || continue
        "$V/bin/python" -m pip install -q wheel
        "$V/bin/python" -m wheel tags --remove --platform-tag linux_aarch64 "$w" >/dev/null
    done
    "$V/bin/python" -m pip install -q --no-index --find-links "$L/wheels" \
        -r third_party/LAS-Randomizer/requirements.txt pyinstaller==6.22.3
    touch "$V/.ok"
fi
if [ ! -f "$V/.gen-ok" ]; then
    "$V/bin/python" -m pip install -q capstone==5.0.9 unicorn==2.1.4 lz4==4.4.5
    touch "$V/.gen-ok"
fi
"$V/bin/python" -m PyInstaller --noconfirm --clean --log-level WARN --distpath "$L/helper" --workpath "$L/pyinstaller" \
    tools/rando/rando_gen.spec
du -sh "$L/helper/rando-gen"
"$V/bin/python" -m PyInstaller --noconfirm --clean --log-level WARN --distpath "$L/helper" --workpath "$L/pyinstaller" \
    tools/lasgen.spec
du -sh "$L/helper/lasgen"
