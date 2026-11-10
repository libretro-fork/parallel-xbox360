#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
FORWARD_FLOW_SOURCE=tools/tests/core_binding_translate_test.c \
FORWARD_FLOW_BRIDGE=tools/tests/binding_translate_bridge.cc \
FORWARD_FLOW_OUT=build/tests/binding-translate sh tools/tests/run_forward_flow_tests.sh
