#!/bin/bash
# make deps (macOS): the libraries and tools the shipped .app contains, built from pinned sources for the app's
# minimum macOS (MACOS_MIN, default 15.0) into build/deps. Homebrew's bottles target the macOS they were built on, so
# they can't ship. Plain `make` (dev builds) keeps using Homebrew.
#   SDL3; FFmpeg (LGPL only, no GPL parts: just what the game's movies need - WebM: VP9 video + Vorbis audio, plus
#   VP8 / Opus / H.264 / AAC in WebM or MP4 - and swscale); keystone (for the seed generator helper);
#   a relocatable CPython 3.12 (python-build-standalone, built for macOS 11+) that `make rando-helper` freezes.
# Needs: Xcode command line tools, cmake. Each step is skipped when its output exists (delete build/deps/<x> to redo).
set -euo pipefail
cd "$(dirname "$0")/.."
MIN=${MACOS_MIN:-15.0}
P=$PWD/build/deps
S=$P/src
mkdir -p "$S" "$P/bin" "$P/lib"
export MACOSX_DEPLOYMENT_TARGET=$MIN
JOBS=$(sysctl -n hw.ncpu)

fetch() {  # url sha256 file
    [ -f "$S/$3" ] || curl -fsSL -o "$S/$3.part" "$1" && { [ -f "$S/$3" ] || mv "$S/$3.part" "$S/$3"; }
    echo "$2  $S/$3" | shasum -a 256 -c --quiet
}
unpack() { rm -rf "$S/$2" && mkdir -p "$S/$2" && tar -xf "$S/$1" -C "$S/$2" --strip-components 1; }

fetch https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz \
      9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3 SDL3-3.4.18.tar.gz
fetch https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz \
      8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e ffmpeg-9.0.2.tar.xz
fetch https://github.com/keystone-engine/keystone/archive/refs/tags/0.9.2.tar.gz \
      c9b3a343ed3e05ee168d29daf89820aff9effb2c74c6803c2d9e21d55b5b7c24 keystone-0.9.2.tar.gz
fetch "https://github.com/astral-sh/python-build-standalone/releases/download/20260924/cpython-3.12.14%2B20260924-aarch64-apple-darwin-install_only.tar.gz" \
      9763f43db2481a6af36af82ec40302aab7a73632f880129d07a6e81aec846277 cpython-3.12.14-20260924.tar.gz

if [ ! -f "$P/lib/libSDL3.0.dylib" ]; then
    unpack SDL3-3.4.18.tar.gz sdl
    cmake -S "$S/sdl" -B "$S/sdl/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$P" \
          -DCMAKE_OSX_DEPLOYMENT_TARGET="$MIN" -DCMAKE_OSX_ARCHITECTURES=arm64 -DSDL_SHARED=ON -DSDL_STATIC=OFF \
          -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_INSTALL_DOCS=OFF
    cmake --build "$S/sdl/build" -j"$JOBS" && cmake --install "$S/sdl/build"
fi

if [ ! -f "$P/lib/libavcodec.dylib" ]; then
    unpack ffmpeg-9.0.2.tar.xz ffmpeg
    (cd "$S/ffmpeg" && ./configure --prefix="$P" --cc=clang --arch=arm64 --enable-shared --disable-static \
        --extra-cflags="-mmacosx-version-min=$MIN" --extra-ldflags="-mmacosx-version-min=$MIN" \
        --disable-autodetect --disable-programs --disable-doc --disable-network --disable-avdevice --disable-avfilter \
        --disable-swresample --disable-everything \
        --enable-protocol=file --enable-demuxer=matroska,mov \
        --enable-decoder=vp9,vp8,vorbis,opus,h264,aac --enable-parser=vp9,vp8,vorbis,opus,h264,aac \
        --install-name-dir='@rpath' && make -j"$JOBS" && make install)
fi

if [ ! -f "$P/lib/libkeystone.dylib" ]; then
    unpack keystone-0.9.2.tar.gz keystone
    sed -i '' 's/cmake_policy(SET CMP0051 OLD)//' "$S/keystone/CMakeLists.txt" "$S/keystone/llvm/CMakeLists.txt"  # (CMake 4)
    cmake -S "$S/keystone" -B "$S/keystone/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$P" \
          -DCMAKE_OSX_DEPLOYMENT_TARGET="$MIN" -DCMAKE_OSX_ARCHITECTURES=arm64 -DBUILD_SHARED_LIBS=ON \
          -DBUILD_LIBS_ONLY=ON -DLLVM_TARGETS_TO_BUILD="AArch64" -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
          -DCMAKE_CXX_FLAGS="-Wno-everything" -DCMAKE_C_FLAGS="-Wno-everything"
    cmake --build "$S/keystone/build" -j"$JOBS" && cmake --install "$S/keystone/build"
fi

if [ ! -x "$P/python/bin/python3.12" ]; then
    rm -rf "$P/python" && mkdir -p "$P/python"
    tar -xf "$S/cpython-3.12.14-20260924.tar.gz" -C "$P/python" --strip-components 1
fi

echo "build/deps ready (macOS $MIN):"
for f in "$P"/lib/*.dylib; do
    [ -L "$f" ] && continue
    printf '  %-28s minos %s\n' "$(basename "$f")" "$(otool -l "$f" | awk '/LC_BUILD_VERSION/ {b=1} b && /minos/ {print $2; exit}')"
done
