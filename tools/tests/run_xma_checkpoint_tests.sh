#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${XMA_CHECKPOINT_OUT:-build/tests/xma-checkpoint}
mkdir -p "$out"
suffix=
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) suffix=.exe ;; esac
${CC:-cc} -std=c89 -pedantic -Wno-long-long -O2 -Wall -Wextra -Werror \
 ${XMA_CHECKPOINT_FLAGS:-} -Ithird_party/libretro-common/include \
 tools/tests/core_xma_checkpoint_test.c third_party/libretro-common/formats/wma/rwma_pro.c \
 third_party/libretro-common/formats/wma/rwma_standard.c -lm -o "$out/test$suffix"
"$out/test$suffix"
