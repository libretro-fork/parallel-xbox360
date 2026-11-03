#!/bin/bash
# Direct static Make build with MinGW-w64 GCC; no SDK or shader compiler downloads.
set -euo pipefail
BUILD_DIR=${BUILD_DIR:-build/windows-x86_64}
NUMPROC=${NUMPROC:-$(nproc)}
[ "$NUMPROC" -gt 0 ] || NUMPROC=1
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y --no-install-recommends \
  ca-certificates git make gcc g++ gcc-mingw-w64-x86-64-posix \
  g++-mingw-w64-x86-64-posix binutils-mingw-w64-x86-64 file
make -j "$NUMPROC" platform=windows-x86_64 BUILD_DIR="$BUILD_DIR" \
  CC=x86_64-w64-mingw32-gcc-posix CXX=x86_64-w64-mingw32-g++-posix \
  AR=x86_64-w64-mingw32-ar HOST_CC=gcc
file "$BUILD_DIR/parallel_xbox360_libretro.dll"
