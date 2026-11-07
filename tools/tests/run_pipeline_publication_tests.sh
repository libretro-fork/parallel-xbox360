#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${PIPELINE_PUB_OUT:-build/tests/pipeline-publication}
mkdir -p "$out"
ext=
platform_flags=-D_GNU_SOURCE
libs=-pthread
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) ext=.exe; platform_flags=-Wno-error=cast-function-type; libs="-pthread -lwinmm" ;; esac
san=${SANITIZER--fsanitize=address,undefined}
common=third_party/libretro-common
flags="-std=c89 -pedantic -Wno-long-long -Wall -Wextra -Werror -O2 -g $san $platform_flags -DHAVE_THREADS -DHAVE_THREAD_STORAGE -DRETRO_ATOMIC_FORCE_GCC_NEW -I. -I$common/include"
${CC:-cc} $flags tools/tests/core_pipeline_publication_test.c \
 "$common/rthreads/rthreads.c" "$common/rthreads/retro_eventcount.c" \
 "$common/features/features_cpu.c" "$common/compat/compat_strl.c" "$common/time/rtime.c" \
 $libs -o "$out/test$ext"
"$out/test$ext"
