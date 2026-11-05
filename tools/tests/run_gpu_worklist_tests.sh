#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/gpu-worklist"}
mkdir -p "$out"
exe=worklist-test
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) exe=worklist-test.exe ;; esac
san=${SANITIZER--fsanitize=address,undefined}
${CC:-cc} -std=c89 -pedantic -Wall -Wextra -Werror -O2 -g \
  $san -I"$root" "$root/tools/tests/core_gpu_worklist_test.c" -o "$out/$exe"
"$out/$exe"
