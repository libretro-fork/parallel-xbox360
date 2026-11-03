#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
out=${1:-build/tests/xlast-benchmark}
cc=${CC:-gcc}; cxx=${CXX:-g++}
mkdir -p "$out/legacy"
for name in pugixml.cpp pugixml.hpp pugiconfig.hpp; do
 git show "cb585962:third_party/pugixml/src/$name" > "$out/legacy/$name"
done
flags=(-O3 -g -mavx -fPIC -ffunction-sections -fdata-sections -D_GNU_SOURCE
 -I. -Ithird_party/libretro-common/include)
objects=()
for source in libretro/core_xlast.c libretro/core_decompress.c tools/tests/core_xlast_benchmark.c \
 third_party/libretro-common/formats/xml/rxml.c third_party/libretro-common/encodings/encoding_deflate.c; do
 object="$out/$(basename "$source" .c).o"
 "$cc" -std=c89 "${flags[@]}" -c "$source" -o "$object"; objects+=("$object")
done
"$cxx" -std=c++20 "${flags[@]}" -I"$out/legacy" -c tools/tests/core_xlast_pugi_bridge.cc -o "$out/bridge.o"
"$cxx" -std=c++20 "${flags[@]}" -I"$out/legacy" -c "$out/legacy/pugixml.cpp" -o "$out/pugi.o"
"$cxx" -Wl,--gc-sections "${objects[@]}" "$out/bridge.o" "$out/pugi.o" -o "$out/bench"
{ "$cc" --version; lscpu; sha256sum libretro/core_xlast.c third_party/libretro-common/formats/xml/rxml.c "$out/legacy/pugixml.cpp"; } > "$out/environment.txt"
printf 'pair,mode,strings,XML_bytes,iterations,seconds,ns_per_operation,peak_KiB,checksum\n' > "$out/results.csv"
for n in ${BENCH_COUNTS:-10 500 5000}; do
 for ((pair=0;pair<${BENCH_PAIRS:-10};pair++)); do
  if ((pair%2)); then modes=(5 4 3 2 1 0); else modes=(0 1 2 3 4 5); fi
  for mode in "${modes[@]}"; do
   if ((mode<2)); then iterations=$((100000/n)); ((iterations>=64)) || iterations=64
   else iterations=$((5000000/n)); fi
   line=$(taskset -c "${BENCH_CPU:-2}" "$out/bench" "$mode" "$n" "$iterations")
   printf '%s,%s\n' "$pair" "$line" >> "$out/results.csv"
  done
 done
done
