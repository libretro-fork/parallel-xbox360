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
out=${TEST_BUILD_DIR:-"$root/build/tests/content-bridge"}
objects=${CORE_OBJECTS:-"$root/build/make/$platform/objects.rsp"}
mkdir -p "$out"
cd "$root"
inc="-I. -Isrc -Ilibretro -Itools/tests -Ithird_party/libretro-common/include -Ithird_party/fmt/include -Ithird_party/ffmpeg-xenia -Ithird_party/FFmpeg"
${CC:-cc} -std=c89 -O2 -g -pedantic -Wall -Wextra -Werror -DXE_TEST_CONTENT_BRIDGE $inc \
 -c tools/tests/core_content_input_test.c -o "$out/test.o"
${CXX:-c++} -O2 -std=c++20 -Wno-attributes -DNDEBUG -DXENIA_LIBRETRO -DRETRO_ATOMIC_FORCE_GCC_NEW \
 $inc tools/tests/core_content_bridge.cc "$out/test.o" @"$objects" $libs -o "$out/test$suffix"
"$out/test$suffix" "$out/input.zar"