#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/descriptor-scratch-bridge-$(uname -s)"}
mkdir -p "$out"
san=${SANITIZER--fsanitize=address,undefined}
ext=
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) ext=.exe ;; esac
cd "$root"
inc="-I. -Isrc -Ilibretro -Ithird_party/libretro-common/include -Ithird_party/fmt/include -isystem third_party/Vulkan-Headers/include"
${CC:-cc} -std=c89 -O1 -g -Wall -Wextra -Werror -pedantic $san -DXE_DESCRIPTOR_BRIDGE $inc \
 -c tools/tests/core_descriptor_scratch_test.c -o "$out/test.o"
${CXX:-c++} -std=c++20 -O1 -g -Wno-attributes -DNDEBUG -DVULKAN_HPP_NO_TO_STRING -DXENIA_LIBRETRO -DRETRO_ATOMIC_FORCE_GCC_NEW \
 $san $inc tools/tests/core_descriptor_scratch_bridge.cc "$out/test.o" -o "$out/test$ext"
"$out/test$ext"
