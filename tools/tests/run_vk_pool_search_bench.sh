#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/vk-pool-search-bench-$(uname -s)"}
mkdir -p "$out"
cd "$root"
if [ -n "${BASELINE_SOURCE_DIR-}" ]; then
  ${CXX:-c++} -std=c++20 -O3 -DNDEBUG -include tools/tests/vk_pool_metadata_hooks.h \
   "-I$BASELINE_SOURCE_DIR" -Itools/tests/vk_pool_stubs -I. -Isrc -Ilibretro \
   -Ithird_party/libretro-common/include -isystem third_party/Vulkan-Headers/include \
   tools/tests/vk_pool_search_bench.cc \
   "$BASELINE_SOURCE_DIR/xenia/ui/vulkan/linked_type_descriptor_set_allocator.cc" -o "$out/prior"
  "$out/prior"
fi
${CXX:-c++} -std=c++20 -O3 -DNDEBUG -include tools/tests/vk_pool_metadata_hooks.h \
 -Itools/tests/vk_pool_stubs -I. -Isrc -Ilibretro -Ithird_party/libretro-common/include \
 -isystem third_party/Vulkan-Headers/include tools/tests/vk_pool_search_bench.cc \
 src/xenia/ui/vulkan/linked_type_descriptor_set_allocator.cc -o "$out/current"
"$out/current"
