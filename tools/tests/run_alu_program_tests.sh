#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${ALU_PROGRAM_OUT:-build/tests/alu-program}
mkdir -p "$out"
${CC:-cc} ${ALU_PROGRAM_FLAGS:-} -std=c89 -pedantic -Wno-long-long -Wall -Wextra -Werror -O2 \
 ${SANITIZER--fsanitize=address,undefined} -I. -isystem third_party/SPIRV-Headers/include \
 libretro/core_alu_spv.c tools/tests/core_alu_spv_build_test.c -o "$out/build"
if test "${ALU_CONTROL_TEST:-0}" = 1; then
 "$out/build" "$out/arithmetic.spv" "$out/program.spv" "$out/control.spv"
 "${SPIRV_VALIDATOR:-spirv-val}" --target-env vulkan1.0 "$out/control.spv"
else
 "$out/build" "$out/arithmetic.spv" "$out/program.spv"
fi
"${SPIRV_VALIDATOR:-spirv-val}" --target-env vulkan1.0 "$out/arithmetic.spv"
"${SPIRV_VALIDATOR:-spirv-val}" --target-env vulkan1.0 "$out/program.spv"
if test "${GPU_TESTS:-0}" != 1; then exit 0; fi
if test "${ALU_CONTROL_TEST:-0}" = 1; then ALU_PROGRAM_FLAGS="${ALU_PROGRAM_FLAGS:-} -DXE_ALU_CONTROL_TEST"; fi
${CC:-cc} ${ALU_PROGRAM_FLAGS:-} -std=c89 -pedantic -Wno-long-long -Wall -Wextra -Werror -O2 \
 ${SANITIZER--fsanitize=address,undefined} -I. -isystem third_party/SPIRV-Headers/include \
 -c libretro/core_alu_spv.c -o "$out/emitter.o"
${CC:-cc} ${ALU_PROGRAM_FLAGS:-} -std=c89 -pedantic -Wno-long-long -Wall -Wextra -Werror -O2 \
 ${SANITIZER--fsanitize=address,undefined} -I. -isystem third_party/Vulkan-Headers/include \
 "$out/emitter.o" libretro/core_spv_binary.c tools/tests/core_alu_spv_vulkan_test.c -ldl -o "$out/arithmetic"
"$out/arithmetic"
${CC:-cc} ${ALU_PROGRAM_FLAGS:-} -std=c89 -pedantic -Wno-long-long -Wall -Wextra -Werror -O2 \
 ${SANITIZER--fsanitize=address,undefined} -I. -isystem third_party/Vulkan-Headers/include \
 -c "${ALU_PROGRAM_FIXTURE:-tools/tests/core_alu_program_vulkan_test.c}" -o "$out/driver.o"
# Link the actual native interpreter and actual Make objects. The raw emitter is
# new; the core's existing reusable SPIR-V output object supplies serialization.
CXX="${CXX:-c++} ${SANITIZER--fsanitize=address,undefined} $out/driver.o $out/emitter.o" \
 TEST_SOURCE="${ALU_PROGRAM_BRIDGE:-tools/tests/alu_program_bridge.cc}" TEST_BUILD_DIR="$out/native" \
 sh tools/tests/run_shader_publication_tests.sh
