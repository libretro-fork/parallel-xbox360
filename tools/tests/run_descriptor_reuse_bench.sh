#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/descriptor-reuse-bench-$(uname -s)"}
mkdir -p "$out"
${CXX:-c++} -std=c++20 -O3 -DNDEBUG -I"$root/libretro" -I"$root/third_party/libretro-common/include" \
 "$root/tools/tests/core_descriptor_reuse_bench.cc" -o "$out/bench"
"$out/bench"
