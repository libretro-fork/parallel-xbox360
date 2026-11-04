#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/state-components"}
for component in memory_state scheduler_state delay_state timer_state audio_state; do
  TEST_SOURCE="$root/tools/tests/${component}_test.cc" \
  TEST_BUILD_DIR="$out/$component" \
    sh "$root/tools/tests/run_shader_publication_tests.sh"
done
