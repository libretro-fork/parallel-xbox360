#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
out="${SHADER_MEMEXPORT_OUT:-build/tests/shader-memexport}"
mkdir -p "$out"
${CC:-cc} -std=c89 -pedantic -Wno-long-long -Wall -Wextra -Werror -O2 ${SHADER_MEMEXPORT_FLAGS:-} \
 -I. -Ithird_party/libretro-common/include tools/tests/core_shader_memexport_test.c -o "$out/unit"
"$out/unit"
