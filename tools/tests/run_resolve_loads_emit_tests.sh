#!/bin/sh
# Serial, CPU-only production emitter checks. No Vulkan/D3D12 device is created.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${TEST_BUILD_DIR:-"$root/build/tests/resolve-loads-emit-$(uname -s)"}
mkdir -p "$out/current-spv" "$out/current-dxil"
out=$(CDPATH= cd -- "$out" && pwd)
# Optional same-ABI dynamic-address reference (0149 parent 9693c7c9b).
# Without BASELINE_COMMIT this runner needs no local Git history.
base=${BASELINE_COMMIT:-}
modes=current
if test -n "$base"; then
 mkdir -p "$out/base-spv" "$out/base-dxil"
 git show "$base:src/xenia/gpu/edram_dump_shader.cc" > "$out/dump-base.cc"
 git show "$base:src/xenia/gpu/dxil/edram_dump.c" > "$out/dump-base.c"
 modes='base current'
fi
extra=
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) extra=-Wno-long-long ;; esac
inc='-I. -Isrc -Itools/tests -Ithird_party/libretro-common/include -Ithird_party/fmt/include -Ithird_party -Ithird_party/Vulkan-Headers/include'
flags='-O2 -g -DNDEBUG -DXENIA_LIBRETRO -DVULKAN_HPP_NO_TO_STRING -DFMT_HEADER_ONLY'
for unit in size binary packet arena; do
 ${CC:-cc} -std=c89 -O2 $inc -c "libretro/core_spv_$unit.c" -o "$out/$unit.o"
done
{
 echo '#include "xenia/gpu/spirv_shader_translator.h"'
 echo '#include "xenia/gpu/spirv_compatibility.h"'
 echo '#include "xenia/gpu/spirv/GLSL.std.450.h"'
 echo 'namespace xe { namespace gpu {'
 awk '/^spv::Id SpirvShaderTranslator::(PreClampedDepthTo20e4|UnclampedFloat32To7e3|PreClampedFloat32To7e3|Float7e3To32|Depth20e4To32|PWLGammaToLinear|LinearToPWLGamma)\(/ { emit=1; count++ } emit { print } emit && /^}/ { emit=0 } END { if (count != 7) exit 1 }' src/xenia/gpu/spirv_shader_translator.cc src/xenia/gpu/spirv_shader_translator_rb.cc
 echo '} }'
} > "$out/static_emitters.cc"
for source in "$out/static_emitters.cc" tools/tests/core_resolve_loads_spv_bridge.cc src/xenia/gpu/spirv_builder.cc src/xenia/gpu/spirv/SpvBuilder.cpp src/xenia/gpu/spirv/InReadableOrder.cpp src/xenia/gpu/spirv/Logger.cpp; do
 ${CXX:-c++} -std=c++20 -Wno-attributes $flags $inc -c "$source" -o "$out/$(basename "$source" | sed 's/\.[^.]*$//').o"
done
${CC:-cc} -std=c89 -pedantic -Wall -Wextra -Werror $extra -O2 $inc -c tools/tests/core_resolve_loads_emit_test.c -o "$out/spv-test.o"
for mode in $modes; do
 source=src/xenia/gpu/edram_dump_shader.cc
 if test "$mode" = base; then source="$out/dump-base.cc"; fi
 ${CXX:-c++} -std=c++20 -Wno-attributes $flags $inc -c "$source" -o "$out/dump.o"
 ${CXX:-c++} -static-libgcc -static-libstdc++ "$out/spv-test.o" "$out/dump.o" "$out/static_emitters.o" "$out/core_resolve_loads_spv_bridge.o" "$out/spirv_builder.o" "$out/SpvBuilder.o" "$out/InReadableOrder.o" "$out/Logger.o" "$out/binary.o" "$out/packet.o" "$out/arena.o" "$out/size.o" -o "$out/spv-$mode"
 if test "$mode" = current && test -n "$base"; then
  "$out/spv-$mode" "$out/current-spv" "$out/base-spv"
 else "$out/spv-$mode" "$out/$mode-spv"; fi
done

dxil=src/xenia/gpu/dxil
dxinc="-Itools/tests -I$dxil -isystem $dxil/bytecode"
mkdir -p "$out/dxil-objects"
# Match the production emitter source set; only the dump TU receives test hooks.
for source in "$dxil"/*.c "$dxil"/bytecode/*.c; do
 if test "$source" = "$dxil/edram_dump.c"; then continue; fi
 ${CC:-cc} -std=c17 -O2 -I"$dxil" -I"$dxil/bytecode" -c "$source" -o "$out/dxil-objects/$(basename "$source" .c).o"
done
${CC:-cc} -std=c89 -pedantic -Wall -Wextra -Werror $extra -O2 -DXE_TEST_RESOLVE_DXIL $dxinc -c tools/tests/core_resolve_loads_emit_test.c -o "$out/dxil-test.o"
for mode in $modes; do
 source="$dxil/edram_dump.c"
 if test "$mode" = base; then source="$out/dump-base.c"; fi
 ${CC:-cc} -std=c89 -pedantic -Wall -Wextra -Werror $extra -O2 $dxinc -include tools/tests/core_resolve_loads_dxil_hook.h -c "$source" -o "$out/dxil-dump.o"
 ${CC:-cc} -static-libgcc "$out/dxil-test.o" "$out/dxil-dump.o" "$out"/dxil-objects/*.o -lm -o "$out/dxil-$mode"
 if test "$mode" = current && test -n "$base"; then
  "$out/dxil-$mode" "$out/current-dxil" "$out/base-dxil"
 else "$out/dxil-$mode" "$out/$mode-dxil"; fi
done

if test -n "${SPIRV_VALIDATOR:-}"; then
 for file in "$out"/current-spv/*.spv; do "$SPIRV_VALIDATOR" "$file"; done
 echo 'PASS: SPIR-V validation of every current matrix variant'
fi
if test -n "${DXIL_VALIDATOR:-}"; then
 for file in "$out"/current-dxil/*.dxil; do
  "$DXIL_VALIDATOR" "$file" -o="$file.validated"
  cmp "$file" "$file.validated"
 done
 echo 'PASS: DXIL validation of every current matrix variant'
fi
