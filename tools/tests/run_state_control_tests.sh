#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${TEST_BUILD_DIR:-build/tests/state-control}
mkdir -p "$out"
san=${SANITIZER--fsanitize=address,undefined}
libs=-pthread
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) libs="$libs -lwinmm" ;; esac
${CC:-cc} -std=c89 -O2 -g -ffunction-sections -D_GNU_SOURCE -DHAVE_THREADS -DHAVE_THREAD_STORAGE $san \
 -I. -Ithird_party/libretro-common/include libretro/core_state_control.c tools/tests/core_state_control_test.c \
 third_party/libretro-common/rthreads/rthreads.c third_party/libretro-common/features/features_cpu.c \
 third_party/libretro-common/compat/compat_strl.c third_party/libretro-common/time/rtime.c \
 -Wl,--gc-sections $libs -o "$out/control-check"
"$out/control-check"
