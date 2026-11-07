#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${FLOAT_RTZ_OUT:-build/tests/float-rtz}
mkdir -p "$out"
ext=
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) ext=.exe ;; esac
${CC:-cc} -std=c89 -pedantic -Wno-long-long -Wall -Wextra -Werror -O2 -fno-fast-math \
 ${SANITIZER--fsanitize=address,undefined} -I. -Ithird_party/libretro-common/include \
 tools/tests/core_float_rtz_test.c -o "$out/test$ext"
"$out/test$ext"
