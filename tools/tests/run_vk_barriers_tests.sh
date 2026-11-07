#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${TEST_BUILD_DIR:-"$root/build/tests/vk-barriers-$(uname -s)"}
mkdir -p "$out"
san=${SANITIZER--fsanitize=address,undefined}
extra=
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) extra=-Wno-long-long ;; esac
${CC:-cc} -std=c89 -O1 -g -pedantic -Wall -Wextra -Werror $extra $san \
 -Ilibretro -Itools/tests -Ithird_party/libretro-common/include -isystem third_party/Vulkan-Headers/include \
 tools/tests/core_vk_barriers_test.c -o "$out/leaf"
"$out/leaf"
# The portable C89 planner runs everywhere. Compile the actual C++ recorder and
# C89 API mocks on Windows, where its current platform headers are available.
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) ;; *) exit 0 ;; esac
inc='-I. -Isrc -Ilibretro -Itools/tests -Ithird_party/libretro-common/include -Ithird_party/fmt/include -Ithird_party -Ithird_party/ffmpeg-xenia -Ithird_party/FFmpeg -Ibuild/make/windows-x86_64/generated -isystem third_party/Vulkan-Headers/include'
flags='-O2 -g -mavx -DNDEBUG -DFMT_HEADER_ONLY -DXENIA_LIBRETRO -DVULKAN_HPP_NO_TO_STRING -DHAVE_THREADS -DHAVE_THREAD_STORAGE -DRETRO_ATOMIC_FORCE_GCC_NEW -DASIO_STANDALONE -DASIO_NO_DEPRECATED -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00'
${CC:-cc} -std=c89 -pedantic -Wall -Wextra -Werror $extra $flags $inc -DXE_VK_REPLAY_TEST -c tools/tests/core_vk_barriers_test.c -o "$out/test.o"
for source in tools/tests/core_vk_barriers_bridge.cc src/xenia/gpu/vulkan/deferred_command_buffer.cc; do
 ${CXX:-c++} -std=c++20 -Wno-attributes $flags $inc -include tools/tests/core_command_rows_profile.h -c "$source" -o "$out/$(basename "$source" .cc).o"
done
${CXX:-c++} -static "$out/test.o" "$out/core_vk_barriers_bridge.o" "$out/deferred_command_buffer.o" -o "$out/test.exe"
"$out/test.exe"
