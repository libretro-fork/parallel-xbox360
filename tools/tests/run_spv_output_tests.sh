#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/spv-output-$(uname -s)"}
mkdir -p "$out/base"
out=$(CDPATH= cd -- "$out" && pwd)
if test -n "${BASELINE_ARCHIVE:-}"; then
 cp "$BASELINE_ARCHIVE" "$out/base.tar"
else
 baseline=$(git -C "$root" rev-parse --verify "${BASELINE_COMMIT:-27b870d25}^{commit}")
git -C "$root" archive "$baseline" src/xenia/gpu/spirv src/xenia/gpu/spirv_shader_translator.cc src/xenia/gpu/spirv_shader_translator_rb.cc src/xenia/gpu/spirv_builder.h src/xenia/gpu/spirv_builder.cc src/xenia/gpu/edram_dump_shader.h src/xenia/gpu/edram_dump_shader.cc src/xenia/gpu/edram_transfer_shader.h src/xenia/gpu/edram_transfer_shader.cc > "$out/base.tar"
fi
tar -xf "$out/base.tar" -C "$out/base"
san=${SANITIZER-}
${CC:-cc} -std=c89 -pedantic -O2 $san -Wall -Wextra -Werror -I"$root/third_party/libretro-common/include" "$root/tools/tests/core_spv_words_test.c" -o "$out/words-unit"
"$out/words-unit"
${CC:-cc} -std=c89 -pedantic -O2 $san -Wall -Wextra -Werror "$root/tools/tests/core_spv_binary_test.c" -o "$out/unit"
"$out/unit"
${CC:-cc} -std=c89 -pedantic -O2 $san -Wall -Wextra -Werror -I"$root/third_party/libretro-common/include" "$root/tools/tests/core_spv_size_test.c" -o "$out/size-unit"
"$out/size-unit"
${CC:-cc} -std=c89 -pedantic -O2 $san -Wall -Wextra -Werror -I"$root/third_party/libretro-common/include" "$root/tools/tests/core_spv_walk_test.c" -o "$out/walk-unit"
"$out/walk-unit"
${CC:-cc} -std=c89 -pedantic -O2 $san -Wall -Wextra -Werror -I"$root/third_party/libretro-common/include" -c "$root/libretro/core_spv_size.c" -o "$out/size.o"
${CC:-cc} -std=c89 -pedantic -O2 $san -Wall -Wextra -Werror -c "$root/libretro/core_spv_binary.c" -o "$out/binary.o"
${CC:-cc} -std=c89 -O2 $san -I"$root/third_party/libretro-common/include" -c "$root/libretro/core_spv_operands.c" -o "$out/operands.o"
profile=
profile_object=
if test "${CAPTURE_ALLOCATIONS:-0}" = 1; then
 # Interpose matching new/delete even for out-of-line standard-library strings.
 # The Windows shared C++ runtime cannot use the executable's allocator override.
 profile="-DXE_SPV_OUTPUT_ALLOCATIONS -static-libstdc++ -static-libgcc"
 profile_object="$out/profile.o"
 ${CC:-cc} -std=c89 -D_POSIX_C_SOURCE=200809L -O2 -c "$root/tools/tests/core_pipeline_bench_profile.c" -o "$profile_object"
fi
arena_flags=
if test "${CAPTURE_ALLOCATIONS:-0}" = 1; then
 arena_flags="-include $root/tools/tests/core_pipeline_bench_profile.h -DXE_SPV_ARENA_ALLOC=xe_bench_malloc -DXE_SPV_ARENA_FREE=xe_bench_free"
fi
${CC:-cc} -std=c89 -pedantic -O2 $san -Wall -Wextra -Werror -I"$root/third_party/libretro-common/include" $arena_flags -c "$root/libretro/core_spv_arena.c" -o "$out/arena.o"
${CC:-cc} -std=c89 -pedantic -O2 $san -Wall -Wextra -Werror -I"$root/third_party/libretro-common/include" "$root/tools/tests/core_spv_arena_test.c" -o "$out/arena-unit"
"$out/arena-unit"
for mode in baseline current; do
 src="$root/src"
 legacy=
 legacy_views=
 arena_test=-DXE_SPV_ARENA_TEST
 if test "$mode" = baseline; then
  src="$out/base/src"
  legacy_views=-DXE_SPV_OUTPUT_LEGACY
  arena_test=
  # Accept the older vector-output baseline as well as reusable-output commits.
  if test -z "$(sed -n '/^bool BuildEdramDumpShaderSpirv(/p' "$src/xenia/gpu/edram_dump_shader.h")"; then legacy=-DXE_SPV_OUTPUT_LEGACY; fi
 fi
# Isolate the seven production static emitters needed by the helpers, without
# linking the unrelated translator's configuration registrars and Vulkan device.
# Each mode reads its own source, without mocked emitter bodies.
{
 echo '#include "xenia/gpu/spirv_shader_translator.h"'
 echo '#include "xenia/gpu/spirv_compatibility.h"'
 echo '#include "xenia/gpu/spirv/GLSL.std.450.h"'
 echo 'namespace xe { namespace gpu {'
 awk '/^spv::Id SpirvShaderTranslator::(PreClampedDepthTo20e4|UnclampedFloat32To7e3|PreClampedFloat32To7e3|Float7e3To32|Depth20e4To32|PWLGammaToLinear|LinearToPWLGamma)\(/ { emit=1; count++ } emit { print } emit && /^}/ { emit=0 } END { if (count != 7) exit 1 }' "$src/xenia/gpu/spirv_shader_translator.cc" "$src/xenia/gpu/spirv_shader_translator_rb.cc"
 echo '} }'
} > "$out/static_emitters-$mode.cc"
 ${CXX:-c++} -std=c++20 -O2 -DNDEBUG -DXENIA_LIBRETRO=1 -DVULKAN_HPP_NO_TO_STRING -Wno-attributes $san $legacy $profile $arena_test -I"$src" -I"$root/src" -I"$root" \
  -I"$root/third_party/libretro-common/include" -I"$root/third_party/fmt/include" \
  -I"$root/third_party" -I"$root/third_party/Vulkan-Headers/include" \
  "$root/tools/tests/core_spv_edram_output_test.cc" \
  "$out/static_emitters-$mode.cc" \
  "$src/xenia/gpu/edram_dump_shader.cc" "$src/xenia/gpu/edram_transfer_shader.cc" \
  "$src/xenia/gpu/spirv_builder.cc" "$src/xenia/gpu/spirv/SpvBuilder.cpp" \
  "$src/xenia/gpu/spirv/SpvPostProcess.cpp" "$src/xenia/gpu/spirv/InReadableOrder.cpp" \
  "$src/xenia/gpu/spirv/Logger.cpp" "$out/binary.o" "$out/operands.o" "$out/arena.o" "$out/size.o" $profile_object -o "$out/$mode"
 "$out/$mode" "$out/$mode.bin"
 "$out/$mode" "$out/$mode-wide.bin" wide
 ${CXX:-c++} -std=c++20 -O2 -DNDEBUG $san $legacy_views -I"$src" -I"$root/src" -I"$root" \
  -I"$root/third_party/libretro-common/include" \
  "$root/tools/tests/core_spv_views_bridge.cc" "$src/xenia/gpu/spirv_builder.cc" \
  "$src/xenia/gpu/spirv/SpvBuilder.cpp" "$src/xenia/gpu/spirv/SpvPostProcess.cpp" \
  "$src/xenia/gpu/spirv/InReadableOrder.cpp" "$src/xenia/gpu/spirv/Logger.cpp" \
  "$out/binary.o" "$out/operands.o" "$out/arena.o" "$out/size.o" $profile_object -o "$out/views-$mode"
 for variant in 0 1 2 3 4 5 6 7; do
  "$out/views-$mode" "$out/views-$mode-$variant.spv" "$variant"
 done
done
cmp "$out/baseline.bin" "$out/current.bin"
cmp "$out/baseline-wide.bin" "$out/current-wide.bin"
for variant in 0 1 2 3 4 5 6 7; do
 cmp "$out/views-baseline-$variant.spv" "$out/views-current-$variant.spv"
 # Older validators do not know the replicated-composites extension (4..7).
 if test "$variant" -lt 4 && test -n "${SPIRV_VALIDATOR:-}"; then
  "$SPIRV_VALIDATOR" "$out/views-current-$variant.spv"
 fi
done
${CC:-cc} -std=c89 -O2 $san -include "$root/tools/tests/core_spv_arena_test_alloc.h" -c "$root/libretro/core_spv_binary.c" -o "$out/binary-fault.o"
${CC:-cc} -std=c89 -pedantic -O2 $san -Wall -Wextra -Werror -c "$root/tools/tests/core_spv_arena_test_alloc.c" -o "$out/fault.o"
${CXX:-c++} -std=c++20 -O2 -DNDEBUG -DXE_SPV_BINARY_CHECK $san \
 -I"$root/src/xenia/gpu/spirv" -I"$root" -I"$root/third_party/libretro-common/include" \
 "$root/tools/tests/core_spv_module_bridge.cc" \
 "$root/src/xenia/gpu/spirv/SpvBuilder.cpp" "$root/src/xenia/gpu/spirv/SpvPostProcess.cpp" \
 "$root/src/xenia/gpu/spirv/InReadableOrder.cpp" "$root/src/xenia/gpu/spirv/Logger.cpp" \
 "$out/binary-fault.o" "$out/operands.o" "$out/arena.o" "$out/size.o" "$out/fault.o" $profile_object -o "$out/debug-bridge"
for variant in 0 1 2 3 4; do
 "$out/debug-bridge" "$out/debug-$variant.spv" "$variant"
 if test -n "${SPIRV_VALIDATOR:-}"; then "$SPIRV_VALIDATOR" "$out/debug-$variant.spv"; fi
done
echo "PASS: parent/current EDRAM output byte-for-byte, stable high-water storage"
echo "PASS: eight archived-vector/borrowed-view modules, overwritten inputs and nested access chains"

# Fault-injected operand ownership is isolated from profiling adapters.
${CC:-cc} -std=c89 -O2 $san -Wall -Wextra -Werror -I"$root/third_party/libretro-common/include" -include "$root/tools/tests/core_spv_arena_test_alloc.h" -c "$root/libretro/core_spv_arena.c" -o "$out/arena-plain.o"
${CC:-cc} -std=c89 -O2 $san -I"$root/third_party/libretro-common/include" -include "$root/tools/tests/core_spv_arena_test_alloc.h" -c "$root/libretro/core_spv_operands.c" -o "$out/operands-fault.o"
${CXX:-c++} -std=c++20 -O2 -DNDEBUG $san -I"$root" -I"$root/src" -I"$root/third_party/libretro-common/include" \
 "$root/tools/tests/core_spv_arena_bridge.cc" "$root/src/xenia/gpu/spirv/SpvBuilder.cpp" "$root/src/xenia/gpu/spirv/SpvPostProcess.cpp" \
 "$root/src/xenia/gpu/spirv/InReadableOrder.cpp" "$root/src/xenia/gpu/spirv/Logger.cpp" "$out/binary.o" "$out/arena-plain.o" "$out/size.o" "$out/operands-fault.o" "$out/fault.o" -o "$out/arena-bridge"
"$out/arena-bridge"
case "$san" in *address*)
 if "$out/arena-unit" uaf > "$out/uaf-c.log" 2>&1; then echo "missed C arena UAF"; exit 1; fi
 grep -q 'use-after-poison' "$out/uaf-c.log"
 if "$out/arena-bridge" uaf > "$out/uaf-cpp.log" 2>&1; then echo "missed typed arena UAF"; exit 1; fi
 grep -q 'use-after-poison' "$out/uaf-cpp.log"
 echo "PASS: ASan catches retired C and typed instruction access"
 ;; esac
