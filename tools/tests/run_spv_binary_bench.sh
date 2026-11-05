#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/spv-binary-bench"}
mkdir -p "$out"
out=$(CDPATH= cd -- "$out" && pwd)
# A baseline must be a complete source snapshot, so IR layouts and traversal
# interfaces come from the same revision. Never mix old objects/new headers.
revisions=current
if test -n "${BASELINE_ROOT:-}"; then revisions="parent current"; fi
for revision in $revisions; do
 src="$root"
 if test "$revision" = parent; then src=$(CDPATH= cd -- "$BASELINE_ROOT" && pwd); fi
 obj="$out/$revision"
 mkdir -p "$obj"
 ${CC:-cc} -std=c89 -O2 -I"$src/third_party/libretro-common/include" -c "$src/libretro/core_spv_size.c" -o "$obj/size.o"
 ${CC:-cc} -std=c89 -O2 -c "$src/tools/tests/core_pipeline_bench_profile.c" -o "$obj/profile.o"
 ${CC:-cc} -std=c89 -O2 -include "$src/tools/tests/core_pipeline_bench_profile.h" \
  -DXE_SPV_BINARY_REALLOC=xe_bench_realloc -DXE_SPV_BINARY_FREE=xe_bench_free \
  -c "$src/libretro/core_spv_binary.c" -o "$obj/binary.o"
 ${CC:-cc} -std=c89 -O2 -I"$src/third_party/libretro-common/include" -c "$src/libretro/core_spv_operands.c" -o "$obj/operands.o"
 ${CC:-cc} -std=c89 -O2 -include "$src/tools/tests/core_pipeline_bench_profile.h" -DXE_SPV_INDEX_REALLOC=xe_bench_realloc -DXE_SPV_INDEX_FREE=xe_bench_free -I"$src/third_party/libretro-common/include" -c "$src/libretro/core_spv_arena.c" -o "$obj/arena.o"
 ${CXX:-c++} -std=c++20 -O2 -DNDEBUG -DXE_SPV_BINARY_CHECK -DXE_SPV_BYTE_BENCH \
  -include "$src/tools/tests/core_pipeline_bench_profile.h" \
  -DXE_SPV_WALK_ALLOC=xe_bench_malloc -DXE_SPV_WALK_FREE=xe_bench_free \
  -DXE_SPV_PLAN_ALLOC=xe_bench_malloc -DXE_SPV_PLAN_FREE=xe_bench_free \
  -DXE_SPV_INDEX_REALLOC=xe_bench_realloc -DXE_SPV_INDEX_FREE=xe_bench_free \
  -static-libstdc++ -static-libgcc -I"$src" -I"$src/src/xenia/gpu/spirv" -I"$src/third_party/libretro-common/include" \
  "$src/tools/tests/core_spv_module_bridge.cc" "$src/src/xenia/gpu/spirv/SpvBuilder.cpp" \
  "$src/src/xenia/gpu/spirv/SpvPostProcess.cpp" "$src/src/xenia/gpu/spirv/InReadableOrder.cpp" \
  "$src/src/xenia/gpu/spirv/Logger.cpp" "$obj/binary.o" "$obj/operands.o" \
  "$obj/arena.o" "$obj/size.o" "$obj/profile.o" -o "$obj/test"
 echo "revision=$revision"
 for variant in 0 4 5; do
  for sample in 1 2 3 4 5; do
   "$obj/test" "$obj/module-$variant.spv" "$variant"
  done
 done
done
if test -n "${BASELINE_ROOT:-}"; then
 for variant in 0 4 5; do cmp "$out/parent/module-$variant.spv" "$out/current/module-$variant.spv"; done
fi
