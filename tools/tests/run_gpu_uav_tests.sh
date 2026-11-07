#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/gpu-uav-$(uname -s)"}
mkdir -p "$out"
cd "$root"
san=${SANITIZER--fsanitize=address,undefined}
extra=
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) extra=-Wno-long-long ;; esac
${CC:-cc} -std=c89 -O1 -g -pedantic -Wall -Wextra -Werror $extra $san \
 -Ilibretro -Ithird_party/libretro-common/include tools/tests/core_gpu_uav_test.c -o "$out/leaf"
"$out/leaf"
