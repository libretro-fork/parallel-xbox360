#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${NG2_SHADOW_OUT:-build/tests/ng2-shadow}
mkdir -p "$out"
${CC:-cc} -std=c89 -pedantic -Wno-long-long -O2 -Wall -Wextra -Werror \
 -DHAVE_THREADS -DHAVE_THREAD_STORAGE -DRETRO_ATOMIC_FORCE_GCC_NEW -I. -Ithird_party/libretro-common/include -c tools/tests/core_ng2_shadow_test.c -o "$out/driver.o"
CXX="${CXX:-c++} $out/driver.o" TEST_SOURCE=tools/tests/ng2_shadow_bridge.cc \
 TEST_BUILD_DIR="$out" sh tools/tests/run_shader_publication_tests.sh
