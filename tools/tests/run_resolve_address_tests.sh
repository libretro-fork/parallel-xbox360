#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${TEST_BUILD_DIR:-"$root/build/tests/resolve-address-$(uname -s)"}
mkdir -p "$out"
san=${SANITIZER--fsanitize=address,undefined}
extra=
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) extra=-Wno-long-long ;; esac
inc='-I. -Isrc -Ilibretro -Ithird_party/libretro-common/include -Ithird_party/fmt/include'
${CC:-cc} -std=c89 -O2 -g -pedantic -Wall -Wextra -Werror $extra $san $inc -c tools/tests/core_resolve_address_test.c -o "$out/test.o"
${CXX:-c++} -std=c++20 -O2 -g -DNDEBUG -Wall -Wextra -Werror $san $inc -c tools/tests/core_resolve_address_bridge.cc -o "$out/bridge.o"
${CXX:-c++} -static-libgcc -static-libstdc++ $san "$out/test.o" "$out/bridge.o" -o "$out/test"
"$out/test"
