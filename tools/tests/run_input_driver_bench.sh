#!/bin/sh
set -eu
base=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$base/build/tests/input-driver-$(uname -s)"}
old="$out/baseline/libretro"
ext=
libs=-pthread
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) ext=.exe; libs="$libs -static" ;; esac
san=${SANITIZER-}
mkdir -p "$out/baseline"
git -C "$base" archive "${INPUT_BASELINE:-446c545f}" libretro/libretro_hid.cc libretro/libretro_hid.h | tar -x -C "$out/baseline"
inc="-I$base -I$base/src -I$base/libretro -I$base/third_party/libretro-common/include -I$base/third_party/fmt/include"
arch=
case $(uname -m) in x86_64|amd64) arch=-mavx ;; esac
for source in core_input_mailbox core_audio_cadence core_input_analog; do
 ${CC:-cc} -std=c89 -O3 -D_GNU_SOURCE $san -DRETRO_ATOMIC_FORCE_GCC_NEW $inc -c "$base/libretro/$source.c" -o "$out/$source.o"
done
${CC:-cc} -std=c89 -O3 -D_GNU_SOURCE $san -c "$base/tools/tests/core_input_driver_bench.c" -o "$out/bench.o"
for variant in old new; do
 flags=
 dir="$base/libretro"
 if [ "$variant" = old ]; then flags=-DINPUT_OLD; dir="$old"; fi
 ${CXX:-c++} -std=c++20 -O3 $san -DNDEBUG -DXENIA_LIBRETRO $arch $flags -I"$dir" $inc -c "$base/tools/tests/core_input_handoff_bridge.cc" -o "$out/bridge-$variant.o"
 ${CXX:-c++} -std=c++20 -O3 $san -DNDEBUG -DXENIA_LIBRETRO $arch -I"$dir" $inc -c "$dir/libretro_hid.cc" -o "$out/hid-$variant.o"
 ${CXX:-c++} $san "$out/bench.o" "$out/bridge-$variant.o" "$out/hid-$variant.o" "$out/core_input_mailbox.o" "$out/core_audio_cadence.o" "$out/core_input_analog.o" $libs -o "$out/$variant$ext"
done
if [ "${INPUT_BENCH_BUILD_ONLY:-0}" = 1 ]; then exit 0; fi
for pass in 1 2; do
 order="old new"
 if [ "$pass" = 2 ]; then order="new old"; fi
 for variant in $order; do
  "$out/$variant$ext" > "$out/$variant-$pass.txt"
  printf '%s pass %s\n' "$variant" "$pass"
  cat "$out/$variant-$pass.txt"
 done
done
