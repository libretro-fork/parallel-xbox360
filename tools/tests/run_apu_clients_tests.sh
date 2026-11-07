#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${APU_CLIENTS_OUT:-build/tests/apu-clients}
mkdir -p "$out"
ext=
platform_flags=-D_GNU_SOURCE
libs=-pthread
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) ext=.exe; platform_flags=-Wno-error=cast-function-type; libs="-pthread -lwinmm" ;; esac
san=${SANITIZER--fsanitize=address,undefined}
common=third_party/libretro-common
flags="-std=c89 -pedantic -Wno-long-long -Wall -Wextra -Werror -O2 -g $san $platform_flags -DHAVE_THREADS -DHAVE_THREAD_STORAGE -DRETRO_ATOMIC_FORCE_GCC_NEW -DXE_APU_CLIENT_TEST -I. -I$common/include"
${CC:-cc} $flags -Dcpu_features_get_time_usec=xe_apu_test_time -c libretro/core_apu_clients.c -o "$out/clients.o"
${CC:-cc} $flags tools/tests/core_apu_clients_test.c "$out/clients.o" \
 "$common/rthreads/rthreads.c" "$common/rthreads/retro_eventcount.c" \
 "$common/features/features_cpu.c" "$common/compat/compat_strl.c" "$common/time/rtime.c" \
 $libs -o "$out/test$ext"
"$out/test$ext"
