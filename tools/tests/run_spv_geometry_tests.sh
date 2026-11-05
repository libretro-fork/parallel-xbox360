#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
parent=${BASELINE_ROOT:?Set BASELINE_ROOT to a complete pre-change source archive}
parent=$(CDPATH= cd -- "$parent" && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/spv-geometry"}
mkdir -p "$out"
out=$(CDPATH= cd -- "$out" && pwd)
san=${SANITIZER-}
for unit in geometry vfs; do
 source="$root/tools/tests/core_${unit}_test.c"
 if test "$unit" = geometry; then source="$root/tools/tests/core_spv_geometry_test.c"; fi
 ${CC:-cc} -std=c89 -O2 -Wall -Wextra -Werror $san -I"$root/third_party/libretro-common/include" \
  "$source" -o "$out/$unit-test"
 "$out/$unit-test"
done
profile=
profile_flags=
if test "${PROFILE:-0}" = 1; then
 profile="-DXE_SPV_GEOMETRY_PROFILE -static-libstdc++ -static-libgcc"
 profile_flags="-include $root/tools/tests/core_pipeline_bench_profile.h -DXE_SPV_ALLOC=xe_bench_malloc -DXE_SPV_FREE=xe_bench_free -DXE_SPV_ARENA_ALLOC=xe_bench_malloc -DXE_SPV_ARENA_FREE=xe_bench_free -DXE_SPV_INDEX_REALLOC=xe_bench_realloc -DXE_SPV_INDEX_FREE=xe_bench_free -DXE_SPV_BINARY_REALLOC=xe_bench_realloc -DXE_SPV_BINARY_FREE=xe_bench_free"
fi
for revision in parent current; do
 src="$root"
 legacy=
 if test "$revision" = parent; then
  src="$parent"
  if test -z "$(sed -n '/^bool BuildGuestPrimitiveGeometryShaderSpirv(/p' "$src/src/xenia/gpu/spirv_builtin_geometry_shader.h")"; then
   legacy=-DXE_SPV_GEOMETRY_LEGACY
  fi
 fi
 obj="$out/$revision"
 mkdir -p "$obj/validation"
 for unit in size binary operands arena; do
  ${CC:-cc} -std=c89 -O2 $san $profile_flags -I"$src/third_party/libretro-common/include" -I"$root/tools/tests" -c "$src/libretro/core_spv_$unit.c" -o "$obj/$unit.o"
 done
 profile_object=
 if test "${PROFILE:-0}" = 1; then
  profile_object="$obj/profile.o"
  ${CC:-cc} -std=c89 -D_POSIX_C_SOURCE=200809L -O2 -c "$root/tools/tests/core_pipeline_bench_profile.c" -o "$profile_object"
 fi
 ${CXX:-c++} -std=c++20 -O2 -DNDEBUG -DFMT_HEADER_ONLY -DXENIA_LIBRETRO -DVULKAN_HPP_NO_TO_STRING -Wno-attributes $san $profile $profile_flags $legacy \
  -I"$src/src" -I"$src" -I"$src/third_party/libretro-common/include" -I"$src/third_party/fmt/include" -I"$src/third_party/Vulkan-Headers/include" \
  "$root/tools/tests/core_spv_geometry_bridge.cc" "$src/src/xenia/gpu/spirv_builtin_geometry_shader.cc" \
  "$src/src/xenia/gpu/spirv_builder.cc" "$src/src/xenia/gpu/spirv/SpvBuilder.cpp" \
  "$src/src/xenia/gpu/spirv/SpvPostProcess.cpp" "$src/src/xenia/gpu/spirv/InReadableOrder.cpp" "$src/src/xenia/gpu/spirv/Logger.cpp" \
  "$obj/size.o" "$obj/binary.o" "$obj/operands.o" "$obj/arena.o" $profile_object -o "$obj/test"
 if test "${PROFILE:-0}" = 1; then
  "$obj/test" "$obj/geometry.bin" bench
 else
  SPV_GEOMETRY_VALIDATION="$obj/validation" "$obj/test" "$obj/geometry.bin"
 fi
done
cmp "$out/parent/geometry.bin" "$out/current/geometry.bin"
if test -n "${SPIRV_VALIDATOR:-}" && test "${PROFILE:-0}" != 1; then
 for module in "$out/current/validation/"*.spv; do "$SPIRV_VALIDATOR" "$module"; done
fi
echo "PASS: geometry parent/current shader bytes match"
