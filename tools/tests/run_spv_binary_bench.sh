#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/spv-binary-bench"}
mkdir -p "$out"
out=$(CDPATH= cd -- "$out" && pwd)
${CC:-cc} -std=c89 -O2 -I"$root/third_party/libretro-common/include" -c "$root/libretro/core_spv_size.c" -o "$out/size.o"
${CC:-cc} -std=c89 -O2 -c "$root/tools/tests/core_pipeline_bench_profile.c" -o "$out/profile.o"
${CC:-cc} -std=c89 -O2 -include "$root/tools/tests/core_pipeline_bench_profile.h" \
 -DXE_SPV_BINARY_REALLOC=xe_bench_realloc -DXE_SPV_BINARY_FREE=xe_bench_free \
 -c "$root/libretro/core_spv_binary.c" -o "$out/binary.o"
${CC:-cc} -std=c89 -O2 -I"$root/third_party/libretro-common/include" -c "$root/libretro/core_spv_operands.c" -o "$out/operands.o"
${CC:-cc} -std=c89 -O2 -I"$root/third_party/libretro-common/include" -c "$root/libretro/core_spv_arena.c" -o "$out/arena.o"
baseline=${BASELINE_READABLE_ORDER:-"$out/readable-parent.cpp"}
if test -z "${BASELINE_READABLE_ORDER:-}"; then
 git -C "$root" show "${BASELINE_COMMIT:-67a701ccf}:src/xenia/gpu/spirv/InReadableOrder.cpp" > "$baseline"
fi
for walker in parent current; do
 readable="$baseline"
 if test "$walker" = current; then readable="$root/src/xenia/gpu/spirv/InReadableOrder.cpp"; fi
 ${CXX:-c++} -std=c++20 -O2 -DNDEBUG -DXE_SPV_BINARY_CHECK -DXE_SPV_BYTE_BENCH \
  -include "$root/tools/tests/core_pipeline_bench_profile.h" \
  -DXE_SPV_WALK_ALLOC=xe_bench_malloc -DXE_SPV_WALK_FREE=xe_bench_free \
  -static-libstdc++ -static-libgcc -I"$root" -I"$root/src/xenia/gpu/spirv" -I"$root/third_party/libretro-common/include" \
  "$root/tools/tests/core_spv_module_bridge.cc" "$root/src/xenia/gpu/spirv/SpvBuilder.cpp" \
  "$root/src/xenia/gpu/spirv/SpvPostProcess.cpp" "$readable" \
  "$root/src/xenia/gpu/spirv/Logger.cpp" "$out/binary.o" "$out/operands.o" \
  "$out/arena.o" "$out/size.o" "$out/profile.o" -o "$out/test-$walker"
 echo "walker=$walker"
 for variant in 0 4 5; do
  for sample in 1 2 3 4 5; do
   "$out/test-$walker" "$out/$walker-module-$variant.spv" "$variant"
  done
 done
done
for variant in 0 4 5; do
 cmp "$out/parent-module-$variant.spv" "$out/current-module-$variant.spv"
done
