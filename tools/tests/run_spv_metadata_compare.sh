#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/spv-metadata-compare"}
mkdir -p "$out"
out=$(CDPATH= cd -- "$out" && pwd)
baseline=${BASELINE_ROOT:-}
if test -z "$baseline"; then
 mkdir -p "$out/base"
 git -C "$root" archive "${BASELINE_COMMIT:-e8278d524}" > "$out/base.tar"
 tar -xf "$out/base.tar" -C "$out/base"
 baseline="$out/base"
fi
baseline=$(CDPATH= cd -- "$baseline" && pwd)
san=${SANITIZER-}
${CC:-cc} -std=c89 -pedantic -O2 $san -Wall -Wextra -Werror -c "$root/libretro/core_spv_probe.c" -o "$out/probe.o"
for revision in parent current; do
 src="$root"
 if test "$revision" = parent; then src="$baseline"; fi
 obj="$out/$revision"
 mkdir -p "$obj"
 for unit in size binary operands arena; do
  ${CC:-cc} -std=c89 -pedantic -O2 $san -Wall -Wextra -Werror -I"$src/third_party/libretro-common/include" \
   -c "$src/libretro/core_spv_$unit.c" -o "$obj/$unit.o"
 done
 ${CXX:-c++} -std=c++20 -O2 -DNDEBUG $san -I"$src" -I"$src/src/xenia/gpu/spirv" -I"$root" -I"$src/third_party/libretro-common/include" \
  "$root/tools/tests/core_spv_metadata_bridge.cc" "$src/src/xenia/gpu/spirv/SpvBuilder.cpp" \
  "$src/src/xenia/gpu/spirv/SpvPostProcess.cpp" "$src/src/xenia/gpu/spirv/InReadableOrder.cpp" \
  "$src/src/xenia/gpu/spirv/Logger.cpp" "$obj/size.o" "$obj/binary.o" "$obj/operands.o" "$obj/arena.o" "$out/probe.o" -o "$obj/test"
 for variant in 0 1 2 3; do "$obj/test" "$obj/metadata-$variant.bin" "$variant"; done
done
for variant in 0 1 2 3; do cmp "$out/parent/metadata-$variant.bin" "$out/current/metadata-$variant.bin"; done
if test -n "${SPIRV_VALIDATOR:-}"; then
 for minor in 0 1 2 3 4 5 6; do
  dd if="$out/current/metadata-3.bin" of="$out/probe-$minor.spv" bs=156 skip="$minor" count=1 2>/dev/null
  "$SPIRV_VALIDATOR" "$out/probe-$minor.spv"
 done
fi
echo 'PASS: capability/postprocess bytes match baseline and seven raw probes match the legacy builder'
