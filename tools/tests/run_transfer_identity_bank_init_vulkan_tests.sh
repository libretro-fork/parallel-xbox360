#!/bin/sh
# Untimed complete-bank creation/lifetime checks; preserve validation output.
set -eu
test "$#" = 3
executable=$1
prefix=$2
out=$3
mkdir -p "$out"
unset IDENTITY_FIRST_SKIP_DRAW
for configuration in '1 1 0 1 0' '4 4 1 1 0' '4 4 1 0 0' '4 4 1 1 1' '4 4 1 0 1'; do
 name=$(printf '%s' "$configuration" | tr ' ' '-')
 set -- $configuration
 log="$out/$name.log"
 VK_INSTANCE_LAYERS="${IDENTITY_MSAA_VALIDATION:-${VK_INSTANCE_LAYERS:-}}" "$executable" "$prefix" "$@" >"$log" 2>&1
 cat "$log"
 if grep -E 'VUID-|Validation Error' "$log"; then exit 1; fi
 grep -q 'PASS: complete supported bank' "$log"
 grep -q 'INIT_FIRST_DRAW ' "$log"
 # A real missing draw must fail the image oracle, not a timer side effect.
 log="$out/$name-skip.log"
 if IDENTITY_FIRST_SKIP_DRAW=1 VK_INSTANCE_LAYERS="${IDENTITY_MSAA_VALIDATION:-${VK_INSTANCE_LAYERS:-}}" "$executable" "$prefix" "$@" >"$log" 2>&1; then
  echo 'Missing-draw control unexpectedly passed' >&2
  exit 1
 fi
 cat "$log"
 if grep -E 'VUID-|Validation Error' "$log"; then exit 1; fi
 grep -q 'First transfer image mismatch' "$log"
done
