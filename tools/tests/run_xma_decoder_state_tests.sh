#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${DECODER_STATE_OUT:-build/tests/xma-decoder-state}
mkdir -p "$out"
out=$(CDPATH= cd -- "$out" && pwd)
extra=
if [ "${DECODER_STATE_OPEN_FAILURE_TEST:-0}" = 1 ]; then
 extra=-DXE_XMA_OPEN_FAIL_TEST
fi
arch_flags=
case $(uname -m) in x86_64|amd64) arch_flags=-mavx ;; esac
for name in xma_context_new xma_decoder; do
 ${CXX:-c++} -O2 $arch_flags -std=c++20 -Wno-attributes -DNDEBUG -DXENIA_LIBRETRO -DRETRO_ATOMIC_FORCE_GCC_NEW \
  -I. -Isrc -Ithird_party/libretro-common/include -Ithird_party/fmt/include \
  \
  -c "src/xenia/apu/$name.cc" -o "$out/$name.o"
done
${CC:-cc} -std=c89 -pedantic -Wno-long-long -O2 -Wall -Wextra -Werror \
 -I. -Ithird_party/libretro-common/include -c libretro/core_xma_native.c -o "$out/native.o"
for name in pro standard; do
 ${CC:-cc} -std=c89 -pedantic -Wno-long-long -O2 -Wall -Wextra -Werror \
  -Dcalloc=xe_xma_test_calloc -Dfree=xe_xma_test_free \
  -Ithird_party/libretro-common/include \
  -c "third_party/libretro-common/formats/wma/rwma_$name.c" -o "$out/$name.o"
done
${CC:-cc} -std=c89 -pedantic -Wno-long-long -O2 -Wall -Wextra -Werror \
 $extra -I. -Ithird_party/libretro-common/include -c tools/tests/core_xma_decoder_state_test.c -o "$out/driver.o"
platform=${PLATFORM:-linux-x86_64}
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) platform=${PLATFORM:-windows-x86_64} ;; esac
objects=${CORE_OBJECTS:-"$root/build/make/$platform/objects.rsp"}
response_root=${CORE_OBJECT_ROOT:-$root}
response_out=$out
case $(uname -s) in
 MINGW*|MSYS*|CYGWIN*)
  response_root=$(cygpath -m "$response_root")
  response_out=$(cygpath -m "$out")
 ;;
esac
# Replace production codec objects with allocation-instrumented test objects.
awk -v base="$response_root" '
{
 while (match($0, /"[^"]+"/)) {
  path=substr($0,RSTART+1,RLENGTH-2)
  $0=substr($0,RSTART+RLENGTH)
  if (path ~ /\/(xma_context_new\.cc|xma_decoder\.cc|core_xma_native\.c|rwma_pro\.c|rwma_standard\.c)\.o$/) continue
  if (path !~ /^\// && path !~ /^[A-Za-z]:/) path=base "/" path
  print "\"" path "\""
 }
}' "$objects" > "$out/core.rsp"
for name in driver xma_context_new xma_decoder native pro standard; do
 printf '\"%s/%s.o\"\n' "$response_out" "$name"
done > "$out/objects.rsp"
cat "$out/core.rsp" >> "$out/objects.rsp"
CXX="${CXX:-c++} $extra" \
 CORE_OBJECTS="$out/objects.rsp" TEST_SOURCE=tools/tests/xma_decoder_state_bridge.cc \
 TEST_BUILD_DIR="$out" sh tools/tests/run_shader_publication_tests.sh
