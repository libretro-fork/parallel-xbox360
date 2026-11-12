#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${PRIMITIVE_RANGES_OUT:-build/tests/primitive-ranges}
mkdir -p "$out"
${CC:-cc} -std=c89 -pedantic-errors -Wno-long-long -O2 -Wall -Wextra -Werror \
 -I. -Ithird_party/libretro-common/include tools/tests/core_primitive_ranges_test.c -o "$out/test"
"$out/test"
