#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/presenter-bench"}
mkdir -p "$out"
out=$(CDPATH= cd -- "$out" && pwd)
source="$root/src/xenia/ui/presenter.cc"
inc="-I$root -I$root/src -I$root/third_party -I$root/third_party/libretro-common/include -I$root/tools/tests"
parent=
if test -n "${PRESENTER_REFERENCE_REVISION:-}"; then
  mkdir -p "$out/reference/src/xenia/ui"
  for file in presenter.cc presenter.h; do
    git -C "$root" show "$PRESENTER_REFERENCE_REVISION:src/xenia/ui/$file" > "$out/reference/src/xenia/ui/$file"
  done
  source="$out/reference/src/xenia/ui/presenter.cc"
  inc="-I$out/reference/src $inc"
  parent=-DXE_PRESENTER_PARENT
fi
flags="-O3 -DNDEBUG -DRETRO_ATOMIC_FORCE_GCC_NEW"
${CC:-cc} -std=c89 -Wall -Wextra -Werror -pedantic $flags $inc -c "$root/tools/tests/core_presenter_bench.c" -o "$out/driver.o"
${CC:-cc} -std=c89 $flags $inc -c "$root/tools/tests/core_pipeline_bench_profile.c" -o "$out/profile.o"
exe=test
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) exe=test.exe ;; esac
${CXX:-c++} -std=c++20 -Wall -Wextra -Werror $flags $parent $inc "$source" "$root/tools/tests/core_presenter_bench_bridge.cc" "$out/driver.o" "$out/profile.o" -pthread -o "$out/$exe"
"$out/$exe"
