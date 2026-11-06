#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/resolve-readback-$(uname -s)"}
mkdir -p "$out"
cd "$root"
san=${SANITIZER--fsanitize=address,undefined}
# SDK headers use newer syntax; fixtures and helpers stay MSVC C89.
extra=
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) extra=-Wno-long-long ;; esac
${CC:-cc} -std=c89 -O1 -g -pedantic -Wall -Wextra -Werror $extra $san \
 -Ilibretro -Ithird_party/libretro-common/include \
 -isystem third_party/Vulkan-Headers/include \
 tools/tests/core_resolve_readback_test.c -o "$out/leaf"
"$out/leaf"
${CXX:-c++} -std=c++20 -O1 -g -Wall -Wextra -Werror $san \
 -Ilibretro -Ithird_party/libretro-common/include \
 tools/tests/core_resolve_readback_owner_test.cc -o "$out/owner"
"$out/owner"
