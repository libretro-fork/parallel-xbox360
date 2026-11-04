#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
c="$root/third_party/libretro-common"
out=${TEST_BUILD_DIR:-"$root/build/tests/notice-$(uname -s)"}
mkdir -p "$out"
san=${SANITIZER--fsanitize=address,undefined}
flags="-std=c89 -O2 -g -D_GNU_SOURCE -DHAVE_THREADS -DHAVE_THREAD_STORAGE"
libs="-pthread"
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) libs="$libs -lwinmm" ;; esac
${CC:-cc} $flags $san -I"$c/include" -c "$c/rthreads/rthreads.c" -o "$out/rthreads.o"
${CC:-cc} $flags $san -Wall -Wextra -Werror -I"$c/include" -I"$root/libretro" \
 "$root/libretro/core_events.c" "$root/tools/tests/core_notice_test.c" "$out/rthreads.o" $libs -o "$out/test"
"$out/test"
