#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${IDENTITY_OUT:-build/tests/transfer-identity}
mkdir -p "$out"
flags='-std=c89 -pedantic-errors -Wno-long-long -Wall -Wextra -Werror -O2'
includes='-I. -Ithird_party/libretro-common/include -isystem third_party/SPIRV-Headers/include -isystem third_party/Vulkan-Headers/include'
gcc $flags $includes -fsanitize=address,undefined -fno-sanitize-recover=all tools/tests/core_transfer_identity_test.c -o "$out/packet"
"$out/packet"
gcc $flags $includes -DXE_GPU_DRAW_STATS=1 -fsanitize=address,undefined -fno-sanitize-recover=all tools/tests/core_transfer_identity_vk_test.c libretro/core_draw_stats.c libretro/core_transfer_identity_vk.c libretro/core_transfer_identity_spv.c libretro/core_spv_binary.c -o "$out/bank"
"$out/bank"
gcc $flags $includes -fsanitize=address,undefined -fno-sanitize-recover=all tools/tests/transfer_identity_fixture.c libretro/core_transfer_identity_spv.c libretro/core_transfer_geometry.c libretro/core_spv_binary.c -o "$out/generator"
"$out/generator" "$out/identity"
gcc $flags $includes -fsanitize=address,undefined -fno-sanitize-recover=all tools/tests/transfer_identity_msaa_spv_fixture.c libretro/core_spv_binary.c -o "$out/msaa-generator"
"$out/msaa-generator" "$out/identity"
for module in "$out"/identity-*.spv; do spirv-val --target-env vulkan1.0 "$module"; done
gcc $flags $includes tools/tests/transfer_identity_image_vulkan_test.c libretro/core_transfer_identity_vk.c libretro/core_transfer_identity_spv.c libretro/core_spv_binary.c -ldl -o "$out/image"
gcc $flags $includes tools/tests/transfer_identity_msaa_vulkan_test.c libretro/core_transfer_identity_vk.c libretro/core_transfer_identity_spv.c libretro/core_spv_binary.c -ldl -o "$out/msaa"
if test "${GPU_TESTS:-0}" = 1; then
 "$out/image" "$out/identity"
 python3 tools/tests/transfer_identity_msaa_check.py vulkan "$out/msaa" "$out/identity" "$out/logs"
 python3 tools/tests/transfer_identity_msaa_check.py vulkan "$out/msaa" "$out/identity" "$out/typed-logs" --typed
else
 echo 'UNVERIFIED: identity GPU image checks disabled (set GPU_TESTS=1)'
fi
