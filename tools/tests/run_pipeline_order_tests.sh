#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/pipeline-order-$(uname -s)"}
mkdir -p "$out"
exe=pipeline-order-test
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) exe=pipeline-order-test.exe ;; esac
san=${SANITIZER--fsanitize=address,undefined}
${CC:-cc} -std=c89 -Wall -Wextra -Werror -O1 -g \
  -DHAVE_THREADS -DHAVE_THREAD_STORAGE $san \
  -I"$root" -I"$root/third_party/libretro-common/include" \
  "$root/libretro/core_pipeline_order.c" "$root/libretro/core_cache_writer.c" \
  "$root/tools/tests/core_pipeline_order_test.c" \
  "$root/third_party/libretro-common/queues/retro_spsc.c" \
  "$root/third_party/libretro-common/rthreads/rthreads.c" \
  "$root/third_party/libretro-common/rthreads/retro_eventcount.c" \
  -pthread -o "$out/$exe"
"$out/$exe"

# The production C++ shim only constructs/destroys payload objects in C89 slots.
${CC:-cc} -std=c89 -Wall -Wextra -Werror -O1 -g \
  -DHAVE_THREADS -DHAVE_THREAD_STORAGE $san \
  -I"$root" -I"$root/third_party/libretro-common/include" \
  -c "$root/libretro/core_pipeline_order.c" -o "$out/order.o"
for f in rthreads retro_eventcount; do
 ${CC:-cc} -std=c89 -O1 -g $san -DHAVE_THREADS -DHAVE_THREAD_STORAGE \
   -I"$root/third_party/libretro-common/include" \
   -c "$root/third_party/libretro-common/rthreads/$f.c" -o "$out/$f.o"
done
${CXX:-c++} -std=c++20 -O1 -g $san \
  -I"$root" -I"$root/src" -I"$root/third_party/libretro-common/include" \
  "$root/tools/tests/core_pipeline_order_bridge.cc" "$out/order.o" \
  "$out/rthreads.o" "$out/retro_eventcount.o" \
  -pthread -o "$out/bridge-$exe"
"$out/bridge-$exe"
