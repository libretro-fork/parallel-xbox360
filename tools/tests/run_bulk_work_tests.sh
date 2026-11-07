#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${BULK_TEST_OUT:-build/tests/bulk-work}
mkdir -p "$out"
san=${SANITIZER--fsanitize=address,undefined}
platform_flags=-D_GNU_SOURCE
libs=-pthread
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) platform_flags=-Wno-error=cast-function-type; libs="-pthread -lwinmm" ;; esac
common=third_party/libretro-common
flags="-std=c89 -pedantic -Wno-long-long -Wall -Wextra -Werror -O2 -g $san $platform_flags -DHAVE_THREADS -DHAVE_THREAD_STORAGE -I. -I$common/include"
for mode in real fault; do
 extra=
 if test "$mode" = fault; then extra="-include tools/tests/core_bulk_fault.h"; fi
 ${CC:-cc} $flags $extra -c libretro/core_bulk_work.c -o "$out/$mode.o"
done
for f in rthreads/rthreads features/features_cpu compat/compat_strl time/rtime; do
 ${CC:-cc} $flags -c "$common/$f.c" -o "$out/$(basename "$f").o"
done
deps="$out/rthreads.o $out/features_cpu.o $out/compat_strl.o $out/rtime.o"
${CC:-cc} $flags tools/tests/core_bulk_work_test.c "$out/fault.o" $deps $libs -o "$out/test.exe"
"$out/test.exe"
${CC:-cc} $flags -c libretro/core_gpu_worklist.c -o "$out/storage.o"
${CXX:-c++} -std=c++20 -O2 -g $san -Wall -Wextra -Werror -I. -Isrc -I"$common/include" \
 tools/tests/core_bulk_work_bridge.cc "$out/real.o" "$out/storage.o" $deps $libs -o "$out/bridge.exe"
"$out/bridge.exe"
