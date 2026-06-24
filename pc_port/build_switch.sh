#!/usr/bin/env bash
set -euo pipefail
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export PATH="$PATH:$DEVKITPRO/devkitA64/bin:$DEVKITPRO/tools/bin"
cmake -B build_switch \
  -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Switch.cmake" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build_switch -j"$(nproc)"
