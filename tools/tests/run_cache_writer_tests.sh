#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/cache-writer-$(uname -s)"}
mkdir -p "$out"
exe=cache-writer-test
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) exe=cache-writer-test.exe ;; esac
san=${SANITIZER--fsanitize=address,undefined}
platform_flags=-D_GNU_SOURCE
libs=-pthread
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) platform_flags=-Wno-error=cast-function-type; libs="-pthread -lwinmm" ;; esac
${CC:-cc} -std=c89 -Wall -Wextra -Werror -O1 -g \
  -DHAVE_THREADS -DHAVE_THREAD_STORAGE $san $platform_flags \
  -I"$root" -I"$root/third_party/libretro-common/include" \
  "$root/libretro/core_cache_writer.c" "$root/tools/tests/core_cache_writer_test.c" \
  "$root/third_party/libretro-common/queues/retro_spsc.c" \
  "$root/third_party/libretro-common/rthreads/rthreads.c" \
  "$root/third_party/libretro-common/rthreads/retro_eventcount.c" \
  $libs -o "$out/$exe"
"$out/$exe"
