#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${ALU_OPERAND_OUT:-build/tests/alu-operand}
mkdir -p "$out"
${CC:-cc} -std=c89 -pedantic -Wno-long-long -Wall -Wextra -Werror -O2 \
 ${SANITIZER--fsanitize=address,undefined} -I. -Ithird_party/libretro-common/include \
 tools/tests/core_alu_operand_test.c -o "$out/test"
"$out/test"
