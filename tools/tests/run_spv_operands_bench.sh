#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/spv-operand-bench-$(uname -s)"}
mkdir -p "$out/base/xenia/gpu/spirv"
git -C "$root" show "${BASELINE_COMMIT:-7e088a41}:src/xenia/gpu/spirv/spvIR.h" > "$out/base/xenia/gpu/spirv/spvIR.h"
profile="$root/tools/tests/core_pipeline_bench_profile.h"
common="$root/third_party/libretro-common/include"
${CC:-cc} -std=c89 -O3 -c "$root/tools/tests/core_pipeline_bench_profile.c" -o "$out/profile.o"
for capture in tracked plain; do
 cflags=
 cppflags=
 if test "$capture" = tracked; then
  cflags="-DXE_SPV_ALLOC=xe_bench_malloc -DXE_SPV_FREE=xe_bench_free"
 else cppflags=-DXE_SPV_BENCH_PLAIN; fi
 ${CC:-cc} -std=c89 -O3 -I"$common" -include "$profile" $cflags \
  -c "$root/libretro/core_spv_operands.c" -o "$out/storage-$capture.o"
 for mode in baseline current; do
  src="$root/src"
  if test "$mode" = baseline; then src="$out/base"; fi
  ${CXX:-c++} -std=c++20 -O3 -DNDEBUG $cppflags -I"$src" -I"$root" -I"$root/src" \
   -I"$root/src/xenia/gpu/spirv" -I"$root/tools/tests" -I"$common" \
   "$root/tools/tests/core_spv_operands_bench.cc" "$out/profile.o" "$out/storage-$capture.o" \
   -o "$out/$mode-$capture.exe"
 done
done
if test "${BUILD_ONLY:-0}" != 1; then
 for capture in tracked plain; do
  "$out/baseline-$capture.exe" "baseline-$capture"
  "$out/current-$capture.exe" "current-$capture"
 done
fi
