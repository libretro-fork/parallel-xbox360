#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${TEST_BUILD_DIR:-build/tests/layout-ready}
mkdir -p "$out"
san=${SANITIZER--fsanitize=address,undefined}
${CC:-cc} -std=c89 -pedantic -Wno-long-long -Wall -Wextra -Werror -O1 -g $san \
 -D_GNU_SOURCE -DHAVE_THREADS -DHAVE_THREAD_STORAGE -DRETRO_ATOMIC_FORCE_GCC_NEW \
 -I. -Ithird_party/libretro-common/include -isystem third_party/Vulkan-Headers/include \
 tools/tests/core_layout_ready_test.c third_party/libretro-common/rthreads/rthreads.c \
 third_party/libretro-common/rthreads/retro_eventcount.c -pthread -o "$out/test"
"$out/test"
if test "${GPU_TESTS:-0}" = 1; then
 ${CC:-cc} -std=c89 -pedantic -Wno-long-long -Wall -Wextra -Werror -O2 \
  -I. -Ithird_party/libretro-common/include -isystem third_party/Vulkan-Headers/include \
  tools/tests/core_layout_device_test.c -ldl -o "$out/device"
 "$out/device"
fi
