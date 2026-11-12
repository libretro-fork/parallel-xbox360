#!/bin/sh
# Untimed qualification of the existing producer used as the GPU reference.
# Pass a verified source snapshot of the exact parent; no game assets are read.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
test "$#" = 1
parent=$(CDPATH= cd -- "$1" && pwd)
out=${IDENTITY_REFERENCE_OUT:-"$root/build/tests/transfer-identity-reference"}
mkdir -p "$out"
out=$(CDPATH= cd -- "$out" && pwd)
sh "$root/tools/tests/build_transfer_identity_reference.sh" "$parent" "$out"
flags='-std=c89 -pedantic-errors -Wno-long-long -O2 -Wall -Wextra -Werror'
"$out/generator" "$out/legacy"
set -- "$out"/legacy-*.spv
test "$#" = 52
for module in "$@"; do spirv-val --target-env vulkan1.0 "$module"; done
"$out/msaa-generator" "$out/msaa-legacy"
set -- "$out"/msaa-legacy-*.spv
test "$#" = 120
for module in "$@"; do spirv-val --target-env vulkan1.0 "$module"; done
"$out/msaa-generator" "$out/msaa-full" --all-sources
set -- "$out"/msaa-full-*.spv
test "$#" = 360
for module in "$@"; do spirv-val --target-env vulkan1.0 "$module"; done
cd "$root"
gcc $flags -I. -Ithird_party/libretro-common/include -isystem third_party/SPIRV-Headers/include tools/tests/transfer_identity_fixture.c libretro/core_transfer_identity_spv.c libretro/core_transfer_geometry.c libretro/core_spv_binary.c -o "$out/identity-generator"
"$out/identity-generator" "$out/identity"
gcc $flags -I. -isystem third_party/SPIRV-Headers/include tools/tests/transfer_identity_msaa_spv_fixture.c libretro/core_spv_binary.c -o "$out/msaa-utilities"
"$out/msaa-utilities" "$out/identity"
for module in "$out"/identity-*.spv; do spirv-val --target-env vulkan1.0 "$module"; done
gcc $flags -I. -Ithird_party/libretro-common/include -isystem third_party/SPIRV-Headers/include -isystem third_party/Vulkan-Headers/include tools/tests/transfer_identity_image_vulkan_test.c libretro/core_transfer_identity_vk.c libretro/core_transfer_identity_spv.c libretro/core_spv_binary.c -ldl -o "$out/image"
gcc $flags -I. -Ithird_party/libretro-common/include -isystem third_party/SPIRV-Headers/include -isystem third_party/Vulkan-Headers/include tools/tests/transfer_identity_msaa_reference_vulkan_test.c libretro/core_transfer_identity_vk.c libretro/core_transfer_identity_spv.c libretro/core_spv_binary.c -ldl -o "$out/msaa-image"
gcc $flags -I. -Ithird_party/libretro-common/include -isystem third_party/SPIRV-Headers/include -isystem third_party/Vulkan-Headers/include tools/tests/transfer_identity_bank_init_vulkan_test.c libretro/core_transfer_identity_vk.c libretro/core_transfer_identity_spv.c libretro/core_spv_binary.c -ldl -o "$out/bank-init"
# Small images exercise work/query/image paths; they never qualify throughput.
gcc $flags -I. -Ithird_party/libretro-common/include -isystem third_party/SPIRV-Headers/include tools/tests/transfer_identity_msaa_warm_spv_fixture.c libretro/core_transfer_geometry.c libretro/core_spv_binary.c -o "$out/msaa-warm-utilities"
"$out/msaa-warm-utilities" "$out/warm"
for module in "$out"/warm-*.spv; do spirv-val --target-env vulkan1.0 "$module"; done
gcc $flags -DXE_SHADER_COMPILE_BENCH=1 -DXE_IDENTITY_MSAA_SMALL=1 -I. -Ithird_party/libretro-common/include -isystem third_party/SPIRV-Headers/include -isystem third_party/Vulkan-Headers/include tools/tests/transfer_identity_msaa_warm_vulkan_test.c libretro/core_transfer_identity_vk.c libretro/core_transfer_identity_spv.c libretro/core_spv_binary.c -ldl -o "$out/msaa-work"
if test "${GPU_TESTS:-0}" = 1; then
 # One complete bank per process, including manual sample shading and both
 # rendering interfaces. Creation counts and post-close ownership are checked.
 sh tools/tests/run_transfer_identity_bank_init_vulkan_tests.sh "$out/bank-init" "$out/identity" "$out/bank-init-logs"
 IDENTITY_LEGACY_PREFIX="$out/legacy" "$out/image" "$out/identity"
 python3 tools/tests/transfer_identity_msaa_reference_check.py "$out/msaa-image" "$out/identity" "$out/msaa-legacy" "$out/msaa-logs"
 for mode in render-pass dynamic stale sample; do
  case "$mode" in
   render-pass) set -- --diagnostic ;;
   dynamic) set -- --diagnostic --dynamic ;;
   stale|sample) set -- --diagnostic --control "$mode" ;;
  esac
  VK_INSTANCE_LAYERS="${IDENTITY_MSAA_VALIDATION:-${VK_INSTANCE_LAYERS:-}}" python3 tools/tests/transfer_identity_msaa_warm_check.py "$out/msaa-work" "$out/warm" "$out/msaa-full" "$out/msaa-work-$mode" "$@"
 done
else
 echo 'UNVERIFIED: legacy numeric reference images disabled (set GPU_TESTS=1)'
fi
