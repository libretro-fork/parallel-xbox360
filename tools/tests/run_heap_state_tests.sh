#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${TEST_BUILD_DIR:-build/tests/heap-state}
mkdir -p "$out"
san=${SANITIZER--fsanitize=address,undefined}
extra=
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) extra=-Wno-long-long ;; esac
${CC:-cc} -std=c89 -O2 -g -pedantic -Wall -Wextra -Werror $extra $san \
 -I. -Ithird_party/libretro-common/include tools/tests/core_heap_state_test.c -o "$out/heap-plan"
"$out/heap-plan"
