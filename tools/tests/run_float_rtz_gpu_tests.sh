#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${FLOAT_RTZ_GPU_OUT:-build/tests/float-rtz-gpu}
FORWARD_FLOW_OUT="$out" FORWARD_FLOW_SOURCE=tools/tests/core_float_rtz_gpu_fixture.c \
 FORWARD_FLOW_BRIDGE=tools/tests/float_rtz_bridge.cc sh tools/tests/run_forward_flow_tests.sh
if [ "${GPU_TESTS:-0}" != 1 ]; then exit 0; fi
libs=-ldl
suffix=
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) libs=; suffix=.exe ;; esac
${CC:-cc} -std=c89 -pedantic -Wno-long-long -Wall -Wextra -Werror -O2 \
 -DXE_VERTEX_TEST_CASES=576 -DXE_FLOAT_RTZ_GPU_TEST -Itools/tests \
 -isystem third_party/Vulkan-Headers/include tools/tests/core_shader_call_vulkan_test.c \
 $libs -o "$out/runtime$suffix"
"$out/runtime$suffix" "$out"
