#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
platform=${PLATFORM:-linux-x86_64}
libs="-pthread -ldl -lm -lrt"
suffix=
case $(uname -s) in
 MINGW*|MSYS*|CYGWIN*)
  platform=${PLATFORM:-windows-x86_64}
  suffix=.exe
  libs="-static -lntdll -lwsock32 -lws2_32 -lxinput -lcomctl32 -lshcore -lshlwapi -ldxguid -lbcrypt -ld3d12 -ldxgi -lsynchronization -lversion -lole32 -luuid -lshell32 -luser32 -lgdi32 -limm32 -lwinmm -ladvapi32 -lsetupapi -lcfgmgr32 -lhid -ldwmapi -lcomdlg32 -loleaut32"
 ;;
esac
out=${TEST_BUILD_DIR:-"$root/build/tests/spv-memexport"}
objects=${CORE_OBJECTS:-"$root/build/make/$platform/objects.rsp"}
source="$root/tools/tests/core_spv_memexport_bridge.cc"
mkdir -p "$out"
out=$(CDPATH= cd -- "$out" && pwd)
# An optional reference object overrides only ExportToMemory. Compile it with
# the SAME current public headers and ABI as the core; never mix class layouts.
# run_spv_export_reference.sh builds the historical algorithm this way.
set --
if test -n "${MEMEXPORT_REFERENCE_OBJECT:-}"; then set -- "$MEMEXPORT_REFERENCE_OBJECT"; fi
if test "${PROFILE:-0}" = 1; then
 source="$root/tools/tests/core_spv_memexport_bench.cc"
 ${CC:-cc} -std=c89 -O2 -c "$root/tools/tests/core_pipeline_bench_profile.c" -o "$out/profile.o"
 set -- "$@" "$out/profile.o"
fi
cd "$root"
${CXX:-c++} -O2 -std=c++20 -Wno-attributes -DNDEBUG -DXENIA_LIBRETRO -DRETRO_ATOMIC_FORCE_GCC_NEW \
 -I"$root" -I"$root/src" -I"$root/third_party/libretro-common/include" -I"$root/third_party/fmt/include" \
 -I"$root/third_party/ffmpeg-xenia" -I"$root/third_party/FFmpeg" \
 "$source" "$@" @"$objects" $libs -o "$out/test$suffix"
export SPV_GUEST_OUTPUT=${SPV_GUEST_OUTPUT:-"$out/guest.bin"}
"$out/test$suffix"
