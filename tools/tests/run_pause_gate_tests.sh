#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/pause-$(uname -s)"}
mkdir -p "$out"
exe=pause-test
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) exe=pause-test.exe ;; esac
san=${SANITIZER--fsanitize=address,undefined}
${CC:-cc} -std=c89 -Wall -Wextra -Werror -O1 -g \
  -DHAVE_THREADS -DHAVE_THREAD_STORAGE $san \
  -I"$root" -I"$root/third_party/libretro-common/include" \
  "$root/libretro/core_pause_gate.c" "$root/tools/tests/core_pause_gate_test.c" \
  "$root/third_party/libretro-common/rthreads/rthreads.c" \
  "$root/third_party/libretro-common/rthreads/retro_eventcount.c" \
  -pthread -o "$out/$exe"
"$out/$exe"
