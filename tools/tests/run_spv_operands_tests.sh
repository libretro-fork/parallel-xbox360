#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/spv-operands-$(uname -s)"}
mkdir -p "$out"
san=${SANITIZER--fsanitize=address,undefined}
${CC:-cc} -std=c89 -pedantic -O2 -g $san -Wall -Wextra -Werror -I"$root/third_party/libretro-common/include" "$root/tools/tests/core_spv_packet_test.c" -o "$out/test"
"$out/test"
${CC:-cc} -std=c89 -O2 -g $san -Wall -Wextra -Werror -I"$root/libretro" -I"$root/third_party/libretro-common/include" \
 -c "$root/libretro/core_spv_packet.c" -o "$out/plain.o"
${CC:-cc} -std=c89 -O2 -g $san -I"$root/third_party/libretro-common/include" -c "$root/libretro/core_spv_arena.c" -o "$out/arena.o"
${CXX:-c++} -std=c++20 -O2 -g $san -Wall -Wextra -Werror -I"$root" -I"$root/src" \
 -I"$root/third_party/libretro-common/include" "$root/tools/tests/core_spv_operands_bridge.cc" \
 "$out/plain.o" "$out/arena.o" -o "$out/bridge"
"$out/bridge"
