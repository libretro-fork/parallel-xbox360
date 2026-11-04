#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
export TEST_SOURCE="$root/tools/tests/kernel_reference_test.cc"
export TEST_BUILD_DIR=${TEST_BUILD_DIR:-"$root/build/tests/kernel-reference"}
exec sh "$root/tools/tests/run_shader_publication_tests.sh"
