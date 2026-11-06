#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/upload-scan-$(uname -s)"}
mkdir -p "$out"
cd "$root"
san=${SANITIZER--fsanitize=address,undefined}
extra=
# LLP64 stdint expands UINT64_C with an unsigned-long-long suffix; it is the
# compiler's 64-bit extension in C89, also used by the native MSVC backend.
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) extra=-Wno-long-long ;; esac
${CC:-cc} -std=c89 -O1 -g -pedantic -Wall -Wextra -Werror $extra -DRETRO_ATOMIC_FORCE_GCC_NEW $san \
 -Ilibretro -Ithird_party/libretro-common/include tools/tests/core_upload_scan_test.c -o "$out/leaf"
"$out/leaf"
${CXX:-c++} -std=c++20 -O1 -g -Wall -Wextra -Werror -DRETRO_ATOMIC_FORCE_GCC_NEW $san \
 -Ilibretro -Ithird_party/libretro-common/include tools/tests/core_upload_scan_owner_test.cc -o "$out/owner"
"$out/owner"
