#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/vk-sparse-bind-$(uname -s)"}
mkdir -p "$out"
san=${SANITIZER--fsanitize=address,undefined}
${CC:-cc} -std=c89 -O1 -g -pedantic -Wall -Wextra -Werror $san \
 -I"$root/libretro" -I"$root/third_party/libretro-common/include" \
 -isystem "$root/third_party/Vulkan-Headers/include" \
 "$root/tools/tests/core_vk_sparse_bind_test.c" -o "$out/test"
"$out/test"
