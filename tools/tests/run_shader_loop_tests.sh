#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${SHADER_LOOP_OUT:-build/tests/shader-loop-unit}
mkdir -p "$out"
${CC:-cc} -std=c89 -pedantic -Wno-long-long -O2 -g -Wall -Wextra -Werror \
 ${SANITIZER--fsanitize=address,undefined} -I. -Ithird_party/libretro-common/include \
 tools/tests/core_shader_loop_test.c -o "$out/test"
"$out/test"
