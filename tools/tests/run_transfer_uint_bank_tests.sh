#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${UINT_BANK_OUT:-build/tests/transfer-uint-bank}
mkdir -p "$out"
flags='-std=c89 -pedantic-errors -Wno-long-long -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined'
gcc $flags -DXE_GPU_DRAW_STATS=1 -I. -Ithird_party/libretro-common/include -isystem third_party/Vulkan-Headers/include -isystem third_party/SPIRV-Headers/include tools/tests/core_transfer_uint_vk_test.c libretro/core_draw_stats.c libretro/core_transfer_uint_vk.c libretro/core_transfer_uint_spv.c libretro/core_spv_binary.c -o "$out/bank-asan"
"$out/bank-asan"
gcc $flags -I. -Ithird_party/libretro-common/include -isystem third_party/SPIRV-Headers/include tools/tests/transfer_uint_shape_spv_fixture.c libretro/core_transfer_uint_spv.c libretro/core_spv_binary.c -o "$out/shape-generator"
"$out/shape-generator" "$out/shape"
for module in "$out"/shape-*.spv; do spirv-val --target-env vulkan1.0 "$module"; done
sh tools/tests/run_command_stream_tests.sh
