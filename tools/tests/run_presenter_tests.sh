#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/presenter"}
mkdir -p "$out"
cc=${CC:-cc}
cxx=${CXX:-c++}
san=${SANITIZER--fsanitize=address,undefined}
inc="-I$root -I$root/src -I$root/third_party -I$root/third_party/libretro-common/include"
flags="-O1 -g -DNDEBUG -DHAVE_THREADS -DHAVE_THREAD_STORAGE $san -fno-omit-frame-pointer"
exe=presenter-test
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) exe=presenter-test.exe ;; esac
# Intentional word splitting for compiler flags; paths must have no spaces.
$cc -std=c89 -Wall -Wextra -Werror -pedantic $flags $inc -c \
  "$root/tools/tests/core_presenter_test.c" -o "$out/driver.o"
$cc $flags $inc -c "$root/third_party/libretro-common/rthreads/rthreads.c" \
  -o "$out/rthreads.o"
$cxx -std=c++20 -Wall -Wextra -Werror $flags $inc \
  "$root/src/xenia/ui/presenter.cc" "$root/tools/tests/core_presenter_bridge.cc" \
  "$out/driver.o" "$out/rthreads.o" -pthread -o "$out/$exe"
"$out/$exe"
