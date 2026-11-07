#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${TEST_BUILD_DIR:-"$root/build/tests/gpu-divide-$(uname -s)"}
mkdir -p "$out"
san=${SANITIZER--fsanitize=address,undefined}
extra=
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) extra=-Wno-long-long ;; esac
${CC:-cc} -std=c89 -O2 -g -pedantic -Wall -Wextra -Werror $extra $san \
 -Ilibretro tools/tests/core_gpu_divide_test.c -o "$out/test"
"$out/test"
