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
out=${TEST_BUILD_DIR:-"$root/build/tests/shader-publication"}
objects=${CORE_OBJECTS:-"$root/build/make/$platform/objects.rsp"}
source=${TEST_SOURCE:-"$root/tools/tests/shader_publication_test.cc"}
mkdir -p "$out"
# Uses the actual Make link inputs, including the C handoff objects.
cd "$root"
${CXX:-c++} -O2 -std=c++20 -Wno-attributes -DNDEBUG -DXENIA_LIBRETRO -DRETRO_ATOMIC_FORCE_GCC_NEW \
 -I"$root" -I"$root/src" -I"$root/third_party/libretro-common/include" -I"$root/third_party/fmt/include" \
 -I"$root/third_party/ffmpeg-xenia" -I"$root/third_party/FFmpeg" \
 "$source" @"$objects" $libs -o "$out/test$suffix"
"$out/test$suffix"
