#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/spv-export-reference"}
mkdir -p "$out"
out=$(CDPATH= cd -- "$out" && pwd)
platform=${PLATFORM:-linux-x86_64}
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) platform=${PLATFORM:-windows-x86_64} ;; esac
revision=${REFERENCE_REVISION:-205d19125207a9600e391b6cea3c72ee550e1ee5}
git -C "$root" show "$revision:src/xenia/gpu/spirv_shader_translator_memexport.cc" > "$out/reference.cc"
cd "$root"
${MAKE:-make} -f Makefile -f tools/tests/Makefile \
  "platform=$platform" "REFERENCE_SOURCE=$out/reference.cc" \
  "REFERENCE_OBJECT=$out/reference.o" spv-export-reference
echo "Reference object: $out/reference.o (historical algorithm, current headers)"
