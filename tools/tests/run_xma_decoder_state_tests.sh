#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${DECODER_STATE_OUT:-build/tests/xma-decoder-state}
mkdir -p "$out"
extra=
if [ "${DECODER_STATE_OPEN_FAILURE_TEST:-0}" = 1 ]; then
 extra=-DXE_XMA_OPEN_FAIL_TEST
 arch_flags=
 case $(uname -m) in x86_64|amd64) arch_flags=-mavx ;; esac
 ${CXX:-c++} -O2 $arch_flags -std=c++20 -Wno-attributes -DNDEBUG -DXENIA_LIBRETRO -DRETRO_ATOMIC_FORCE_GCC_NEW \
  -Davcodec_open2=xe_xma_test_open2 -I. -Isrc -Ithird_party/libretro-common/include \
  -Ithird_party/fmt/include -Ithird_party/FFmpeg -Ithird_party/ffmpeg-xenia \
  -c src/xenia/apu/xma_context_new.cc -o "$out/context.o"
fi
${CC:-cc} -std=c89 -pedantic -Wno-long-long -O2 -Wall -Wextra -Werror \
 $extra -I. -Ithird_party/libretro-common/include -c tools/tests/core_xma_decoder_state_test.c -o "$out/driver.o"
if [ -n "$extra" ]; then extra="$extra $out/context.o"; fi
CXX="${CXX:-c++} $extra $out/driver.o" TEST_SOURCE=tools/tests/xma_decoder_state_bridge.cc \
 TEST_BUILD_DIR="$out" sh tools/tests/run_shader_publication_tests.sh
