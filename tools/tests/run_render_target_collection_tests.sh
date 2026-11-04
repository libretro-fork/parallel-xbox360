#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
TEST_SOURCE="$root/tools/tests/render_target_collection_test.cc" \
TEST_BUILD_DIR="${TEST_BUILD_DIR:-$root/build/tests/render-target-collection}" \
  sh "$root/tools/tests/run_shader_publication_tests.sh"