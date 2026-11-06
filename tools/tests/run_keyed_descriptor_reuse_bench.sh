#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/keyed-descriptor-bench-$(uname -s)"}
mkdir -p "$out"
cd "$root"
${CXX:-c++} -std=c++20 -O3 -DNDEBUG -Ilibretro -Ithird_party/libretro-common/include \
 tools/tests/keyed_descriptor_reuse_bench.cc -o "$out/bench"
"$out/bench"
