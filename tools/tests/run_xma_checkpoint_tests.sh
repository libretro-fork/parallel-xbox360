#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${XMA_CHECKPOINT_OUT:-build/tests/xma-checkpoint}
mkdir -p "$out"
${CC:-cc} -std=c89 -pedantic -Wno-long-long -O2 -Wall -Wextra -Werror \
 -Ithird_party/FFmpeg -c tools/tests/core_xma_checkpoint_test.c -o "$out/driver.o"
# Reuse the actual core link inputs. Build the matching core first.
CXX="${CXX:-c++} $out/driver.o" TEST_SOURCE=tools/tests/xma_checkpoint_bridge.cc \
 TEST_BUILD_DIR="$out" sh tools/tests/run_shader_publication_tests.sh
