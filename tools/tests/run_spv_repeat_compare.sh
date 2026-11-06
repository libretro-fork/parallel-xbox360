#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
. "$root/tools/tests/spv_storage_source.sh"
out=${TEST_BUILD_DIR:-"$root/build/tests/spv-repeat-compare"}
mkdir -p "$out"
out=$(CDPATH= cd -- "$out" && pwd)
baseline=${BASELINE_ROOT:-}
if test -z "$baseline"; then
 mkdir -p "$out/base"
 git -C "$root" archive "${BASELINE_COMMIT:-b40bab43d}" > "$out/base.tar"
 tar -xf "$out/base.tar" -C "$out/base"
 baseline="$out/base"
fi
baseline=$(CDPATH= cd -- "$baseline" && pwd)
san=${SANITIZER-}
fixture=${TEST_SOURCE:-"$root/tools/tests/core_spv_repeat_bridge.cc"}
profile_flags=; profile_object=
if test "${PROFILE:-0}" = 1; then
 profile_flags="-include $root/tools/tests/core_pipeline_bench_profile.h -DXE_SPV_ALLOC=xe_bench_malloc -DXE_SPV_FREE=xe_bench_free -DXE_SPV_ARENA_ALLOC=xe_bench_malloc -DXE_SPV_ARENA_FREE=xe_bench_free -DXE_SPV_INDEX_REALLOC=xe_bench_realloc -DXE_SPV_INDEX_FREE=xe_bench_free -DXE_SPV_BINARY_REALLOC=xe_bench_realloc -DXE_SPV_BINARY_FREE=xe_bench_free"
 profile_object="$out/profile.o"
 ${CC:-cc} -std=c89 -O2 -c "$root/tools/tests/core_pipeline_bench_profile.c" -o "$profile_object"
fi
for revision in parent current; do
 src="$root"; legacy=
 if test "$revision" = parent; then
  src="$baseline"
  if test "${BASELINE_REPEAT_API:-0}" != 1; then legacy=-DXE_SPV_REPEAT_LEGACY; fi
 fi
 obj="$out/$revision"; mkdir -p "$obj"
 for unit in size binary operands arena; do
  unit_source="$src/libretro/core_spv_$unit.c"
  if test "$unit" = operands; then unit_source=$(spv_storage_source "$src"); fi
  ${CC:-cc} -std=c89 -pedantic -O2 $san $profile_flags -Wall -Wextra -Werror -I"$src/third_party/libretro-common/include" -c "$unit_source" -o "$obj/$unit.o"
 done
 ${CXX:-c++} -std=c++20 -O2 -DNDEBUG -DFMT_HEADER_ONLY -static-libstdc++ -static-libgcc $san $legacy $profile_flags -I"$src" -I"$src/src" -I"$src/src/xenia/gpu/spirv" -I"$src/third_party/libretro-common/include" -I"$src/third_party/fmt/include" \
  "$fixture" "$src/src/xenia/gpu/spirv_builder.cc" "$src/src/xenia/gpu/spirv/SpvBuilder.cpp" "$src/src/xenia/gpu/spirv/SpvPostProcess.cpp" "$src/src/xenia/gpu/spirv/InReadableOrder.cpp" "$src/src/xenia/gpu/spirv/Logger.cpp" \
  "$obj/size.o" "$obj/binary.o" "$obj/operands.o" "$obj/arena.o" $profile_object -o "$obj/test"
 for variant in 0 1 2 3; do "$obj/test" "$obj/module-$variant.spv" "$variant"; done
done
for variant in 0 1 2 3; do cmp "$out/parent/module-$variant.spv" "$out/current/module-$variant.spv"; done
if test -n "${SPIRV_VALIDATOR:-}"; then "$SPIRV_VALIDATOR" "$out/current/module-0.spv"; fi
echo 'PASS: repeated sources/extract/compare bytes match parent including replicated and cooperative modes'
