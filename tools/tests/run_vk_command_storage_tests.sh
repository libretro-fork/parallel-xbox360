#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/vk-command-storage-$(uname -s)"}
mkdir -p "$out"
cd "$root"
san=${SANITIZER--fsanitize=address,undefined}
# Vulkan SDK headers use newer syntax; the fixture and page utility stay C89.
${CC:-cc} -std=c89 -O1 -g -pedantic -Wall -Wextra -Werror $san \
 -Ilibretro -Ithird_party/libretro-common/include \
 -isystem third_party/Vulkan-Headers/include \
 tools/tests/core_vk_command_storage_test.c -o "$out/leaf"
"$out/leaf"
