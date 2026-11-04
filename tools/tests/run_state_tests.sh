#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
common="$root/third_party/libretro-common"
out=${TEST_BUILD_DIR:-"$root/build/tests/state-$(uname -s)"}
mkdir -p "$out"
san=${SANITIZER--fsanitize=address,undefined}
flags="-std=c89 -O1 -g -D_GNU_SOURCE -DHAVE_THREADS -DHAVE_THREAD_STORAGE"
libs="-pthread"
${CC:-cc} -std=c89 -pedantic -Wall -Wextra -Werror $san -I"$root" \
  "$root/tools/tests/core_log_line_test.c" -o "$out/log-line"
"$out/log-line"
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) libs="$libs -lwinmm" ;; esac
${CC:-cc} $flags $san -pedantic -Wall -Wextra -Werror -I"$root" -I"$common/include" \
  "$root/tools/tests/core_state_wait_test.c" -o "$out/wait"
"$out/wait"
${CC:-cc} $flags $san -Wall -Wextra -Werror -I"$root" -I"$common/include" \
  "$root/tools/tests/core_state_io_test.c" -o "$out/cursor"
"$out/cursor"
for source in rthreads/rthreads.c rthreads/retro_eventcount.c features/features_cpu.c \
  compat/compat_strl.c time/rtime.c; do
  ${CC:-cc} $flags $san -ffunction-sections -I"$common/include" -c "$common/$source" \
    -o "$out/$(basename "$source" .c).o"
done
${CC:-cc} $flags $san -Wall -Wextra -Werror -Wl,--gc-sections \
  -I"$root" -I"$common/include" "$root/tools/tests/core_pause_snapshot_test.c" \
  "$root/libretro/core_pause_gate.c" "$out/"*.o $libs -o "$out/pause"
"$out/pause"
${CXX:-c++} -std=c++20 -O1 -g $san -I"$root" -I"$root/src" \
  -I"$common/include" -I"$root/third_party/fmt/include" \
  "$root/tools/tests/byte_stream_state_test.cc" "$root/src/xenia/base/byte_stream.cc" \
  -o "$out/stream"
"$out/stream"
