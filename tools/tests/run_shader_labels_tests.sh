#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${SHADER_LABELS_OUT:-build/tests/shader-labels}
mkdir -p "$out"
${CC:-cc} ${SHADER_LABELS_FLAGS:-} -std=c89 -pedantic -Wno-long-long -O2 -Wall -Wextra -Werror \
 -I. -Ithird_party/libretro-common/include tools/tests/core_shader_labels_test.c -o "$out/test"
"$out/test"
