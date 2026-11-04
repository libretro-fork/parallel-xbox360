#!/bin/sh
set -eu
: "${XMA_INPUT_RAW:?Set to the data chunk of FFmpeg samples/game-formats/xma/Title_Call.xma}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
export TEST_SOURCE="$root/tools/tests/xma_stream_test.cc"
export TEST_BUILD_DIR="${TEST_BUILD_DIR:-$root/build/tests/xma-stream}"
exec sh "$root/tools/tests/run_shader_publication_tests.sh"
