#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/gpu-downscale"}
mkdir -p "$out"
exe=plan-test
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) exe=plan-test.exe ;; esac
san=${SANITIZER--fsanitize=address,undefined}
${CC:-cc} -std=c89 -pedantic -Wall -Wextra -Werror -O2 -g \
  $san -I"$root" "$root/tools/tests/core_gpu_downscale_test.c" -o "$out/$exe"
"$out/$exe"
# Set GPU_TESTS=1 to run the actual compute modules on an installed Vulkan driver.
# API headers use extensions; our test code still rejects declarations after statements.
if [ "${GPU_TESTS:-0}" = 1 ]; then
  libs=-ldl
  case $(uname -s) in MINGW*|MSYS*|CYGWIN*) libs= ;; esac
  ${CC:-cc} -std=gnu89 -Wall -Wextra -Werror -Wdeclaration-after-statement -O2 \
    -I"$root" -isystem "$root/third_party/Vulkan-Headers/include" \
    "$root/tools/tests/core_downscale_vulkan_test.c" -o "$out/vulkan-test" $libs
  "$out/vulkan-test" "$root/tools/tests/data/resolve_downscale_reference.spv" \
    "$root/src/xenia/gpu/shaders/bytecode/vulkan_spirv/resolve_downscale_cs.spv"
  case $(uname -s) in MINGW*|MSYS*|CYGWIN*)
    ${CC:-cc} -std=gnu89 -Wall -Wextra -Werror -Wdeclaration-after-statement -O2 \
      -I"$root" -isystem "$root/third_party/DirectX-Headers/include" \
      "$root/tools/tests/core_downscale_d3d12_test.c" -o "$out/d3d12-test" -ld3d12 -ldxgi -ldxguid
    "$out/d3d12-test" "$root/src/xenia/gpu/shaders/bytecode/d3d12_dxil/resolve_downscale_cs.dxil"
  ;; esac
fi
