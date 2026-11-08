#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
out=${DXIL_FORWARD_OUT:-build/tests/dxil-forward-flow}
mkdir -p "$out"
# This helper emits raw DXIL; validation/execution are separate optional tools.
${MAKE:-make} -j1 build/make/host/dxil-alu-test${EXEEXT:-} HOST_CFLAGS="-O2 -Wno-long-long"
generator=build/make/host/dxil-alu-test${EXEEXT:-}
: > "$out/list.txt"
i=0
while test "$i" -le 43; do
 if test "$i" -ge 5 && test "$i" -le 10; then
  if "$generator" "$out/case$i.dxil" flow "$i"; then echo "accepted invalid flow $i"; exit 1; fi
 else
  "$generator" "$out/case$i.dxil" flow "$i"
  printf '%s\n' "$out/case$i.dxil" >> "$out/list.txt"
  if test -n "${DXIL_VALIDATOR:-}"; then "$DXIL_VALIDATOR" "$out/case$i.dxil"; fi
  if test -n "${DXIL_RUNTIME:-}"; then "$DXIL_RUNTIME" "$out/case$i.dxil" "$out/case$i.dxil.expected"; fi
 fi
 i=$((i+1))
done
echo 'PASS: 38 generated DXIL flow fixtures, state-slot counts and six invalid programs'
