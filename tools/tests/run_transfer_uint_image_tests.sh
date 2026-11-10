#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${UINT_IMAGE_OUT:-build/tests/transfer-uint-image}
mkdir -p "$out"
flags='-std=c89 -pedantic-errors -Wno-long-long -Wall -Wextra -Werror -O2'
gcc $flags -I. tools/tests/core_transfer_uint_packet_test.c -o "$out/packet"
"$out/packet"
gcc $flags -I. -Ithird_party/libretro-common/include -isystem third_party/SPIRV-Headers/include tools/tests/transfer_uint_spv_fixture.c libretro/core_transfer_uint_spv.c libretro/core_transfer_geometry.c libretro/core_spv_binary.c -o "$out/generator"
"$out/generator" "$out/transfer"
for f in "$out"/transfer-*.spv; do spirv-val --target-env vulkan1.0 "$f"; done
if test "${GPU_TESTS:-0}" = 1; then
 gcc $flags -I. -isystem third_party/Vulkan-Headers/include tools/tests/transfer_uint_image_vulkan_test.c -ldl -o "$out/vulkan"
 "$out/vulkan" "$out/transfer"
fi
