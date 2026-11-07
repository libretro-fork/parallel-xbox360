#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${POINTER_UNIQUE_OUT:-build/tests/pointer-unique-unit}
mkdir -p "$out"
${CC:-cc} -std=c89 -pedantic -Wno-long-long -O2 -g -Wall -Wextra -Werror \
 ${SANITIZER--fsanitize=address,undefined} -I. -Ithird_party/libretro-common/include \
 tools/tests/core_pointer_unique_test.c -o "$out/test"
"$out/test"
