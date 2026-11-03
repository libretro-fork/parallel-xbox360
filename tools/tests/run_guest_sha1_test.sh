#!/usr/bin/env bash
# Developer-only differential against actual FFmpeg state transitions.
set -euo pipefail
cd "$(dirname "$0")/../.."
out=${1:-build/tests/guest-sha1}
cc=${CC:-gcc}
cxx=${CXX:-g++}
common=third_party/libretro-common
mkdir -p "$out/legacy"
git show d40cc603:third_party/crypto/TinySHA1.hpp > "$out/legacy/TinySHA1.hpp"
san=()
[[ $(uname -s) != Linux ]] || san=(-fsanitize=address,undefined -fno-sanitize-recover=all)
flags=(-O1 -g -ffunction-sections -fdata-sections -D_GNU_SOURCE
  -DHAVE_THREADS -DRETRO_ATOMIC_FORCE_GCC_NEW -I. -I"$common/include")
for source in libretro/core_guest_hash.c libretro/core_crypto.c tools/tests/core_guest_sha1_test.c; do
  "$cc" -std=c89 -pedantic-errors -Wall -Wextra -Werror "${flags[@]}" "${san[@]}" \
    -c "$source" -o "$out/$(basename "$source" .c).o"
done
sources=(hash/lrc_hash.c features/features_cpu.c utils/md5.c crypto/crypto.c crypto/kdf.c)
windows=0
case $(uname -s) in MINGW*|MSYS*) windows=1 ;; esac
if (( windows )); then
  sources+=(streams/file_stream.c vfs/vfs_implementation.c file/file_path.c
    file/file_path_io.c string/stdstring.c string/rstrtod.c
    encodings/encoding_utf.c compat/compat_strl.c compat/compat_posix_string.c
    compat/compat_strcasestr.c compat/fopen_utf8.c time/rtime.c
    rthreads/rthreads.c rthreads/retro_eventcount.c)
fi
libs=(-lm)
(( !windows )) || libs+=(-lwinmm)
objects=()
for source in "${sources[@]}"; do
  "$cc" -std=c89 "${flags[@]}" "${san[@]}" -c "$common/$source" \
    -o "$out/$(basename "$source" .c).o"
  objects+=("$out/$(basename "$source" .c).o")
done
for source in tools/tests/core_guest_sha1_reference.c; do
  "$cc" -std=c99 "${flags[@]}" "${san[@]}" -Ithird_party/ffmpeg-xenia -Ithird_party/FFmpeg \
    -c "$source" -o "$out/$(basename "$source" .c).o"
  objects+=("$out/$(basename "$source" .c).o")
done
"$cxx" -std=c++20 "${flags[@]}" "${san[@]}" -I"$out/legacy" -c tools/tests/core_sha1_bridge.cc -o "$out/bridge.o"
objects+=("$out/bridge.o")
"$cxx" "${san[@]}" -Wl,--gc-sections "$out/core_guest_hash.o" \
  "$out/core_guest_sha1_test.o" "$out/core_crypto.o" "${objects[@]}" "${libs[@]}" -o "$out/test"
"$out/test"
