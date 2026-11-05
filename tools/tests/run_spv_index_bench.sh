#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/spv-index-bench"}
mkdir -p "$out"
out=$(CDPATH= cd -- "$out" && pwd)
revisions=current
if test -n "${BASELINE_ROOT:-}"; then revisions="parent current"; fi
for revision in $revisions; do
 src="$root"
 if test "$revision" = parent; then src=$(CDPATH= cd -- "$BASELINE_ROOT" && pwd); fi
 obj="$out/$revision"
 mkdir -p "$obj"
 ${CC:-cc} -std=c89 -O2 -c "$root/tools/tests/core_pipeline_bench_profile.c" -o "$obj/profile.o"
 ${CC:-cc} -std=c89 -O2 -include "$root/tools/tests/core_pipeline_bench_profile.h" \
  -DXE_SPV_ARENA_ALLOC=xe_bench_malloc -DXE_SPV_ARENA_FREE=xe_bench_free \
  -DXE_SPV_INDEX_REALLOC=xe_bench_realloc -DXE_SPV_INDEX_FREE=xe_bench_free \
  -I"$src/third_party/libretro-common/include" -c "$src/libretro/core_spv_arena.c" -o "$obj/arena.o"
 ${CC:-cc} -std=c89 -O2 -I"$src/third_party/libretro-common/include" -c "$src/libretro/core_spv_operands.c" -o "$obj/operands.o"
 ${CXX:-c++} -std=c++20 -O2 -DNDEBUG \
  -include "$root/tools/tests/core_pipeline_bench_profile.h" \
  -DXE_SPV_INDEX_REALLOC=xe_bench_realloc -DXE_SPV_INDEX_FREE=xe_bench_free \
  -static-libstdc++ -static-libgcc -I"$src" -I"$src/src/xenia/gpu/spirv" -I"$src/third_party/libretro-common/include" \
  "$root/tools/tests/core_spv_index_bench.cc" "$obj/arena.o" "$obj/operands.o" "$obj/profile.o" -o "$obj/test"
 echo "revision=$revision"
 "$obj/test"
done
