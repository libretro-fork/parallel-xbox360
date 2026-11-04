#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
c="$root/third_party/libretro-common"
out=${TEST_BUILD_DIR:-"$root/build/tests/cache-file-$(uname -s)"}
mkdir -p "$out"
san=${SANITIZER--fsanitize=address,undefined}
flags="-std=c89 -O1 -g -D_GNU_SOURCE -DHAVE_THREADS -DHAVE_THREAD_STORAGE"
libs="-pthread -lm"
ext=
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) flags="$flags -Wno-cast-function-type"; libs="$libs -lwinmm"; ext=.exe ;; *) flags="$flags -DHAVE_MMAP" ;; esac
for f in streams/file_stream.c vfs/vfs_implementation.c file/file_path.c file/file_path_io.c string/stdstring.c string/rstrtod.c encodings/encoding_utf.c compat/compat_strcasestr.c compat/compat_posix_string.c compat/fopen_utf8.c compat/compat_strl.c time/rtime.c rthreads/rthreads.c rthreads/retro_eventcount.c features/features_cpu.c encodings/encoding_xxh3.c encodings/encoding_xxh64.c; do
 ${CC:-cc} $flags $san -I"$c/include" -c "$c/$f" -o "$out/$(basename "$f" .c).o"
done
for f in "$root/libretro/core_cache_file.c" "$root/tools/tests/core_cache_file_test.c"; do
 ${CC:-cc} $flags $san -DXE_TEST_SHADER_STORAGE -Wall -Wextra -Werror -I"$c/include" -I"$root/libretro" -c "$f" -o "$out/$(basename "$f" .c).o"
done
${CXX:-c++} -std=c++20 -O1 -g $san -DNDEBUG -DXENIA_LIBRETRO=1 -I"$root" -I"$root/src" -I"$c/include" -I"$root/third_party/fmt/include" -c "$root/tools/tests/core_cache_reader_bridge.cc" -o "$out/reader-bridge.o"
${CXX:-c++} $san "$out"/*.o $libs -o "$out/test$ext"
"$out/test$ext" "$out/native-cache.dat"
