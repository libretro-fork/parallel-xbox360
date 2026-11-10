#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${SAMPLE_OUT:-build/tests/transfer-sample}
mkdir -p "$out"
flags='-std=c89 -pedantic-errors -Wno-long-long -Wall -Wextra -Werror -O2'
${CC:-cc} $flags -I. tools/tests/core_transfer_sample_test.c -o "$out/unit"
"$out/unit"
${CC:-cc} $flags -I. -isystem third_party/SPIRV-Headers/include \
 -DXE_TRANSFER_SAMPLE_GENERATOR tools/tests/transfer_sample_spv_fixture.c \
 libretro/core_spv_binary.c -o "$out/generator"
"$out/generator" "$out/sample.spv"
spirv-val --target-env vulkan1.0 "$out/sample.spv"
if test "${GPU_TESTS:-0}" = 1; then
 ${CC:-cc} $flags -I. -isystem third_party/Vulkan-Headers/include \
  tools/tests/transfer_sample_vulkan_test.c -ldl -o "$out/vulkan"
 "$out/vulkan" "$out/sample.spv"
fi
