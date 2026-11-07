#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${PIPELINE_RETIRE_OUT:-build/tests/pipeline-retire}
mkdir -p "$out"
san=${SANITIZER--fsanitize=address,undefined}
platform_flags=-D_GNU_SOURCE
libs=-pthread
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) platform_flags=-Wno-error=cast-function-type; libs="-pthread -lwinmm" ;; esac
common=third_party/libretro-common
${CC:-cc} -std=c89 -pedantic -Wno-long-long -O2 -g -Wall -Wextra -Werror \
 $san $platform_flags -DHAVE_THREADS -DHAVE_THREAD_STORAGE \
 -I. -I"$common/include" libretro/core_pipeline_retire.c \
 tools/tests/core_pipeline_retire_test.c "$common/rthreads/rthreads.c" \
 "$common/features/features_cpu.c" "$common/compat/compat_strl.c" \
 "$common/time/rtime.c" $libs -o "$out/test.exe"
"$out/test.exe"
