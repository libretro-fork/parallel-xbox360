#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
FORWARD_FLOW_SOURCE=tools/tests/core_shader_analysis_text_test.c \
FORWARD_FLOW_BRIDGE=tools/tests/shader_analysis_text_bridge.cc \
FORWARD_FLOW_OUT=build/tests/analysis-text sh tools/tests/run_forward_flow_tests.sh
