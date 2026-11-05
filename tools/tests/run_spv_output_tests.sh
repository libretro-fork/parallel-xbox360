#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/spv-output-$(uname -s)"}
mkdir -p "$out/base"
if test -n "${BASELINE_ARCHIVE:-}"; then
 cp "$BASELINE_ARCHIVE" "$out/base.tar"
else
 baseline=$(git -C "$root" rev-parse --verify "${BASELINE_COMMIT:-4accd7893}^{commit}")
git -C "$root" archive "$baseline" src/xenia/gpu/spirv src/xenia/gpu/spirv_shader_translator.cc src/xenia/gpu/spirv_shader_translator_rb.cc src/xenia/gpu/spirv_builder.h src/xenia/gpu/spirv_builder.cc src/xenia/gpu/edram_dump_shader.h src/xenia/gpu/edram_dump_shader.cc src/xenia/gpu/edram_transfer_shader.h src/xenia/gpu/edram_transfer_shader.cc > "$out/base.tar"
fi
tar -xf "$out/base.tar" -C "$out/base"
# Isolate the seven production static emitters needed by the helpers, without
# linking the unrelated translator's configuration registrars and Vulkan device.
# All selected function bodies come verbatim from the archived parent sources.
{
 echo '#include "xenia/gpu/spirv_shader_translator.h"'
 echo '#include "xenia/gpu/spirv_compatibility.h"'
 echo '#include "xenia/gpu/spirv/GLSL.std.450.h"'
 echo 'namespace xe { namespace gpu {'
 awk '/^spv::Id SpirvShaderTranslator::(PreClampedDepthTo20e4|UnclampedFloat32To7e3|PreClampedFloat32To7e3|Float7e3To32|Depth20e4To32|PWLGammaToLinear|LinearToPWLGamma)\(/ { emit=1; count++ } emit { print } emit && /^}/ { emit=0 } END { if (count != 7) exit 1 }' "$out/base/src/xenia/gpu/spirv_shader_translator.cc" "$out/base/src/xenia/gpu/spirv_shader_translator_rb.cc"
 echo '} }'
} > "$out/static_emitters.cc"
san=${SANITIZER-}
${CC:-cc} -std=c89 -pedantic -O2 $san -Wall -Wextra -Werror "$root/tools/tests/core_spv_binary_test.c" -o "$out/unit"
"$out/unit"
${CC:-cc} -std=c89 -pedantic -O2 $san -Wall -Wextra -Werror -c "$root/libretro/core_spv_binary.c" -o "$out/binary.o"
${CC:-cc} -std=c89 -O2 $san -I"$root/third_party/libretro-common/include" -c "$root/libretro/core_spv_operands.c" -o "$out/operands.o"
for mode in baseline current; do
 src="$root/src"
 legacy=
 if test "$mode" = baseline; then src="$out/base/src"; legacy=-DXE_SPV_OUTPUT_LEGACY; fi
 ${CXX:-c++} -std=c++20 -O2 -DNDEBUG -DXENIA_LIBRETRO=1 -DVULKAN_HPP_NO_TO_STRING -Wno-attributes $san $legacy -I"$src" -I"$root/src" -I"$root" \
  -I"$root/third_party/libretro-common/include" -I"$root/third_party/fmt/include" \
  -I"$root/third_party" -I"$root/third_party/Vulkan-Headers/include" \
  "$root/tools/tests/core_spv_edram_output_test.cc" \
  "$out/static_emitters.cc" \
  "$src/xenia/gpu/edram_dump_shader.cc" "$src/xenia/gpu/edram_transfer_shader.cc" \
  "$src/xenia/gpu/spirv_builder.cc" "$src/xenia/gpu/spirv/SpvBuilder.cpp" \
  "$src/xenia/gpu/spirv/SpvPostProcess.cpp" "$src/xenia/gpu/spirv/InReadableOrder.cpp" \
  "$src/xenia/gpu/spirv/Logger.cpp" "$out/binary.o" "$out/operands.o" -o "$out/$mode"
 "$out/$mode" "$out/$mode.bin"
done
cmp "$out/baseline.bin" "$out/current.bin"
${CXX:-c++} -std=c++20 -O2 -DNDEBUG -DXE_SPV_BINARY_CHECK $san \
 -I"$root/src/xenia/gpu/spirv" -I"$root" -I"$root/third_party/libretro-common/include" \
 "$root/tools/tests/core_spv_module_bridge.cc" \
 "$root/src/xenia/gpu/spirv/SpvBuilder.cpp" "$root/src/xenia/gpu/spirv/SpvPostProcess.cpp" \
 "$root/src/xenia/gpu/spirv/InReadableOrder.cpp" "$root/src/xenia/gpu/spirv/Logger.cpp" \
 "$out/binary.o" "$out/operands.o" -o "$out/debug-bridge"
for variant in 0 1 2 3 4; do
 "$out/debug-bridge" "$out/debug-$variant.spv" "$variant"
 if test -n "${SPIRV_VALIDATOR:-}"; then "$SPIRV_VALIDATOR" "$out/debug-$variant.spv"; fi
done
echo "PASS: parent/current EDRAM output byte-for-byte, stable high-water storage"
