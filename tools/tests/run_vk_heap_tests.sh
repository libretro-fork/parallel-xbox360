#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/vk-heap-$(uname -s)"}
mkdir -p "$out"
ext=
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) ext=.exe ;; esac
san=${SANITIZER--fsanitize=address,undefined}
${CC:-cc} -std=c89 -Wall -Wextra -Werror -O2 -g $san -I"$root/libretro" \
 -isystem "$root/third_party/Vulkan-Headers/include" \
 "$root/libretro/core_gpu_spans.c" "$root/libretro/core_vk_heap.c" \
 "$root/tools/tests/core_vk_heap_test.c" -o "$out/test$ext"
"$out/test$ext"
