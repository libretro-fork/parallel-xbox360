#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/xma-payload"}
mkdir -p "$out"
${CC:-cc} -std=c89 -pedantic -Wall -Wextra -Werror ${SANITIZER-} \
 -I"$root" -I"$root/third_party/libretro-common/include" \
 "$root/tools/tests/core_xma_payload_test.c" -o "$out/test"
"$out/test"
