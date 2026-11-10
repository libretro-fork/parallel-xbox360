#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${BINARY_SLOT_OUT:-build/tests/binary-slot}
mkdir -p "$out"
${CC:-cc} -std=c89 -pedantic -Wno-long-long -O2 -Wall -Wextra -Werror -DXE_BINARY_NATIVE \
 -DHAVE_THREADS -DHAVE_THREAD_STORAGE -DRETRO_ATOMIC_FORCE_GCC_NEW -I. -Ithird_party/libretro-common/include -c tools/tests/core_binary_slot_test.c -o "$out/driver.o"
extra=
case $(uname -s) in
 MINGW*|MSYS*|CYGWIN*) ;;
 *)
  # Non-Windows cores omit DXIL generation. Link its real binary owner only.
  ${CC:-cc} -std=gnu89 -O2 -ffunction-sections -fdata-sections \
   -Isrc/xenia/gpu/dxil/bytecode -c src/xenia/gpu/dxil/shader_module.c -o "$out/module.o"
  extra="$out/module.o -Wl,--gc-sections"
 ;;
esac
CXX="${CXX:-c++} $out/driver.o $extra" TEST_SOURCE=tools/tests/binary_slot_bridge.cc \
 TEST_BUILD_DIR="$out" sh tools/tests/run_shader_publication_tests.sh
