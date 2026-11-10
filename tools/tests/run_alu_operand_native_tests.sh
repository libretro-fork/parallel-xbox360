#!/bin/sh
set -eu
FORWARD_FLOW_SOURCE=tools/tests/core_alu_operand_native_test.c \
 FORWARD_FLOW_BRIDGE=tools/tests/alu_operand_bridge.cc \
 FORWARD_FLOW_OUT=${ALU_OPERAND_OUT:-build/tests/alu-operand-native} \
 sh "$(dirname -- "$0")/run_forward_flow_tests.sh"
