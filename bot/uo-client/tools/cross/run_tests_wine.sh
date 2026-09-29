#!/usr/bin/env bash
# Build the Windows client + every test with MinGW-w64 and run ctest under
# Wine. For Linux machines (cloud sessions, CI) without Visual Studio.
#   needs: cmake ninja g++-mingw-w64-x86-64-posix wine64
#   usage: tools/cross/run_tests_wine.sh [build-dir]
set -euo pipefail
here="$(cd "$(dirname "$0")/../.." && pwd)"
build="${1:-$here/build-mingw}"
export WINEDEBUG=-all WINEPREFIX="${WINEPREFIX:-$build/wineprefix}"
cmake -S "$here" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE="$here/tools/cross/mingw-w64.cmake"
ninja -C "$build"
wineboot -i >/dev/null 2>&1 || true
ctest --test-dir "$build" -j4 --timeout 240 --output-on-failure
