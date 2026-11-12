#!/bin/sh
# C89 fixture build; optional hardware timing, never part of the core build.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${TRANSFER_WARM_OUT:-build/tests/transfer-uint-warm}
mkdir -p "$out"
out=$(CDPATH= cd -- "$out" && pwd)
# Raw samples are immutable evidence. Use a new output directory for each run.
test ! -e "$out/forward.csv" && test ! -e "$out/reverse.csv"
head=${TRANSFER_WARM_HEAD_SOURCE:-$root}
head=$(CDPATH= cd -- "$head" && pwd)
test -f "$head/libretro/core_transfer_uint_vk.c"
parent=${TRANSFER_WARM_PARENT_SOURCE:-}
if test -n "$parent"; then
 parent=$(CDPATH= cd -- "$parent" && pwd)
 test -f "$parent/libretro/core_transfer_uint_vk.c"
 gcc -std=c89 -pedantic-errors -Wno-long-long -O2 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared -I"$parent" -I"$root/tools/tests" -isystem "$parent/third_party/Vulkan-Headers/include" -isystem "$parent/third_party/SPIRV-Headers/include" "$root/tools/tests/transfer_uint_revision.c" "$parent/libretro/core_transfer_uint_vk.c" "$parent/libretro/core_transfer_uint_spv.c" "$parent/libretro/core_spv_binary.c" -o "$out/parent.so"
 gcc -std=c89 -pedantic-errors -Wno-long-long -O2 -Wall -Wextra -Werror -Itools/tests -isystem third_party/Vulkan-Headers/include tools/tests/transfer_uint_revision_test.c -ldl -o "$out/revision-abi-test"
 "$out/revision-abi-test" "$out/parent.so"
 export TRANSFER_PARENT_LIBRARY="$out/parent.so"
else
 unset TRANSFER_PARENT_LIBRARY
{
 echo '#include "xenia/gpu/spirv_shader_translator.h"'
 echo '#include "xenia/gpu/spirv_compatibility.h"'
 echo '#include "xenia/gpu/spirv/GLSL.std.450.h"'
 echo 'namespace xe { namespace gpu {'
 awk '/^spv::Id SpirvShaderTranslator::(PreClampedDepthTo20e4|UnclampedFloat32To7e3|PreClampedFloat32To7e3|Float7e3To32|Depth20e4To32|PWLGammaToLinear|LinearToPWLGamma)\(/ { emit=1; count++ } emit { print } emit && /^}/ { emit=0 } END { if (count != 7) exit 1 }' src/xenia/gpu/spirv_shader_translator.cc src/xenia/gpu/spirv_shader_translator_rb.cc
 echo '} }'
} > "$out/reference-static-emitters.cc"
for name in binary packet arena size; do
 gcc -std=c89 -pedantic-errors -Wno-long-long -O2 -Wall -Wextra -Werror -I. -Ithird_party/libretro-common/include -c libretro/core_spv_$name.c -o "$out/legacy-$name.o"
done
gcc -std=c89 -pedantic-errors -Wno-long-long -O2 -Wall -Wextra -Werror -I. -Itools/tests -Ithird_party/libretro-common/include -c "$root/tools/tests/transfer_uint_reference_fixture.c" -o "$out/reference-driver.o"
g++ -std=c++20 -O2 -DNDEBUG -DXENIA_LIBRETRO=1 -DVULKAN_HPP_NO_TO_STRING -Wno-attributes -Isrc -I. -Itools/tests -Ithird_party/libretro-common/include -Ithird_party/fmt/include -Ithird_party -Ithird_party/Vulkan-Headers/include "$root/tools/tests/transfer_uint_reference_bridge.cc" "$out/reference-static-emitters.cc" src/xenia/gpu/edram_transfer_shader.cc src/xenia/gpu/spirv_builder.cc src/xenia/gpu/spirv/SpvBuilder.cpp src/xenia/gpu/spirv/InReadableOrder.cpp src/xenia/gpu/spirv/Logger.cpp "$out/reference-driver.o" "$out/legacy-binary.o" "$out/legacy-packet.o" "$out/legacy-arena.o" "$out/legacy-size.o" -o "$out/legacy-generator"
mkdir -p "$out/legacy-modules"
"$out/legacy-generator" "$out/legacy-modules/legacy"
for module in "$out"/legacy-modules/*.spv; do spirv-val --target-env vulkan1.0 "$module"; done
fi
flags='-std=c89 -pedantic-errors -Wno-long-long -O2 -Wall -Wextra -Werror'
gcc $flags -I. -Ithird_party/libretro-common/include -isystem third_party/SPIRV-Headers/include tools/tests/transfer_uint_warm_spv_fixture.c libretro/core_transfer_geometry.c libretro/core_spv_binary.c -o "$out/probe-generator"
"$out/probe-generator" "$out/transfer"
set -- "$out"/transfer-*.spv
test "$#" = 4
for module in "$@"; do spirv-val --target-env vulkan1.0 "$module"; done
gcc $flags -DXE_SHADER_COMPILE_BENCH -I"$head" -I"$root/tools/tests" -I"$root" -isystem "$head/third_party/Vulkan-Headers/include" -isystem "$head/third_party/SPIRV-Headers/include" "$root/tools/tests/transfer_uint_warm_vulkan_test.c" "$head/libretro/core_transfer_uint_vk.c" "$head/libretro/core_transfer_uint_spv.c" "$head/libretro/core_spv_binary.c" -ldl -o "$out/transfer-warm"
if test "${GPU_TESTS:-0}" = 1; then
 unset TRANSFER_REFERENCE TRANSFER_CORRECTNESS_ONLY TRANSFER_GPU_CSV TRANSFER_REVERSE_ORDER
 parent_args=
 if test -n "$parent"; then parent_args=--parent; fi
 export TRANSFER_LEGACY_PREFIX="$out/legacy-modules/legacy"
 XE_SHADER_BENCH_CPU_CSV="$out/reference-init-cpu.csv" XE_SHADER_BENCH_CSV="$out/reference-init.csv" TRANSFER_REFERENCE=1 "$out/transfer-warm" "$out/transfer" >"$out/reference.log" 2>&1
 XE_SHADER_BENCH_CPU_CSV="$out/prepared-init-cpu.csv" XE_SHADER_BENCH_CSV="$out/prepared-init.csv" TRANSFER_CORRECTNESS_ONLY=1 "$out/transfer-warm" "$out/transfer" >"$out/prepared.log" 2>&1
 XE_SHADER_BENCH_CPU_CSV="$out/forward-init-cpu.csv" XE_SHADER_BENCH_CSV="$out/forward-init.csv" TRANSFER_GPU_CSV="$out/forward.csv" "$out/transfer-warm" "$out/transfer" >"$out/forward.log" 2>&1
 XE_SHADER_BENCH_CPU_CSV="$out/reverse-init-cpu.csv" XE_SHADER_BENCH_CSV="$out/reverse-init.csv" TRANSFER_REVERSE_ORDER=1 TRANSFER_GPU_CSV="$out/reverse.csv" "$out/transfer-warm" "$out/transfer" >"$out/reverse.log" 2>&1
 python3 tools/tests/transfer_uint_warm_analysis.py --input "$out/forward.csv" --out "$out/forward.json" $parent_args
 python3 tools/tests/transfer_uint_warm_analysis.py --reverse --input "$out/reverse.csv" --out "$out/reverse.json" $parent_args
fi
