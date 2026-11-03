#!/usr/bin/env bash
# Developer-only historical oracle; the core build requires no Git extraction.
set -euo pipefail
cd "$(dirname "$0")/../.."
out=${1:-build/tests/guest-sha256}
reference=${2:-183a97d1}
cc=${CC:-gcc}
cxx=${CXX:-g++}
common=third_party/libretro-common
mkdir -p "$out/legacy"
git show "$reference:third_party/crypto/sha256.h" > "$out/legacy/sha256.h"
# The historical implementation dereferences unaligned uint32_t pointers.
# Make only the test oracle's loads legal on strict-alignment targets.
git show "$reference:third_party/crypto/sha256.cpp" |
  sed 's/const uint32_t\* input = (uint32_t\*)data;/uint32_t input[16]; memcpy(input, data, sizeof(input));/' \
  > "$out/legacy/guest_sha256_reference.cpp"
san=()
[[ $(uname -s) != Linux ]] || san=(-fsanitize=address,undefined -fno-sanitize-recover=all)
flags=(-O1 -g -ffunction-sections -fdata-sections -D_GNU_SOURCE
  -DHAVE_THREADS -DRETRO_ATOMIC_FORCE_GCC_NEW -I. -I"$common/include")
for source in libretro/core_guest_hash.c tools/tests/core_guest_sha256_test.c; do
  "$cc" -std=c89 -pedantic-errors -Wall -Wextra -Werror "${flags[@]}" "${san[@]}" \
    -c "$source" -o "$out/$(basename "$source" .c).o"
done
sources=(hash/lrc_hash.c features/features_cpu.c)
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
"$cxx" -std=c++20 "${flags[@]}" "${san[@]}" -I"$out/legacy" \
  -c tools/tests/core_guest_sha256_bridge.cc -o "$out/bridge.o"
"$cxx" "${san[@]}" -Wl,--gc-sections "$out/core_guest_hash.o" \
  "$out/core_guest_sha256_test.o" "${objects[@]}" \
  "$out/bridge.o" "${libs[@]}" -o "$out/test"
"$out/test"
