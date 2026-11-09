#!/bin/sh
set -eu
export FORWARD_FLOW_OUT=${QUEUE_SERVICES_OUT:-build/tests/queue-services}
export FORWARD_FLOW_SOURCE=tools/tests/core_queue_services_test.c
export FORWARD_FLOW_BRIDGE=tools/tests/queue_services_bridge.cc
sh "$(dirname -- "$0")/run_forward_flow_tests.sh"
