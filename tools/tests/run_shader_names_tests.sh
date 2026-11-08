#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${SHADER_NAMES_OUT:-build/tests/shader-names}
mkdir -p "$out"
${CC:-cc} -std=c89 -pedantic -Wno-long-long -Wdeclaration-after-statement -Wall -Wextra -Werror -O2 \
 -I. -Ithird_party/libretro-common/include tools/tests/core_shader_names_test.c -o "$out/test"
"$out/test"
