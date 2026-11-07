#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/pipeline-handoff-$(uname -s)"}
mkdir -p "$out/base"
baseline=$(git -C "$root" rev-parse --verify "${BASELINE_COMMIT:-8ce2f22a}^{commit}")
for f in libretro/core_job_pool.c libretro/core_job_pool.h libretro/core_pipeline_order.c libretro/core_pipeline_order.h src/xenia/gpu/pipeline_creation_queue.h src/xenia/gpu/pipeline_publish_order.h; do
 mkdir -p "$out/base/$(dirname "$f")"
 git -C "$root" show "$baseline:$f" > "$out/base/$f"
done
if git -C "$root" cat-file -e "$baseline:libretro/core_completion.h" 2>/dev/null; then
 git -C "$root" show "$baseline:libretro/core_completion.h" > "$out/base/libretro/core_completion.h"
fi
flags="-std=c89 -O3 -D_GNU_SOURCE -DHAVE_THREADS -DHAVE_THREAD_STORAGE"
common="$root/third_party/libretro-common"
for f in rthreads retro_eventcount; do
 ${CC:-cc} $flags -I"$common/include" -c "$common/rthreads/$f.c" -o "$out/$f.o"
done
${CC:-cc} $flags -Wall -Wextra -Werror -c "$root/tools/tests/core_pipeline_bench_profile.c" -o "$out/profile.o"
for mode in baseline current; do
 src=$root
 if test "$mode" = baseline; then src="$out/base"; fi
 for f in core_job_pool core_pipeline_order; do
  ${CC:-cc} $flags -DXE_CAPTURE_ALLOCATIONS -include "$root/tools/tests/core_pipeline_bench_profile.h" \
   -I"$common/include" -c "$src/libretro/$f.c" -o "$out/$mode-$f.o"
 done
 ${CXX:-c++} -std=c++20 -O3 -I"$src" -I"$src/src" -I"$root/tools/tests" -I"$common/include" \
  "$root/tools/tests/core_pipeline_handoff_bench.cc" "$out/$mode-core_job_pool.o" \
  "$out/$mode-core_pipeline_order.o" "$out/profile.o" "$out/rthreads.o" "$out/retro_eventcount.o" \
  -pthread -o "$out/$mode.exe"
done
if test "${BUILD_ONLY:-0}" != 1; then
 "$out/baseline.exe" baseline
 "$out/current.exe" current
fi
