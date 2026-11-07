#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) ;; *) echo 'D3D12 COM replay requires a Windows host; portable row tests are separate'; exit 0 ;; esac
out=${TEST_BUILD_DIR:-"$root/build/tests/d3d12-barriers-mingw"}
mkdir -p "$out"
inc='-I. -Isrc -Ilibretro -Ithird_party/libretro-common/include -Ithird_party/fmt/include -Ithird_party/Vulkan-Headers/include -Ithird_party -Ithird_party/ffmpeg-xenia -Ithird_party/FFmpeg -Ibuild/make/windows-x86_64/generated -isystem third_party/DirectX-Headers/include/directx'
flags='-O2 -g -mavx -DNDEBUG -DFMT_HEADER_ONLY -DXENIA_LIBRETRO -DVULKAN_HPP_NO_TO_STRING -DHAVE_THREADS -DHAVE_THREAD_STORAGE -DRETRO_ATOMIC_FORCE_GCC_NEW -DASIO_STANDALONE -DASIO_NO_DEPRECATED -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00'
${CC:-cc} -std=c89 -pedantic -Wall -Wextra -Werror -Wno-long-long $flags $inc -c tools/tests/core_d3d12_barrier_test.c -o "$out/test.o"
for source in tools/tests/core_d3d12_barrier_bridge.cc src/xenia/gpu/d3d12/deferred_command_list.cc; do
 ${CXX:-c++} -std=c++20 -Wno-attributes $flags $inc -include tools/tests/core_command_rows_profile.h -c "$source" -o "$out/$(basename "$source" .cc).o"
done
${CXX:-c++} -static "$out/test.o" "$out/core_d3d12_barrier_bridge.o" "$out/deferred_command_list.o" -o "$out/test.exe"
"$out/test.exe"
