#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${ALU_RESUME_OUT:-build/tests/alu-resume}
mkdir -p "$out"
san=${SANITIZER--fsanitize=address,undefined}
${CC:-cc} ${ALU_RESUME_FLAGS:-} -std=c89 -pedantic -Wno-long-long -O2 -Wall -Wextra -Werror $san \
 -I. -isystem third_party/SPIRV-Headers/include libretro/core_alu_spv.c \
 "${ALU_RESUME_UNIT:-tools/tests/core_alu_spv_build_test.c}" -o "$out/build"
"$out/build" "$out/arithmetic.spv" "$out/program.spv" "$out/control.spv" "$out/resume.spv"
for module in arithmetic program control resume; do
 "${SPIRV_VALIDATOR:-spirv-val}" --target-env vulkan1.0 "$out/$module.spv"
done
if test "${GPU_TESTS:-0}" != 1; then exit 0; fi
${CC:-cc} ${ALU_RESUME_FLAGS:-} -std=c89 -pedantic -Wno-long-long -O2 -Wall -Wextra -Werror $san \
 -I. -isystem third_party/SPIRV-Headers/include -c libretro/core_alu_spv.c -o "$out/emitter.o"
for mode in reference continuation; do
 flags=
 if test "$mode" = reference; then flags=-DXE_RESUME_BASELINE; fi
 mkdir -p "$out/$mode"
 ${CC:-cc} ${ALU_RESUME_FLAGS:-} -std=c89 -pedantic -Wno-long-long -O2 -Wall -Wextra -Werror $san \
  -I. -Itools/tests -isystem third_party/Vulkan-Headers/include $flags \
  -c "${ALU_RESUME_SOURCE:-tools/tests/core_alu_resume_vulkan_test.c}" -o "$out/$mode/driver.o"
 XE_ALU_PROGRAM_DUMP="$out/$mode/observed.spv" \
 CXX="${CXX:-c++} $san $out/$mode/driver.o $out/emitter.o -Wl,--strip-debug" \
 TEST_SOURCE="${ALU_RESUME_BRIDGE:-tools/tests/alu_program_bridge.cc}" TEST_BUILD_DIR="$out/$mode" \
 sh tools/tests/run_shader_publication_tests.sh
 module=resume
 if test "$mode" = reference; then module=control; fi
 cmp "$out/$module.spv" "$out/$mode/observed.spv"
 "${SPIRV_VALIDATOR:-spirv-val}" --target-env vulkan1.0 "$out/$mode/observed.spv"
done
