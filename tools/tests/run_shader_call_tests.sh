#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${SHADER_CALL_OUT:-build/tests/shader-call}
mkdir -p "$out"
${CC:-cc} -std=c89 -pedantic -O2 ${SANITIZER:-} -Wall -Wextra -Werror -I. -Ithird_party/libretro-common/include tools/tests/core_shader_call_test.c -o "$out/test"
"$out/test"
