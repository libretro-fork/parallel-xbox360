#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/resolve-recent-reference-$(uname -s)"}
mkdir -p "$out"
cd "$root"
${CXX:-c++} -std=c++20 -O2 -Wall -Wextra -Werror -Ilibretro \
 -Ithird_party/libretro-common/include tools/tests/core_resolve_recent_reference.cc -o "$out/reference"
"$out/reference" "$@"
