#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${TEST_BUILD_DIR:-build/tests/job-wake}
mkdir -p "$out"
san=${SANITIZER--fsanitize=address,undefined}
${CC:-cc} -std=c89 -pedantic -Wno-long-long -Wall -Wextra -Werror -O1 -g $san \
 -D_GNU_SOURCE -DHAVE_THREADS -DHAVE_THREAD_STORAGE -DRETRO_ATOMIC_FORCE_GCC_NEW \
 -I. -Ithird_party/libretro-common/include tools/tests/core_job_wake_test.c \
 third_party/libretro-common/rthreads/rthreads.c third_party/libretro-common/rthreads/retro_eventcount.c \
 -pthread -o "$out/test"
"$out/test"
${CC:-cc} -std=c89 -pedantic -Wno-long-long -Wall -Wextra -Werror -O1 -g $san \
 -D_GNU_SOURCE -DHAVE_THREADS -DHAVE_THREAD_STORAGE -DRETRO_ATOMIC_FORCE_GCC_NEW \
 -I. -Ithird_party/libretro-common/include tools/tests/core_job_wake_handles_test.c \
 third_party/libretro-common/rthreads/rthreads.c third_party/libretro-common/rthreads/retro_eventcount.c \
 -pthread -o "$out/handles"
"$out/handles"
