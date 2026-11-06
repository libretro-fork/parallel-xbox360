#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/descriptor-scratch-$(uname -s)"}
mkdir -p "$out"
san=${SANITIZER--fsanitize=address,undefined}
${CC:-cc} -std=c89 -O1 -g -pedantic -Wall -Wextra -Werror $san \
 -isystem "$root/third_party/Vulkan-Headers/include" -I"$root/libretro" -I"$root/third_party/libretro-common/include" \
 "$root/tools/tests/core_descriptor_scratch_test.c" -o "$out/test"
"$out/test"