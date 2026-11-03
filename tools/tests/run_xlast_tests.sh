#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
out=${1:-build/tests/xlast}
cc=${CC:-gcc}; cxx=${CXX:-g++}
mkdir -p "$out/legacy"
for name in pugixml.cpp pugixml.hpp pugiconfig.hpp; do
 git show "cb585962:third_party/pugixml/src/$name" > "$out/legacy/$name"
done
flags=(-O1 -g -ffunction-sections -fdata-sections -I. -Ithird_party/libretro-common/include)
link=(-Wl,--gc-sections)
if [[ -n ${SANITIZER-address,undefined} ]]; then
 flags+=(-fsanitize="${SANITIZER-address,undefined}" -fno-sanitize-recover=all)
 link+=(-fsanitize="${SANITIZER-address,undefined}")
fi
strict=(-std=c89 -pedantic-errors -Wall -Wextra -Werror)
for source in libretro/core_xlast.c libretro/core_decompress.c tools/tests/core_xlast_test.c third_party/libretro-common/formats/xml/rxml.c; do
 "$cc" "${strict[@]}" "${flags[@]}" -c "$source" -o "$out/$(basename "$source" .c).o"
done
"$cc" -std=c89 "${flags[@]}" -c third_party/libretro-common/encodings/encoding_deflate.c -o "$out/encoding_deflate.o"
for part in encodings/encoding_crc32.c features/features_cpu.c; do
 "$cc" -std=c89 -D_GNU_SOURCE -DHAVE_THREADS -DRETRO_ATOMIC_FORCE_GCC_NEW "${flags[@]}" -c "third_party/libretro-common/$part" -o "$out/$(basename "$part" .c).o"
done
"$cxx" -std=c++20 "${flags[@]}" -I"$out/legacy" -c tools/tests/core_xlast_pugi_bridge.cc -o "$out/bridge.o"
"$cxx" -std=c++20 "${flags[@]}" -I"$out/legacy" -c "$out/legacy/pugixml.cpp" -o "$out/pugi.o"
common=("$out/core_decompress.o" "$out/encoding_deflate.o" "$out/encoding_crc32.o" "$out/features_cpu.o")
case $("$cc" -dumpmachine) in
 *mingw*)
  "$cc" -std=c89 "${flags[@]}" -c third_party/libretro-common/time/rtime.c -o "$out/rtime.o"
  common+=("$out/rtime.o")
  link+=(-lwinmm)
  ;;
esac
"$cxx" "${link[@]}" "$out/core_xlast.o" "$out/core_xlast_test.o" "$out/rxml.o" "$out/bridge.o" "$out/pugi.o" "${common[@]}" -o "$out/xlast_test"
"$out/xlast_test"
hooks=(-Dmalloc=xe_rxml_malloc -Dcalloc=xe_rxml_calloc -Drealloc=xe_rxml_realloc -Dfree=xe_rxml_free)
"$cc" "${strict[@]}" "${flags[@]}" "${hooks[@]}" -c third_party/libretro-common/formats/xml/rxml.c -o "$out/rxml-oom.o"
"$cc" "${strict[@]}" "${flags[@]}" "${hooks[@]}" -c libretro/core_xlast.c -o "$out/xlast-oom.o"
"$cc" "${strict[@]}" "${flags[@]}" "${link[@]}" tools/tests/core_rxml_oom_test.c "$out/rxml-oom.o" "$out/xlast-oom.o" "${common[@]}" -o "$out/rxml_oom_test"
"$out/rxml_oom_test"
