#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/spv-guest-workspace"}
mkdir -p "$out"
out=$(CDPATH= cd -- "$out" && pwd)
export TEST_BUILD_DIR="$out"
export TEST_SOURCE="$root/tools/tests/core_spv_guest_workspace_test.cc"
if test -n "${SPV_GUEST_CACHE:-}"; then
  export TEST_SOURCE="$root/tools/tests/core_spv_guest_cache_test.cc"
fi
export SPV_GUEST_OUTPUT="$out/guest.bin"
sh "$root/tools/tests/run_shader_publication_tests.sh"
if test -n "${SPV_GUEST_REFERENCE:-}"; then
  cmp "$SPV_GUEST_REFERENCE" "$SPV_GUEST_OUTPUT"
  echo "PASS: real translator output matches reference"
fi
