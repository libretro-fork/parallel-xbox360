#!/bin/sh
set -eu
: "${XMA_BENCH_INPUT:?Set a private raw-XMA record corpus}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
platform=${PLATFORM:-linux-x86_64}
libs="-pthread -lm -ldl"
suffix=
case $(uname -s) in
 MINGW*|MSYS*|CYGWIN*)
  platform=${PLATFORM:-windows-x86_64}
  suffix=.exe
  libs="-static -lbcrypt -lole32 -luser32 -ladvapi32 -lws2_32 -lm"
 ;;
esac
out=${XMA_BENCH_OUT:-build/tests/xma-native-benchmark}
archive=${CORE_ARCHIVE:-"build/make/$platform/core.a"}
mkdir -p "$out"
${CC:-cc} -O2 ${XMA_BENCH_FLAGS:-} -std=gnu89 -Wdeclaration-after-statement \
 -Ithird_party/libretro-common/include -Ithird_party/FFmpeg -Ithird_party/ffmpeg-xenia \
 tools/tests/xma_native_benchmark.c third_party/libretro-common/formats/wma/rwma_pro.c \
 third_party/libretro-common/formats/wma/rwma_standard.c "$archive" $libs -o "$out/test$suffix"
"$out/test$suffix" "$XMA_BENCH_INPUT" "${XMA_BENCH_CHANNELS:-2}"
