#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/command-stream-$(uname -s)"}
mkdir -p "$out"
cd "$root"
san=${SANITIZER--fsanitize=address,undefined}
${CC:-cc} -std=c89 -O1 -g -pedantic -Wall -Wextra -Werror $san \
 -Ilibretro -Ithird_party/libretro-common/include tools/tests/core_command_stream_test.c -o "$out/leaf"
"$out/leaf"
${CXX:-c++} -std=c++20 -O1 -g -Wall -Wextra -Werror $san \
 -Ilibretro -Ithird_party/libretro-common/include tools/tests/core_command_stream_owner_test.cc -o "$out/owner"
"$out/owner"
