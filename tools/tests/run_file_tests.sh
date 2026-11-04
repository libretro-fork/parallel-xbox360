#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
c="$root/third_party/libretro-common"
out=${TEST_BUILD_DIR:-"$root/build/tests/mapped-file-$(uname -s)"}
mkdir -p "$out"
san=${SANITIZER--fsanitize=address,undefined}
flags="-std=c89 -O1 -g -D_GNU_SOURCE -DHAVE_THREADS -DHAVE_THREAD_STORAGE"
libs="-pthread -lm"
ext=
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) flags="$flags -Wno-cast-function-type"; libs="$libs -lwinmm"; ext=.exe ;; *) flags="$flags -DHAVE_MMAP" ;; esac
for f in streams/file_stream.c vfs/vfs_implementation.c file/file_path.c file/file_path_io.c string/stdstring.c string/rstrtod.c encodings/encoding_utf.c compat/compat_strcasestr.c compat/compat_posix_string.c compat/fopen_utf8.c compat/compat_strl.c time/rtime.c rthreads/rthreads.c rthreads/retro_eventcount.c features/features_cpu.c; do
 ${CC:-cc} $flags $san -I"$c/include" -c "$c/$f" -o "$out/$(basename "$f" .c).o"
done
for f in "$root/libretro/core_file.c" "$root/tools/tests/core_file_test.c"; do
 ${CC:-cc} $flags $san -Wall -Wextra -Werror -I"$c/include" -I"$root/libretro" -c "$f" -o "$out/$(basename "$f" .c).o"
done
${CC:-cc} $san "$out"/*.o $libs -o "$out/test$ext"
"$out/test$ext" "$out/native-input.dat"
