#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${PIPELINE_PUB_OUT:-build/tests/pipeline-publication-native}
mkdir -p "$out"
extra=
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) ;; *) extra=-DXE_PIPELINE_VULKAN_ONLY ;; esac
if [ "${PIPELINE_PUB_VULKAN_ONLY:-0}" = 1 ]; then extra=-DXE_PIPELINE_VULKAN_ONLY; fi
${CC:-cc} -std=c89 -pedantic -Wno-long-long -O2 -Wall -Wextra -Werror \
 $extra -DXE_PIPELINE_NATIVE_TEST -DHAVE_THREADS -DHAVE_THREAD_STORAGE -DRETRO_ATOMIC_FORCE_GCC_NEW -I. -Ithird_party/libretro-common/include -c tools/tests/core_pipeline_publication_test.c -o "$out/driver.o"
CXX="${CXX:-c++} $extra -Ithird_party -Ithird_party/Vulkan-Headers/include -DVULKAN_HPP_NO_TO_STRING $out/driver.o" TEST_SOURCE=tools/tests/pipeline_publication_bridge.cc \
 TEST_BUILD_DIR="$out" sh tools/tests/run_shader_publication_tests.sh
