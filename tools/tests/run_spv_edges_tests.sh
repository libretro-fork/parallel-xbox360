#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/spv-edges-$(uname -s)"}
mkdir -p "$out"
cd "$root"
san=${SANITIZER--fsanitize=address,undefined}
extra=
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) extra=-Wno-long-long ;; esac
${CC:-cc} -std=c89 -O1 -g -pedantic -Wall -Wextra -Werror $extra $san \
 -Ilibretro -Ithird_party/libretro-common/include tools/tests/core_spv_edges_test.c -o "$out/leaf"
"$out/leaf"

case "$san" in *address*)
 for lifetime in relocate-uaf clear-uaf; do
  if "$out/leaf" "$lifetime" > "$out/$lifetime.log" 2>&1; then echo "missed retired edge view"; exit 1; fi
  grep -q 'use-after-poison' "$out/$lifetime.log"
 done
 echo 'PASS: ASan catches relocated and cleared edge views'
 ;; esac