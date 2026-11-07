#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${BINDING_STORAGE_OUT:-build/tests/binding-storage}
mkdir -p "$out"
${CC:-cc} -std=c89 -pedantic -Wno-long-long -O2 -g -Wall -Wextra -Werror \
 ${SANITIZER--fsanitize=address,undefined} -I. \
 tools/tests/core_binding_storage_test.c -o "$out/test.exe"
"$out/test.exe"
