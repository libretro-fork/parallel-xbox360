#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
. "$root/tools/tests/spv_storage_source.sh"
out=${TEST_BUILD_DIR:-"$root/build/tests/spv-types-compare"}
mkdir -p "$out"
out=$(CDPATH= cd -- "$out" && pwd)
baseline=${BASELINE_ROOT:-}
if test -z "$baseline"; then
 mkdir -p "$out/base"
 git -C "$root" archive "${BASELINE_COMMIT:-3a5d2b181}" > "$out/base.tar"
 tar -xf "$out/base.tar" -C "$out/base"
 baseline="$out/base"
fi
baseline=$(CDPATH= cd -- "$baseline" && pwd)
san=${SANITIZER-}
for revision in parent current; do
 src="$root"
 if test "$revision" = parent; then src="$baseline"; fi
 obj="$out/$revision"
 mkdir -p "$obj"
 for unit in size binary operands arena; do
  unit_source="$src/libretro/core_spv_$unit.c"
  if test "$unit" = operands; then unit_source=$(spv_storage_source "$src"); fi
  ${CC:-cc} -std=c89 -pedantic -O2 $san -Wall -Wextra -Werror -I"$src/third_party/libretro-common/include" \
   -c "$unit_source" -o "$obj/$unit.o"
 done
 ${CXX:-c++} -std=c++20 -O2 -DNDEBUG $san -I"$src" -I"$src/src/xenia/gpu/spirv" -I"$src/third_party/libretro-common/include" \
  "$root/tools/tests/core_spv_types_bridge.cc" "$src/src/xenia/gpu/spirv/SpvBuilder.cpp" \
  "$src/src/xenia/gpu/spirv/SpvPostProcess.cpp" "$src/src/xenia/gpu/spirv/InReadableOrder.cpp" \
  "$src/src/xenia/gpu/spirv/Logger.cpp" "$obj/size.o" "$obj/binary.o" "$obj/operands.o" "$obj/arena.o" -o "$obj/test"
 for variant in 0 1; do "$obj/test" "$obj/types-$variant.bin" "$variant"; done
done
for variant in 0 1; do cmp "$out/parent/types-$variant.bin" "$out/current/types-$variant.bin"; done
echo 'PASS: ordinary/debug type bytes match map/vector baseline'
