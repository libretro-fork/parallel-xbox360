#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${VARIANT_INDEX_OUT:-build/tests/shader-variant-index}
mkdir -p "$out"
${CC:-cc} -std=c89 -pedantic -Wno-long-long -Wdeclaration-after-statement -Wall -Wextra -Werror -O2 \
 ${SANITIZER--fsanitize=address,undefined} -I. "${VARIANT_INDEX_UNIT:-tools/tests/core_shader_variant_index_test.c}" -o "$out/unit"
"$out/unit"
SPIRV_VALIDATOR= \
 FORWARD_FLOW_BRIDGE="${VARIANT_INDEX_BRIDGE:-tools/tests/shader_variant_index_bridge.cc}" \
 FORWARD_FLOW_SOURCE="${VARIANT_INDEX_SOURCE:-tools/tests/core_shader_variant_owner_test.c}" \
 FORWARD_FLOW_OUT="$out/native" \
 sh tools/tests/run_forward_flow_tests.sh
