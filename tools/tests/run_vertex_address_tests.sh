#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
export FORWARD_FLOW_SOURCE=tools/tests/core_vertex_address_test.c
export FORWARD_FLOW_BRIDGE=tools/tests/vertex_address_bridge.cc
export FORWARD_FLOW_OUT=${VERTEX_ADDRESS_OUT:-build/tests/vertex-address}
if test "${VERTEX_ADDRESS_SANITIZE:-0}" = 1; then
 mkdir -p "$FORWARD_FLOW_OUT"
 flags='-fsanitize=undefined -fno-sanitize-recover=undefined'
 arch_flags=
 case $(${CXX:-c++} -dumpmachine) in x86_64*|amd64*) arch_flags=-mavx ;; esac
 # Link this instrumented production object before core.a, so the archive's
 # uninstrumented interpreter is not extracted. No test-only algorithm copy.
 ${CXX:-c++} $flags -std=c++20 $arch_flags -O1 -g -Wno-attributes \
  -DNDEBUG -DXENIA_LIBRETRO -DRETRO_ATOMIC_FORCE_GCC_NEW -I. -Isrc \
  -Ithird_party/libretro-common/include -Ithird_party/fmt/include \
  -c src/xenia/gpu/shader_interpreter.cc -o "$FORWARD_FLOW_OUT/interpreter.o"
 export CXX="${CXX:-c++} $flags $FORWARD_FLOW_OUT/interpreter.o"
 export CC="${CC:-cc} $flags"
fi
exec sh "$(dirname -- "$0")/run_forward_flow_tests.sh"
