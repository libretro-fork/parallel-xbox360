#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${SVOD_OUT:-build/tests/svod}
mkdir -p "$out"
${CC:-cc} -std=c89 -pedantic -Wno-long-long -O2 -Wall -Wextra -Werror -DXE_SVOD_NATIVE \
 -DHAVE_THREADS -DHAVE_THREAD_STORAGE -DRETRO_ATOMIC_FORCE_GCC_NEW -I. -Ithird_party/libretro-common/include -c tools/tests/core_svod_test.c -o "$out/driver.o"
CXX="${CXX:-c++} $out/driver.o" TEST_SOURCE=tools/tests/svod_bridge.cc \
 TEST_BUILD_DIR="$out" TEST_ARGUMENT="$out/native-package" sh tools/tests/run_shader_publication_tests.sh
