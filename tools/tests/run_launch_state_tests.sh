#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/launch-state"}
mkdir -p "$out"
exe=test
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) exe=test.exe ;; esac
san=${SANITIZER--fsanitize=address,undefined}
inc="-I$root -I$root/third_party/libretro-common/include"
flags="-O2 -g -DHAVE_THREADS -DRETRO_ATOMIC_FORCE_GCC_NEW $san"
${CC:-cc} -std=c89 -pedantic -Wall -Wextra -Werror $flags $inc -c \
  "$root/tools/tests/core_launch_state_test.c" -o "$out/test.o"
${CC:-cc} $flags $inc -c "$root/third_party/libretro-common/rthreads/rthreads.c" -o "$out/threads.o"
${CC:-cc} $flags "$out/test.o" "$out/threads.o" -pthread -o "$out/$exe"
"$out/$exe"
