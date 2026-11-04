#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/file-span-$(uname -s)"}
mkdir -p "$out"
san=${SANITIZER--fsanitize=address,undefined}
${CC:-cc} -std=c89 -O2 -g $san -Wall -Wextra -Werror -I"$root/libretro" \
 -I"$root/third_party/libretro-common/include" "$root/tools/tests/core_file_span_test.c" -o "$out/test"
"$out/test"
${CXX:-c++} -std=c++20 -O2 -g $san -Wall -Wextra -Werror -Wno-unused-parameter \
 -I"$root" -I"$root/src" -I"$root/third_party/libretro-common/include" \
 "$root/tools/tests/core_file_span_bridge.cc" -o "$out/bridge"
"$out/bridge"
