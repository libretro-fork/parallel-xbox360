#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${AUDIO_STATE_OUT:-build/tests/audio-driver-state}
mkdir -p "$out"
extra=
wrap=
if [ "${AUDIO_STATE_ALLOC_TEST:-0}" = 1 ]; then
 extra=-DXE_AUDIO_CHECKPOINT_ALLOC_TEST
 wrap=-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free
fi
${CC:-cc} -std=c89 -pedantic -Wno-long-long -O2 -Wall -Wextra -Werror \
 $extra -I. -Ithird_party/libretro-common/include -c tools/tests/core_audio_driver_state_test.c -o "$out/driver.o"
CXX="${CXX:-c++} $wrap $out/driver.o" TEST_SOURCE=tools/tests/audio_driver_state_bridge.cc \
 TEST_BUILD_DIR="$out" sh tools/tests/run_shader_publication_tests.sh
