#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
if [ "$#" = 0 ]; then echo "Pass generated MP3/WMA fixture paths" >&2; exit 2; fi
out=${MEDIA_TEST_OUT:-build/tests/native-media-c89}
mkdir -p "$out"
common=third_party/libretro-common
sources="libretro/core_media_audio.c tools/tests/core_media_audio_test.c"
for name in formats/audio_transfer.c formats/data_transfer.c formats/mp3/rmp3.c \
 formats/wma/rwma_pro.c formats/wma/rwma_standard.c formats/wma/rwma_stream.c \
 memmap/memmap.c features/features_cpu.c streams/file_stream.c vfs/vfs_implementation.c \
 file/file_path.c file/file_path_io.c string/stdstring.c string/rstrtod.c \
 encodings/encoding_utf.c compat/fopen_utf8.c compat/compat_strl.c \
 compat/compat_strcasestr.c time/rtime.c; do sources="$sources $common/$name"; done
suffix=
map=-DHAVE_MMAP
libs="-pthread -lm"
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) suffix=.exe; map=; libs="$libs -lwinmm -lws2_32" ;; esac
${CC:-cc} -std=c89 -pedantic -Wno-long-long -Wdeclaration-after-statement \
 -O2 ${MEDIA_TEST_FLAGS:-} -D_GNU_SOURCE $map -DHAVE_RMP3 -DHAVE_RWMA -DDT_STRICT \
 -I. -I"$common/include" $sources $libs -o "$out/test$suffix"
for fixture in "$@"; do "$out/test$suffix" "$fixture"; done
