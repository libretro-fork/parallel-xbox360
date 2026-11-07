#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${TEST_BUILD_DIR:-"$root/build/tests/spv-image-sink-$(uname -s)"}
mkdir -p "$out/modules"
san=${SANITIZER--fsanitize=address,undefined}
extra=
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) extra=-Wno-long-long ;; esac
${CC:-cc} -std=c89 -O2 -g -pedantic -Wall -Wextra -Werror $extra $san \
 -Ilibretro -Ithird_party/libretro-common/include \
 -include tools/tests/core_spv_image_sink_alloc.h \
 tools/tests/core_spv_image_sink_test.c libretro/core_spv_image_sink.c \
 libretro/core_spv_binary.c libretro/core_spv_size.c -o "$out/test"
"$out/test" "$out/modules"
if test -n "${SPIRV_VALIDATOR:-}"; then
 for file in "$out"/modules/*.spv; do "$SPIRV_VALIDATOR" --target-env vulkan1.2 "$file"; done
 echo 'PASS: validation of every rewritten texture-load template'
fi
