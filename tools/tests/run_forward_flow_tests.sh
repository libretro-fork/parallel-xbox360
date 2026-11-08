#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${FORWARD_FLOW_OUT:-build/tests/forward-flow}
mkdir -p "$out"
${CC:-cc} -std=c89 -pedantic -Wno-long-long -O2 -Wall -Wextra -Werror \
 -DHAVE_THREADS -DHAVE_THREAD_STORAGE -DRETRO_ATOMIC_FORCE_GCC_NEW -I. -Ithird_party/libretro-common/include -c "${FORWARD_FLOW_SOURCE:-tools/tests/core_forward_flow_test.c}" -o "$out/driver.o"
CXX="${CXX:-c++} $out/driver.o" TEST_SOURCE=tools/tests/forward_flow_bridge.cc \
 TEST_ARGUMENT="$out" TEST_BUILD_DIR="$out" sh tools/tests/run_shader_publication_tests.sh

if test -n "${SPIRV_VALIDATOR:-}"; then
 for file in "$out"/*.spv; do
  "$SPIRV_VALIDATOR" --target-env vulkan1.1 "$file"
 done
fi
