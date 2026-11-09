#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/mpsc-records-$(uname -s)"}
mkdir -p "$out"
exe=mpsc-records-test
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) exe=mpsc-records-test.exe ;; esac
san=${SANITIZER--fsanitize=address,undefined}
platform_flags=-D_GNU_SOURCE
libs=-pthread
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) platform_flags=-Wno-error=cast-function-type; libs="-pthread -lwinmm" ;; esac
${CC:-cc} -std=c89 -Wall -Wextra -Werror -O1 -g \
  -DXE_MPSC_RECORDS_TEST -DHAVE_THREADS -DHAVE_THREAD_STORAGE $san $platform_flags \
  -I"$root" -I"$root/third_party/libretro-common/include" \
  "$root/libretro/core_mpsc_records.c" "$root/tools/tests/core_mpsc_records_test.c" \
  "$root/third_party/libretro-common/rthreads/rthreads.c" \
  "$root/third_party/libretro-common/rthreads/retro_eventcount.c" \
  $libs -o "$out/$exe"
"$out/$exe"
