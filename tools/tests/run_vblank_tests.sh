#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${TEST_BUILD_DIR:-build/tests/vblank-state}
mkdir -p "$out"
san=${SANITIZER--fsanitize=address,undefined}
libs=-pthread
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) libs="$libs -lwinmm" ;; esac
flags='-std=c89 -O2 -g -ffunction-sections -D_GNU_SOURCE -DHAVE_THREADS -DHAVE_THREAD_STORAGE'
inc='-I. -Ithird_party/libretro-common/include'
${CC:-cc} $flags $san -Wall -Wextra -Werror $inc tools/tests/core_state_budget_test.c -o "$out/budget"
"$out/budget"
${CC:-cc} $flags $san $inc libretro/core_vblank.c libretro/core_pause_gate.c tools/tests/core_vblank_test.c \
 third_party/libretro-common/rthreads/rthreads.c third_party/libretro-common/rthreads/retro_eventcount.c \
 third_party/libretro-common/features/features_cpu.c third_party/libretro-common/compat/compat_strl.c \
 third_party/libretro-common/time/rtime.c -Wl,--gc-sections $libs -o "$out/vblank"
"$out/vblank"
