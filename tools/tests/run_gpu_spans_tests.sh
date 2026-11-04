#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/gpu-spans-$(uname -s)"}
mkdir -p "$out"
ext=
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) ext=.exe ;; esac
san=${SANITIZER--fsanitize=address,undefined}
${CC:-cc} -std=c89 -Wall -Wextra -Werror -O2 -g $san -I"$root/libretro" \
 "$root/libretro/core_gpu_spans.c" "$root/tools/tests/core_gpu_spans_test.c" -o "$out/test$ext"
"$out/test$ext"
