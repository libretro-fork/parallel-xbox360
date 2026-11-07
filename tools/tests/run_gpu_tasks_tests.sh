#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${TEST_BUILD_DIR:-build/tests/gpu-tasks}
mkdir -p "$out"
san=${SANITIZER--fsanitize=address,undefined}
flags='-std=c89 -O2 -g -D_GNU_SOURCE -DHAVE_THREADS -DHAVE_THREAD_STORAGE -DRETRO_ATOMIC_FORCE_GCC_NEW'
extra=
libs=-pthread
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) extra=-Wno-long-long;libs="$libs -lwinmm" ;; esac
inc='-Ithird_party/libretro-common/include -Ilibretro'
${CC:-cc} $flags $san $inc -c third_party/libretro-common/rthreads/rthreads.c -o "$out/rthreads.o"
${CC:-cc} $flags $san $extra -Wall -Wextra -Werror -pedantic $inc -include tools/tests/core_gpu_tasks_hook.h -c libretro/core_gpu_tasks.c -o "$out/tasks.o"
${CC:-cc} $flags $san $extra -Wall -Wextra -Werror -pedantic $inc -c tools/tests/core_gpu_tasks_test.c -o "$out/test.o"
${CXX:-c++} -std=c++20 -O2 -g $san $inc -c tools/tests/core_gpu_tasks_bridge.cc -o "$out/bridge.o"
${CXX:-c++} -static-libgcc -static-libstdc++ $san "$out/bridge.o" "$out/test.o" "$out/tasks.o" "$out/rthreads.o" $libs -o "$out/test"
"$out/test"
