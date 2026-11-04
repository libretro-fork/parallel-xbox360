#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/gpu-retire"}
mkdir -p "$out"
exe=retire-test
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) exe=retire-test.exe ;; esac
san=${SANITIZER--fsanitize=address,undefined}
${CC:-cc} -std=c89 -pedantic -Wall -Wextra -Werror -O1 -g \
  $san -I"$root" "$root/libretro/core_gpu_retire.c" \
  "$root/tools/tests/core_gpu_retire_test.c" -o "$out/$exe"
"$out/$exe"
