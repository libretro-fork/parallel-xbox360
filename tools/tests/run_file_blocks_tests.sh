#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/file-blocks-$(uname -s)"}
mkdir -p "$out"
san=${SANITIZER--fsanitize=address,undefined}
${CC:-cc} -std=c89 -O2 -g $san -Wall -Wextra -Werror \
 -I"$root/libretro" -I"$root/third_party/libretro-common/include" \
 -include "$root/tools/tests/core_file_blocks_test_alloc.h" \
 "$root/libretro/core_file_blocks.c" "$root/tools/tests/core_file_blocks_test.c" -o "$out/test"
"$out/test"
