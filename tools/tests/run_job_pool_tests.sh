#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/job-pool-$(uname -s)"}
mkdir -p "$out"
exe=job-pool-test
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) exe=job-pool-test.exe ;; esac
san=${SANITIZER--fsanitize=address,undefined}
${CC:-cc} -std=c89 -Wall -Wextra -Werror -O1 -g \
  -DHAVE_THREADS -DHAVE_THREAD_STORAGE $san \
  -I"$root" -I"$root/third_party/libretro-common/include" \
  "$root/libretro/core_job_pool.c" "$root/tools/tests/core_job_pool_test.c" \
  "$root/third_party/libretro-common/rthreads/rthreads.c" \
  "$root/third_party/libretro-common/rthreads/retro_eventcount.c" \
  -pthread -o "$out/$exe"

${CC:-cc} -std=c89 -Wall -Wextra -Werror -O2 -g \
  -DHAVE_THREADS -DHAVE_THREAD_STORAGE $san \
  -I"$root/libretro" -I"$root/third_party/libretro-common/include" \
  "$root/tools/tests/core_completion_test.c" \
  "$root/third_party/libretro-common/rthreads/rthreads.c" \
  -pthread -o "$out/completion-$exe"
"$out/completion-$exe"
"$out/$exe"

for f in "$root/libretro/core_job_pool.c" \
  "$root/third_party/libretro-common/rthreads/rthreads.c" \
  "$root/third_party/libretro-common/rthreads/retro_eventcount.c"; do
 ${CC:-cc} -std=c89 -O1 -g $san -DHAVE_THREADS -DHAVE_THREAD_STORAGE \
   -I"$root/third_party/libretro-common/include" -I"$root/libretro" \
   -c "$f" -o "$out/$(basename "$f" .c).o"
done
${CC:-cc} -std=c89 -Wall -Wextra -Werror -O1 -g $san -DHAVE_THREADS -DHAVE_THREAD_STORAGE \
  -I"$root" -I"$root/third_party/libretro-common/include" \
  -c "$root/tools/tests/core_pipeline_ready_test.c" -o "$out/ready.o"
${CXX:-c++} -std=c++20 -O1 -g $san \
  -I"$root" -I"$root/src" -I"$root/third_party/libretro-common/include" \
  "$root/tools/tests/core_job_pool_bridge.cc" "$root/tools/tests/core_pipeline_ready_bridge.cc" "$out/ready.o" "$out/core_job_pool.o" \
  "$out/rthreads.o" "$out/retro_eventcount.o" \
  -pthread -o "$out/bridge-$exe"
"$out/bridge-$exe"
