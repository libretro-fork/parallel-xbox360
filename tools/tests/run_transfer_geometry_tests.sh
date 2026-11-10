#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${TRANSFER_OUT:-build/tests/transfer-geometry}
mkdir -p "$out"
${CC:-cc} -std=c89 -pedantic-errors -Wno-long-long -Wall -Wextra -Werror -O2 \
 -I. -isystem third_party/libretro-common/include -isystem third_party/SPIRV-Headers/include \
 tools/tests/core_transfer_geometry_test.c libretro/core_transfer_geometry.c \
 libretro/core_spv_binary.c -o "$out/generator"
"$out/generator" "$out/transfer.spv"
spirv-val --target-env vulkan1.0 "$out/transfer.spv"
spirv-val --target-env vulkan1.0 "$out/transfer.spv.xfb.spv"
if test "${GPU_TESTS:-0}" = 1; then
 ${CC:-cc} -std=c89 -pedantic-errors -Wno-long-long -Wall -Wextra -Werror -O2 \
  -I. -isystem third_party/libretro-common/include -isystem third_party/Vulkan-Headers/include \
  tools/tests/transfer_geometry_vulkan_test.c -ldl -o "$out/vulkan"
 "$out/vulkan" "$out/transfer.spv.xfb.spv"
fi
