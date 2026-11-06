#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/failed-launch-bridge"}
objects=${CORE_OBJECTS:-"$root/build/make/windows-x86_64/objects.rsp"}
mkdir -p "$out"
cd "$root"
# This fixture includes libretro.cpp; omit that object from the core response.
sed 's@"[^" ]*/libretro/libretro.cpp.o"@@g' "$objects" > "$out/objects.rsp"
inc="-I. -Isrc -Ilibretro -Ithird_party -Ithird_party/libretro-common/include -Ithird_party/fmt/include -Ithird_party/ffmpeg-xenia -Ithird_party/FFmpeg -Ithird_party/Vulkan-Headers/include -Ithird_party/DirectX-Headers/include/directx -Ithird_party/capstone/include -Ithird_party/zarchive/include -Ibuild/make/windows-x86_64/generated"
${CC:-cc} -std=c89 -O2 -pedantic -Wall -Wextra -Werror $inc -c tools/tests/core_failed_launch_test.c -o "$out/test.o"
${CXX:-c++} -O2 -std=c++20 -mavx -Wno-attributes -DNDEBUG -DXENIA_LIBRETRO -DRETRO_ATOMIC_FORCE_GCC_NEW $inc \
 tools/tests/core_failed_launch_bridge.cc "$out/test.o" @"$out/objects.rsp" \
 -static -lntdll -lwsock32 -lws2_32 -lxinput -lcomctl32 -lshcore -lshlwapi -ldxguid -lbcrypt -ld3d12 -ldxgi -lsynchronization -lversion -lole32 -luuid -lshell32 -luser32 -lgdi32 -limm32 -lwinmm -ladvapi32 -lsetupapi -lcfgmgr32 -lhid -ldwmapi -lcomdlg32 -loleaut32 -o "$out/test.exe"
"$out/test.exe"
