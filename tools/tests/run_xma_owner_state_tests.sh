#!/bin/sh
set -eu
: "${XMA_INPUT_RAW:?Set the local Title_Call.xma data chunk; see xma_stream_test.md}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${XMA_OWNER_OUT:-build/tests/xma-owner-state}
mkdir -p "$out"
${CC:-cc} -std=c89 -pedantic -Wno-long-long -O2 -Wall -Wextra -Werror \
 -I. -Ithird_party/libretro-common/include -c tools/tests/core_xma_owner_state_test.c -o "$out/driver.o"
CXX="${CXX:-c++} $out/driver.o" TEST_SOURCE=tools/tests/xma_owner_state_bridge.cc \
 TEST_BUILD_DIR="$out" sh tools/tests/run_shader_publication_tests.sh
