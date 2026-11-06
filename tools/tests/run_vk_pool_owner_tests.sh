#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/vk-pool-owner-$(uname -s)"}
mkdir -p "$out"
san=${SANITIZER--fsanitize=address,undefined}
# Keep paths containing spaces intact: execute compiler commands from repo root.
cd "$root"
${CC:-cc} -std=c89 -O1 -g -pedantic -Wall -Wextra -Werror $san \
 -Ilibretro -Ithird_party/libretro-common/include -isystem third_party/Vulkan-Headers/include \
 tools/tests/core_vk_pool_owner_test.c -o "$out/leaf"
"$out/leaf"
${CXX:-c++} -std=c++20 -O1 -g -DNDEBUG -Wall -Wextra -Werror -Wno-unused-value $san \
 -include tools/tests/vk_pool_metadata_hooks.h -Itools/tests/vk_pool_stubs -I. -Isrc -Ilibretro -Ithird_party/libretro-common/include \
 -isystem third_party/Vulkan-Headers/include \
 tools/tests/vk_pool_owner_bridge.cc src/xenia/ui/vulkan/linked_type_descriptor_set_allocator.cc -o "$out/bridge"
"$out/bridge"
