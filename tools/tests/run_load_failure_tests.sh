#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${LOAD_FAILURE_OUT:-build/tests/load-failure}
platform=${PLATFORM:-linux-$(uname -m)}
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) platform=${PLATFORM:-windows-x86_64}; ext=.exe ;; *) ext= ;; esac
mkdir -p "$out/system"
${CC:-cc} -std=c89 -pedantic -Wno-long-long -O2 -Wall -Wextra -Werror \
 -I. -Ilibretro -isystem third_party/Vulkan-Headers/include -c tools/tests/core_load_failure_test.c -o "$out/driver.o"
# The bridge includes the real production API owner once, replacing its object.
sed 's@"[^" ]*/libretro/libretro.cpp.o"@@g' "build/make/$platform/objects.rsp" > "$out/objects.rsp"
libs="-pthread -ldl -lm -lrt"
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) libs="-static -lntdll -lwsock32 -lws2_32 -lxinput -lcomctl32 -lshcore -lshlwapi -ldxguid -lbcrypt -ld3d12 -ldxgi -lsynchronization -lversion -lole32 -luuid -lshell32 -luser32 -lgdi32 -limm32 -lwinmm -ladvapi32 -lsetupapi -lcfgmgr32 -lhid -ldwmapi -lcomdlg32 -loleaut32" ;; esac
${CXX:-c++} -O2 -std=c++20 -Wno-attributes -DNDEBUG -DXENIA_LIBRETRO -DRETRO_ATOMIC_FORCE_GCC_NEW \
 -DXBYAK_NO_OP_NAMES -DXBYAK_ENABLE_OMITTED_OPERAND -DVULKAN_HPP_NO_TO_STRING \
 -I. -Isrc -Ilibretro -Ithird_party -Ithird_party/libretro-common/include \
 -Ithird_party/fmt/include -Ithird_party/Vulkan-Headers/include \
 -Ithird_party/ffmpeg-xenia -Ithird_party/FFmpeg \
 "${LOAD_FAILURE_BRIDGE:-tools/tests/load_failure_bridge.cc}" "$out/driver.o" @"$out/objects.rsp" $libs -o "$out/test$ext"
"$out/test$ext" "$out/system"
VK_DRIVER_FILES="$root/$out/missing-driver.json" VK_ICD_FILENAMES="$root/$out/missing-driver.json" \
 "$out/test$ext" "$out/system" providers
