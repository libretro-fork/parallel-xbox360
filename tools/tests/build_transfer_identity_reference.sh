#!/bin/sh
# Build both C89 reference drivers against one copy of the real producer.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
test "$#" = 2
parent=$(CDPATH= cd -- "$1" && pwd)
mkdir -p "$2"
out=$(CDPATH= cd -- "$2" && pwd)
cd "$parent"
{
 echo '#include "xenia/gpu/spirv_shader_translator.h"'
 echo '#include "xenia/gpu/spirv_compatibility.h"'
 echo '#include "xenia/gpu/spirv/GLSL.std.450.h"'
 echo 'namespace xe { namespace gpu {'
 awk '/^spv::Id SpirvShaderTranslator::(PreClampedDepthTo20e4|UnclampedFloat32To7e3|PreClampedFloat32To7e3|Float7e3To32|Depth20e4To32|PWLGammaToLinear|LinearToPWLGamma)\(/ { emit=1; count++ } emit { print } emit && /^}/ { emit=0 } END { if (count != 7) exit 1 }' src/xenia/gpu/spirv_shader_translator.cc src/xenia/gpu/spirv_shader_translator_rb.cc
 echo '} }'
} > "$out/static-emitters.cc"
flags='-std=c89 -pedantic-errors -Wno-long-long -O2 -Wall -Wextra -Werror'
for name in binary packet arena size; do
 gcc $flags -I. -Ithird_party/libretro-common/include -c libretro/core_spv_$name.c -o "$out/legacy-$name.o"
done
gcc $flags -I. -I"$root/tools/tests" -Ithird_party/libretro-common/include -c "$root/tools/tests/transfer_identity_reference_fixture.c" -o "$out/driver.o"
gcc $flags -I. -I"$root/tools/tests" -Ithird_party/libretro-common/include -c "$root/tools/tests/transfer_identity_msaa_reference_fixture.c" -o "$out/msaa-driver.o"
gcc $flags -I. -I"$root/tools/tests" -Ithird_party/libretro-common/include -c "$root/tools/tests/transfer_color_reference_fixture.c" -o "$out/color-driver.o"
set --
index=0
for source in "$root/tools/tests/transfer_uint_reference_bridge.cc" "$root/tools/tests/transfer_color_reference_bridge.cc" "$out/static-emitters.cc" src/xenia/gpu/edram_transfer_shader.cc src/xenia/gpu/spirv_builder.cc src/xenia/gpu/spirv/SpvBuilder.cpp src/xenia/gpu/spirv/InReadableOrder.cpp src/xenia/gpu/spirv/Logger.cpp; do
 object="$out/producer-$index.o"
 g++ -std=c++20 -O2 -DNDEBUG -DXE_TEST_TRANSFER_NUMERIC_REFERENCE -DXENIA_LIBRETRO=1 -DVULKAN_HPP_NO_TO_STRING -Wno-attributes -Isrc -I. -I"$root/tools/tests" -Ithird_party/libretro-common/include -Ithird_party/fmt/include -Ithird_party -Ithird_party/Vulkan-Headers/include -c "$source" -o "$object"
 set -- "$@" "$object"
 index=$((index+1))
done
g++ "$out/driver.o" "$@" "$out/legacy-binary.o" "$out/legacy-packet.o" "$out/legacy-arena.o" "$out/legacy-size.o" -o "$out/generator"
g++ "$out/msaa-driver.o" "$@" "$out/legacy-binary.o" "$out/legacy-packet.o" "$out/legacy-arena.o" "$out/legacy-size.o" -o "$out/msaa-generator"
g++ "$out/color-driver.o" "$@" "$out/legacy-binary.o" "$out/legacy-packet.o" "$out/legacy-arena.o" "$out/legacy-size.o" -o "$out/color-generator"
sha256sum src/xenia/gpu/edram_transfer_shader.cc src/xenia/gpu/edram_transfer_shader.h src/xenia/gpu/spirv_shader_translator.cc src/xenia/gpu/spirv_shader_translator_rb.cc "$root/tools/tests/transfer_uint_reference_bridge.cc" "$root/tools/tests/transfer_identity_reference_fixture.c" "$root/tools/tests/transfer_identity_msaa_reference_fixture.c" > "$out/source-manifest.txt"
