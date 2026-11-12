#!/bin/sh
set -eu
export CC="${CC:-cc} -DXE_PRIMITIVE_NATIVE"
export FORWARD_FLOW_SOURCE=tools/tests/core_primitive_builtin_test.c
export FORWARD_FLOW_BRIDGE=tools/tests/primitive_counts_bridge.cc
export FORWARD_FLOW_OUT=${PRIMITIVE_COUNTS_OUT:-build/tests/primitive-counts}
unset SPIRV_VALIDATOR
exec sh "$(dirname -- "$0")/run_forward_flow_tests.sh"
