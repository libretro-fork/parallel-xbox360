#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${TEST_BUILD_DIR:-"$root/build/tests/source-views-$(uname -s)"}
mkdir -p "$out"
san=${SANITIZER--fsanitize=address,undefined}
extra=
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) extra=-Wno-long-long ;; esac
${CC:-cc} -std=c89 -O2 -g -pedantic -Wall -Wextra -Werror $extra $san \
 -Ilibretro -Ithird_party/libretro-common/include -isystem third_party/Vulkan-Headers/include \
 tools/tests/core_vk_source_views_test.c -o "$out/test"
"$out/test"
