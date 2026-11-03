#!/usr/bin/env bash
# Linux developer benchmark. Run serially on an otherwise idle machine.
set -euo pipefail
cd "$(dirname "$0")/../.."
out=${1:-build/tests/sha1-benchmark}
cc=${CC:-gcc}; cxx=${CXX:-g++}
mkdir -p "$out/legacy"
git show d40cc603:third_party/crypto/TinySHA1.hpp > "$out/legacy/TinySHA1.hpp"
common=third_party/libretro-common
flags=(-O3 -g -mavx -fPIC -ffunction-sections -fdata-sections -D_GNU_SOURCE
  -DHAVE_THREADS -DRETRO_ATOMIC_FORCE_GCC_NEW -I. -I"$common/include")
"$cxx" -std=c++20 "${flags[@]}" -I"$out/legacy" -c tools/tests/core_sha1_bridge.cc -o "$out/bridge.o"
for variant in dispatch scalar; do
  mkdir -p "$out/$variant"
  objects=()
  for source in libretro/core_guest_hash.c libretro/core_crypto.c tools/tests/core_sha1_benchmark.c \
      "$common/hash/lrc_hash.c" "$common/features/features_cpu.c" \
      "$common/crypto/crypto.c" "$common/crypto/kdf.c" "$common/utils/md5.c"; do
    extra=()
    if [[ $variant == scalar ]]; then
      [[ $source != */lrc_hash.c ]] || extra+=(-Dcpu_features_get=xe_test_cpu_features_get)
      [[ $source != */core_sha1_benchmark.c ]] || extra+=(-DXE_SHA_TEST_SCALAR)
    fi
    object="$out/$variant/$(basename "$source" .c).o"
    "$cc" -std=c89 "${flags[@]}" "${extra[@]}" -c "$source" -o "$object"
    objects+=("$object")
  done
  "$cxx" -Wl,--gc-sections "${objects[@]}" "$out/bridge.o" -lm -o "$out/$variant/bench"
done
{
  "$cc" --version
  lscpu
  sha256sum libretro/core_guest_hash.c "$common/hash/lrc_hash.c" "$out/legacy/TinySHA1.hpp"
  printf 'cpu=%s pairs=%s iterations target approximately 128 MiB per process\n' "${BENCH_CPU:-2}" "${BENCH_PAIRS:-10}"
} > "$out/environment.txt"
printf 'variant,pair,mode,size,iterations,seconds,MiB_per_s,checksum\n' > "$out/results.csv"
for size in ${BENCH_SIZES:-3 20 64 512 4096 65536 1048576}; do
  iterations=$((134217728 / size))
  (( iterations <= 262144 )) || iterations=262144
  for ((pair=0;pair<${BENCH_PAIRS:-10};pair++)); do
    if (( pair%2 )); then modes=(3 2 1 0); else modes=(0 1 2 3); fi
    for variant in dispatch scalar; do
      for mode in "${modes[@]}"; do
        line=$(taskset -c "${BENCH_CPU:-2}" "$out/$variant/bench" "$mode" "$size" "$iterations")
        printf '%s,%s,%s\n' "$variant" "$pair" "$line" >> "$out/results.csv"
      done
    done
  done
done
