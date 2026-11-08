#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${TEXTURE_SAMPLING_OUT:-build/tests/texture-sampling}
FORWARD_FLOW_BRIDGE=tools/tests/texture_sampling_bridge.cc FORWARD_FLOW_SOURCE=tools/tests/core_texture_sampling_fixture.c FORWARD_FLOW_OUT="$out" sh tools/tests/run_forward_flow_tests.sh
# Generation/validation can run in ordinary post-build CI without GPU support.
if test "${GPU_TESTS:-0}" = 1; then
 libs=-ldl
 case $(uname -s) in MINGW*|MSYS*|CYGWIN*) libs= ;; esac
 ${CC:-cc} -std=c89 -pedantic -Wno-long-long -Wdeclaration-after-statement -Wall -Wextra -Werror -O2 \
  -I. -isystem third_party/Vulkan-Headers/include tools/tests/core_texture_sampling_vulkan_test.c -o "$out/runtime" $libs
 "$out/runtime" "$out"
fi
