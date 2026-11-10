#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${MSAA_IMAGE_OUT:-build/tests/transfer-msaa-image}
UINT_IMAGE_OUT="$out" sh tools/tests/run_transfer_uint_image_tests.sh
flags='-std=c89 -pedantic-errors -Wno-long-long -Wall -Wextra -Werror -O2'
gcc $flags -I. -isystem third_party/SPIRV-Headers/include tools/tests/transfer_msaa_spv_fixture.c libretro/core_spv_binary.c -o "$out/msaa-generator"
"$out/msaa-generator" "$out/transfer"
for f in "$out"/transfer-*.spv; do spirv-val --target-env vulkan1.0 "$f"; done
gcc $flags -I. -isystem third_party/Vulkan-Headers/include -isystem third_party/SPIRV-Headers/include tools/tests/transfer_msaa_image_vulkan_test.c libretro/core_transfer_uint_vk.c libretro/core_transfer_uint_spv.c libretro/core_spv_binary.c -ldl -o "$out/msaa-vulkan"
if test "${GPU_TESTS:-0}" = 1; then
 "$out/msaa-vulkan" "$out/transfer"
 MSAA_DISABLE_SAMPLE_SHADING=1 "$out/msaa-vulkan" "$out/transfer"
fi
