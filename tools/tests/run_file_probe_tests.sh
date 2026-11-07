#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${FILE_PROBE_OUT:-build/tests/file-probe}
mkdir -p "$out"
${CC:-cc} -std=c89 -pedantic -Wno-long-long -O2 -Wall -Wextra -Werror -DXE_PROBE_NATIVE \
 -I. -Ithird_party/libretro-common/include -c tools/tests/core_file_probe_test.c -o "$out/driver.o"
CXX="${CXX:-c++} $out/driver.o" TEST_SOURCE=tools/tests/file_probe_bridge.cc \
 TEST_BUILD_DIR="$out" sh tools/tests/run_shader_publication_tests.sh
