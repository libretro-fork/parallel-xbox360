#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${SPV_BINDINGS_OUT:-build/tests/spv-bindings}
mkdir -p "$out"
${CC:-cc} -std=c89 -pedantic -Wno-long-long -Wdeclaration-after-statement -Wall -Wextra -Werror -O2 \
 -I. -Ithird_party/libretro-common/include tools/tests/core_spv_bindings_test.c -o "$out/test"
"$out/test"
