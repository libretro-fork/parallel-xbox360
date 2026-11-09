#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${TEST_BUILD_DIR:-build/tests/draw-contract}
mkdir -p "$out"
san=${SANITIZER--fsanitize=address,undefined}
flags="-std=c89 -pedantic -Wno-long-long -Wall -Wextra -Werror -O1 -g -ffunction-sections -fdata-sections -D_GNU_SOURCE -DHAVE_THREADS -DHAVE_THREAD_STORAGE -DRETRO_ATOMIC_FORCE_GCC_NEW"
inc="-I. -Isrc -Itools/tests -Ithird_party/libretro-common/include"
${CC:-cc} $flags $san $inc tools/tests/core_draw_stats_test.c -o "$out/release"
"$out/release"
objects=""
for source in libretro/core_draw_stats.c libretro/core_job_pool.c tools/tests/core_draw_stats_test.c  third_party/libretro-common/rthreads/rthreads.c third_party/libretro-common/rthreads/retro_eventcount.c  third_party/libretro-common/features/features_cpu.c third_party/libretro-common/hash/lrc_hash.c; do
 object="$out/$(basename "$source" .c).o"
 objects="$objects $object"
 ${CC:-cc} $flags $san $inc -DXE_GPU_DRAW_STATS=1 -c "$source" -o "$object"
done
${CXX:-c++} -std=c++20 -O1 -g $san $inc -DXE_GPU_DRAW_STATS=1 -DRETRO_ATOMIC_FORCE_GCC_NEW  -c tools/tests/draw_contract_bridge.cc -o "$out/bridge.o"
${CXX:-c++} $san $objects "$out/bridge.o" -Wl,--gc-sections -pthread -lm -o "$out/test"
"$out/test"
set +e
"$out/test" --negative
status=$?
set -e
test "$status" -eq 1
