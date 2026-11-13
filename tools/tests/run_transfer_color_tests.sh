#!/bin/sh
# Procedural color semantics, failure ownership and actual images; no timing.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${COLOR_OUT:-"$root/build/tests/transfer-color"}
mkdir -p "$out"
out=$(CDPATH= cd -- "$out" && pwd)
flags='-std=c89 -pedantic-errors -Wno-long-long -O2 -Wall -Wextra -Werror'
includes='-I. -Ithird_party/libretro-common/include -isystem third_party/SPIRV-Headers/include -isystem third_party/Vulkan-Headers/include'
for name in color_packet format; do
 gcc $flags $includes -fsanitize=address,undefined -fno-sanitize-recover=all "tools/tests/core_transfer_${name}_test.c" -lm -o "$out/$name"
 ASAN_OPTIONS=detect_leaks=1 "$out/$name"
done
gcc $flags $includes -fsanitize=address,undefined -fno-sanitize-recover=all tools/tests/core_transfer_color_vk_test.c libretro/core_transfer_color_vk.c libretro/core_transfer_color_spv.c libretro/core_spv_binary.c -o "$out/bank"
ASAN_OPTIONS=detect_leaks=1 "$out/bank"
gcc $flags $includes tools/tests/transfer_color_spv_fixture.c -o "$out/generate"
"$out/generate" "$out"
set -- "$out"/color-*.spv
test "$#" = 576
for module in "$@"; do spirv-val --target-env vulkan1.0 "$module"; done
gcc $flags $includes -fsanitize=address,undefined -fno-sanitize-recover=all tools/tests/transfer_color_shape_fixture.c -o "$out/shape-generate"
ASAN_OPTIONS=detect_leaks=1 "$out/shape-generate" "$out"
set -- "$out"/shape-*.spv
test "$#" = 1152
for module in "$@"; do spirv-val --target-env vulkan1.0 "$module"; done
gcc $flags $includes tools/tests/transfer_format_spv_fixture.c libretro/core_spv_binary.c -lm -o "$out/format-generate"
"$out/format-generate" "$out/format.spv"
spirv-val --target-env vulkan1.0 "$out/format.spv"
gcc $flags $includes -DXE_TRANSFER_FORMAT_TEST tools/tests/transfer_sample_vulkan_test.c -ldl -lm -o "$out/format-gpu"
gcc $flags $includes tools/tests/transfer_color_image_vulkan_test.c libretro/core_transfer_color_vk.c libretro/core_transfer_color_spv.c libretro/core_transfer_geometry.c libretro/core_spv_binary.c -ldl -o "$out/image"
gcc $flags $includes -DXE_COLOR_WARM_BENCH tools/tests/transfer_color_image_vulkan_test.c libretro/core_transfer_color_vk.c libretro/core_transfer_color_spv.c libretro/core_transfer_geometry.c libretro/core_spv_binary.c -ldl -o "$out/warm"
if test "${GPU_TESTS:-0}" = 1; then
 reference=${COLOR_REFERENCE_BUILD:-"$out/reference"}
 if test -z "${COLOR_REFERENCE_BUILD:-}"; then
  sh tools/tests/build_transfer_identity_reference.sh "$root" "$reference"
 fi
 mkdir -p "$out/legacy"
 "$reference/color-generator" "$out/legacy"
 for module in "$out"/legacy/ref-*.spv; do spirv-val --target-env vulkan1.0 "$module"; done
 "$out/format-gpu" "$out/format.spv"
 for packed in 0 1; do
  COLOR_PACKED_BANK=$packed COLOR_DYNAMIC_RENDERING=0 "$out/image" "$out" "$out/legacy"
  COLOR_PACKED_BANK=$packed COLOR_DYNAMIC_RENDERING=1 "$out/image" "$out" "$out/legacy"
 done
else
 echo 'UNVERIFIED: color reference GPU images disabled (set GPU_TESTS=1)'
fi
