#!/bin/sh
# Serial CPU-only actual trace and hostile-VFS checks. No graphics device.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
c=third_party/libretro-common
out=${TEST_BUILD_DIR:-build/tests/gpu-trace}
mkdir -p "$out"
out=$(CDPATH= cd -- "$out" && pwd)
san=${SANITIZER--fsanitize=address,undefined}
flags="-O2 -g -D_GNU_SOURCE -DHAVE_THREADS -DHAVE_THREAD_STORAGE"
libs="-pthread -lm"
extra=
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) libs="$libs -lwinmm"; extra=-Wno-long-long ;; *) flags="$flags -DHAVE_MMAP" ;; esac
inc="-I. -Isrc -Ilibretro -I$c/include -Ithird_party/fmt/include -Ithird_party/snappy"
# The build commit only identifies the fixture; no generated header is required.
echo '#define XE_BUILD_COMMIT "0123456789012345678901234567890123456789"' > "$out/version.h"
${CC:-cc} -std=c89 -O2 tools/build/core_codegen.c tools/build/core_deflate.c -o "$out/codegen"
platform=
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) platform=windows ;; esac
"$out/codegen" inputs "$out" $platform
# Keep the baseline and current fixture build identifier identical.
echo '#define XE_BUILD_COMMIT "0123456789012345678901234567890123456789"' > "$out/version.h"
inc="-I$out $inc"
for f in streams/file_stream.c vfs/vfs_implementation.c file/file_path.c file/file_path_io.c string/stdstring.c string/rstrtod.c encodings/encoding_utf.c compat/compat_strcasestr.c compat/compat_posix_string.c compat/fopen_utf8.c compat/compat_strl.c time/rtime.c rthreads/rthreads.c rthreads/retro_eventcount.c features/features_cpu.c encodings/encoding_xxh3.c encodings/encoding_xxh64.c; do
 ${CC:-cc} -std=c89 $flags $san $inc -c "$c/$f" -o "$out/$(basename "$f" .c).o"
done
for f in libretro/core_file.c libretro/core_cache_file.c tools/tests/core_gpu_trace_test.c; do
 ${CC:-cc} -std=c89 -pedantic -Wall -Wextra -Werror $extra $flags $san $inc -c "$f" -o "$out/$(basename "$f" .c).o"
done
for f in third_party/snappy/snappy.cc third_party/snappy/snappy-sinksource.cc third_party/snappy/snappy-stubs-internal.cc; do
 ${CXX:-c++} -std=c++20 -Wno-attributes $flags $san $inc -c "$f" -o "$out/$(basename "$f" .cc).o"
done
modes=current
if test -n "${BASELINE_COMMIT:-}"; then
 mkdir -p "$out/base/src/xenia/gpu"
 git show "$BASELINE_COMMIT:src/xenia/gpu/trace_writer.h" > "$out/base/src/xenia/gpu/trace_writer.h"
 git show "$BASELINE_COMMIT:src/xenia/gpu/trace_writer.cc" > "$out/base/src/xenia/gpu/trace_writer.cc"
 modes='current base'
fi
for mode in $modes; do
 src=src/xenia/gpu/trace_writer.cc
 native_inc="$inc"
 if test "$mode" = base; then
  src="$out/base/src/xenia/gpu/trace_writer.cc"
  native_inc="-DXE_TEST_TRACE_LEGACY -I$out/base/src $inc"
 fi
 ${CXX:-c++} -std=c++20 -Wno-attributes $flags $san -DNDEBUG -DXENIA_LIBRETRO -DXE_OPTION_GPU_TRACE $native_inc -c "$src" -o "$out/trace-writer.o"
 ${CXX:-c++} -std=c++20 -Wno-attributes $flags $san -DNDEBUG -DXENIA_LIBRETRO -DXE_OPTION_GPU_TRACE $native_inc -c tools/tests/core_gpu_trace_bridge.cc -o "$out/bridge.o"
 ${CXX:-c++} -static-libgcc -static-libstdc++ $san "$out"/*.o $libs -o "$out/test-$mode"
 if test "$mode" = base; then "$out/test-$mode" "$out/$mode.xtr" legacy
 else "$out/test-$mode" "$out/$mode.xtr"; fi
done
if test -n "${BASELINE_COMMIT:-}"; then
 cmp "$out/current.xtr" "$out/base.xtr"
 echo 'PASS: all-command trace bytes identical to archived production writer'
fi
