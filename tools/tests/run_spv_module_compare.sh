#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/spv-module-compare-$(uname -s)"}
mkdir -p "$out/base"
baseline=$(git -C "$root" rev-parse --verify "${BASELINE_COMMIT:-7e088a41}^{commit}")
git -C "$root" archive "$baseline" src/xenia/gpu/spirv > "$out/base.tar"
tar -xf "$out/base.tar" -C "$out/base"
san=${SANITIZER-}
${CC:-cc} -std=c89 -O2 $san -I"$root/third_party/libretro-common/include" \
 -c "$root/libretro/core_spv_operands.c" -o "$out/storage.o"
for mode in baseline current; do
 src="$root/src/xenia/gpu/spirv"
 if test "$mode" = baseline; then src="$out/base/src/xenia/gpu/spirv"; fi
 ${CXX:-c++} -std=c++20 -O2 -DNDEBUG $san -I"$src" -I"$root" \
  -I"$root/third_party/libretro-common/include" \
  "$root/tools/tests/core_spv_module_bridge.cc" "$src/SpvBuilder.cpp" \
  "$src/SpvPostProcess.cpp" "$src/InReadableOrder.cpp" "$src/Logger.cpp" \
  "$out/storage.o" -o "$out/$mode.exe"
 for variant in 0 1 2 3 4; do
  "$out/$mode.exe" "$out/$mode-$variant.spv" "$variant"
 done
done
for variant in 0 1 2 3 4; do
 cmp "$out/baseline-$variant.spv" "$out/current-$variant.spv"
 if test -n "${SPIRV_VALIDATOR:-}"; then "$SPIRV_VALIDATOR" "$out/current-$variant.spv"; fi
done
echo "PASS: five production builder modules match baseline byte-for-byte"
