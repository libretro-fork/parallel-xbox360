#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
out=${UCODE_REUSE_OUT:-build/tests/ucode-reuse-posix}
mkdir -p "$out"
cc=${CC:-cc}
flags="${UCODE_REUSE_FLAGS:-} -std=c89 -Wall -Wextra -Werror -Wdeclaration-after-statement -DHAVE_THREADS -I. -Ithird_party/libretro-common/include"
$cc $flags tools/tests/core_shader_ucode_reuse_test.c -o "$out/unit"
"$out/unit"
$cc $flags tools/tests/core_shader_ucode_reuse_threads_test.c libretro/core_shader_ucode_reuse.c third_party/libretro-common/rthreads/rthreads.c -pthread -o "$out/threads"
"$out/threads"
