#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/gpu-transfer"}
mkdir -p "$out"
exe=transfer-test
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) exe=transfer-test.exe ;; esac
san=${SANITIZER--fsanitize=address,undefined}
${CC:-cc} -std=c89 -pedantic -Wall -Wextra -Werror -O2 -g \
  $san -I"$root" "$root/tools/tests/core_gpu_transfer_test.c" \
  "$root/libretro/core_gpu_worklist.c" -o "$out/$exe"
"$out/$exe"
