#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
export TEST_SOURCE="$root/tools/tests/shared_memory_watch_test.cc"
export TEST_BUILD_DIR=${TEST_BUILD_DIR:-"$root/build/tests/shared-memory-watch"}
exec sh "$root/tools/tests/run_shader_publication_tests.sh"
