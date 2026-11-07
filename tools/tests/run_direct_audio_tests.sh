#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TEST_BUILD_DIR:-"$root/build/tests/audio-direct-$(uname -s)"}
mkdir -p "$out"
ext=
platform_flags=-D_GNU_SOURCE
platform_libs=
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) ext=.exe; platform_flags=-Wno-error=cast-function-type; platform_libs=-lwinmm ;; esac
san=${SANITIZER--fsanitize=address,undefined}
simd=
case $(uname -m) in x86_64|amd64) simd=-mssse3 ;; esac
common="$root/third_party/libretro-common"
${CC:-cc} -std=c89 -Wall -Wextra -Werror -O2 -g $san $simd \
 -I"$root/libretro" "$root/libretro/core_audio_convert.c" \
 "$root/tools/tests/core_audio_convert_test.c" -o "$out/convert$ext"
"$out/convert$ext"
for plain in 0 1; do
 flag=
 if [ "$plain" = 1 ]; then flag=-DXE_AUDIO_QUEUE_PLAIN; fi
 ${CC:-cc} -std=c89 -Wall -Wextra -Werror -O2 -g $san $simd $flag $platform_flags \
 -DHAVE_THREADS -DHAVE_THREAD_STORAGE -I"$root/libretro" -I"$common/include" \
 "$root/libretro/core_audio_queue.c" "$root/libretro/core_audio_convert.c" \
 "$root/tools/tests/core_audio_queue_test.c" "$root/tools/tests/core_audio_queue_state_test.c" "$common/queues/retro_spsc.c" \
 "$common/rthreads/rthreads.c" "$common/rthreads/retro_eventcount.c" \
 "$common/features/features_cpu.c" "$common/compat/compat_strl.c" \
 "$common/time/rtime.c" -pthread $platform_libs -o "$out/queue-$plain$ext"
 "$out/queue-$plain$ext"
 ${CC:-cc} -std=c89 -Wall -Wextra -Werror -O2 -g $san $simd $flag $platform_flags \
 -DXE_AUDIO_REGISTRY_TEST -DHAVE_THREADS -DHAVE_THREAD_STORAGE -I"$root/libretro" -I"$common/include" \
 "$root/libretro/core_audio_queue.c" "$root/libretro/core_audio_convert.c" \
 "$root/tools/tests/core_audio_registry_test.c" "$common/queues/retro_spsc.c" \
 "$common/rthreads/rthreads.c" "$common/rthreads/retro_eventcount.c" \
 "$common/features/features_cpu.c" "$common/compat/compat_strl.c" \
 "$common/time/rtime.c" -pthread $platform_libs -o "$out/registry-$plain$ext"
 "$out/registry-$plain$ext"
done
