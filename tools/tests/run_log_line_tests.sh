#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/log-line"}
mkdir -p "$out"
${CC:-cc} -std=c89 -pedantic -Wall -Wextra -Werror ${SANITIZER-} -I"$root" \
  "$root/tools/tests/core_log_line_test.c" -o "$out/test"
"$out/test"
