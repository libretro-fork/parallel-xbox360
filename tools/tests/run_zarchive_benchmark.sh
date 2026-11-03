#!/usr/bin/env bash
# Linux leaf-reader comparison and sustainable copy reference; no core install.
set -euo pipefail
cd "$(dirname "$0")/../.."
[[ $(uname -s) == Linux ]] || { printf 'This runner requires Linux.\n' >&2; exit 1; }
out=${1:-build/tests/zarchive-benchmark}
base=$(git rev-parse --verify "${2:-3ca00408}^{commit}")
cpu=${BENCH_CPU:-2}
cc=${CC:-gcc}
cxx=${CXX:-g++}
common=third_party/libretro-common
mkdir -p "$out/baseline/zarchive" "$out/objects"
git show "$base:third_party/zarchive/include/zarchive/zarchivereader.h" > "$out/baseline/zarchive/zarchivereader.h"
git show "$base:third_party/zarchive/include/zarchive/zarchivecommon.h" > "$out/baseline/zarchive/zarchivecommon.h"
git show "$base:third_party/zarchive/src/zarchivereader.cpp" > "$out/baseline/reader.cpp"
arch=()
[[ $(uname -m) != x86_64 ]] || arch=(-mavx)
flags=(-O3 "${arch[@]}" -finline-functions -funroll-loops -fomit-frame-pointer
  -DNDEBUG -fPIC -fno-semantic-interposition -D_GNU_SOURCE -DHAVE_MMAP
  -DHAVE_THREADS -DRETRO_ATOMIC_FORCE_GCC_NEW -I"$common/include" -I. -Ilibretro)
sources=(streams/file_stream.c vfs/vfs_implementation.c file/file_path.c
  file/file_path_io.c string/stdstring.c string/rstrtod.c encodings/encoding_utf.c
  compat/compat_posix_string.c compat/compat_strcasestr.c compat/fopen_utf8.c
  compat/compat_strl.c time/rtime.c rthreads/rthreads.c rthreads/retro_eventcount.c
  encodings/encoding_rzstd.c encodings/encoding_xxh64.c hash/lrc_hash.c features/features_cpu.c)
objects=()
for source in "${sources[@]}"; do
  object="$out/objects/$(basename "$source" .c).o"
  "$cc" -std=c89 "${flags[@]}" -c "$common/$source" -o "$object"
  objects+=("$object")
done
"$cc" -std=c89 "${flags[@]}" -c libretro/core_file.c -o "$out/objects/core_file.o"
objects+=("$out/objects/core_file.o")
"$cxx" -std=c++20 "${flags[@]}" -Ithird_party/zarchive/include \
  -c third_party/zarchive/src/zarchivewriter.cpp -o "$out/objects/writer.o"
writer=("$out/objects/writer.o")
for variant in baseline current; do
  definitions=()
  extra=()
  if [[ $variant == baseline ]]; then
    inc="$out/baseline"
    definitions=(-DXE_ZAR_BASELINE)
    "$cxx" -std=c++20 "${flags[@]}" -I"$inc" -Ithird_party/zarchive/include \
      -c "$out/baseline/reader.cpp" -o "$out/objects/baseline-reader.o"
    extra+=("$out/objects/baseline-reader.o")
  else
    inc=third_party/zarchive/include
    "$cc" -std=c89 "${flags[@]}" -c libretro/core_zarchive.c -o "$out/objects/engine.o"
    extra+=("$out/objects/engine.o")
  fi
  "$cxx" -std=c++20 "${flags[@]}" "${definitions[@]}" -I"$inc" \
    -Ithird_party/zarchive/include -c tools/tests/core_zarchive_bridge.cc \
    -o "$out/objects/$variant-bridge.o"
  "$cxx" -shared -Wl,-Bsymbolic -Wl,--no-undefined "${objects[@]}" \
    "${writer[@]}" "${extra[@]}" "$out/objects/$variant-bridge.o" \
    -pthread -lm -o "$out/$variant.so"
done
# Test-only counters never enter the production-reader comparison libraries.
"$cc" -std=c89 -pedantic-errors -Wall -Wextra -Werror "${flags[@]}" \
  -DXE_ZAR_TEST -c libretro/core_zarchive.c -o "$out/objects/test-engine.o"
"$cc" -std=c89 -pedantic-errors -Wall -Wextra -Werror "${flags[@]}" \
  -DXE_ZAR_TEST -c tools/tests/core_zarchive_format_test.c -o "$out/objects/format.o"
"$cc" "${objects[@]}" "$out/objects/test-engine.o" "$out/objects/format.o" \
  -pthread -lm -o "$out/format-test"
"$out/format-test" "$out/raw.zar" raw
"$out/format-test" "$out/rle.zar" rle
"$cc" -std=c89 -pedantic-errors -Wall -Wextra -Werror -O3 -D_GNU_SOURCE \
  -DHAVE_THREADS -I"$common/include" tools/tests/core_zarchive_benchmark.c \
  "$out/objects/rthreads.o" -pthread -ldl -o "$out/bench"
"$cc" -std=c89 -pedantic-errors -Wall -Wextra -Werror -O3 "${arch[@]}" \
  -fPIC -shared -DXE_ZAR_COPY_FLOOR tools/tests/core_zarchive_benchmark.c -o "$out/copy.so"
objdump -d --disassemble=archive_test_copy "$out/copy.so" > "$out/copy-assembly.txt"
objdump -d --disassemble=xe_zar_read "$out/current.so" > "$out/read-assembly.txt"
sed -i 's/[[:blank:]]*$//' "$out/copy-assembly.txt" "$out/read-assembly.txt"
{
  printf 'Baseline: %s\nCurrent: ' "$base"
  git rev-parse HEAD
  printf 'Guest CPU affinity: %s\n' "$cpu"
  printf 'Flags:'; printf ' %q' "${flags[@]}"; printf '\n'
  "$cc" --version
  uname -a
  lscpu
  sha256sum libretro/core_zarchive.c libretro/core_file.c \
    tools/tests/core_zarchive_bridge.cc tools/tests/core_zarchive_benchmark.c
  printf '\nCanonical Git source objects (line endings normalized):\n'
  for source in libretro/core_zarchive.c libretro/core_file.c tools/tests/core_zarchive_bridge.cc tools/tests/core_zarchive_benchmark.c; do
    printf '%s  %s\n' "$(git hash-object "$source")" "$source"
  done
} > "$out/environment.txt"
printf 'variant,pair,mode,count,seconds,peak_kib,checksum\n' > "$out/results.csv"
for mode in cache-raw cache-rle handle-raw handle-rle miss-rle handle-miss-rle open-raw open-rle; do
  case $mode in *raw) file="$out/raw.zar";; *) file="$out/rle.zar";; esac
  case $mode in open*) count=${BENCH_OPEN_ITERS:-300};;
    *miss*) count=${BENCH_MISS_ITERS:-1200000};; *) count=${BENCH_CACHE_ITERS:-30000000};; esac
  for ((pair=0; pair<${BENCH_PAIRS:-10}; ++pair)); do
    if ((pair % 2)); then order=(current baseline); else order=(baseline current); fi
    for variant in "${order[@]}"; do
      printf '%s,%s,' "$variant" "$pair" >> "$out/results.csv"
      taskset -c "$cpu" "$out/bench" "$out/$variant.so" "$file" "$mode" "$count" >> "$out/results.csv"
    done
  done
  for ((pair=0; pair<${BENCH_CONTROL_PAIRS:-5}; ++pair)); do
    for variant in control-a control-b; do
      printf '%s,%s,' "$variant" "$pair" >> "$out/results.csv"
      taskset -c "$cpu" "$out/bench" "$out/baseline.so" "$file" "$mode" "$count" >> "$out/results.csv"
    done
  done
done
for mode in floor-raw floor-rle; do
  case $mode in *raw) file="$out/raw.zar";; *) file="$out/rle.zar";; esac
  for ((pair=0; pair<${BENCH_FLOOR_PAIRS:-10}; ++pair)); do
    printf 'copy,%s,' "$pair" >> "$out/results.csv"
    taskset -c "$cpu" "$out/bench" "$out/current.so" "$file" "$mode" \
      "${BENCH_CACHE_ITERS:-30000000}" "$out/copy.so" >> "$out/results.csv"
  done
done
printf 'Results, environment and assembly: %s\n' "$out"
