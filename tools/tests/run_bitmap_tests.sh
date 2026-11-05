#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/bitmap-$(uname -s)"}
mkdir -p "$out"
exe=bitmap-test
libs=-pthread
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) exe=bitmap-test.exe; libs="$libs -lwinmm" ;; esac
san=${SANITIZER--fsanitize=address,undefined}
${CC:-cc} -std=c89 -pedantic -Wall -Wextra -Werror -O2 -g \
  -D_GNU_SOURCE -DHAVE_THREADS -DHAVE_THREAD_STORAGE -DRETRO_ATOMIC_FORCE_GCC_NEW \
  $san -I"$root" -isystem "$root/third_party/libretro-common/include" \
  -c "$root/tools/tests/core_bitmap_test.c" -o "$out/test.o"
objects=
for name in rthreads/rthreads features/features_cpu compat/compat_strl time/rtime; do
  object="$out/$(basename "$name").o"
  ${CC:-cc} -std=c89 -O2 -g -D_GNU_SOURCE -DHAVE_THREADS -DHAVE_THREAD_STORAGE \
    -DRETRO_ATOMIC_FORCE_GCC_NEW $san -I"$root/third_party/libretro-common/include" \
    -c "$root/third_party/libretro-common/$name.c" -o "$object"
  objects="$objects $object"
done
# Dependency objects use the common sources' existing warning policy.
${CC:-cc} $san "$out/test.o" $objects $libs -o "$out/$exe"
"$out/$exe"
