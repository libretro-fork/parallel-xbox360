#!/bin/sh
# Linux supplementary diagnostics only; no performance acceptance or retries.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
out=${SHADER_RESOURCE_OUT:-$(mktemp -d)}
mkdir -p "$out"
if find "$out" -maxdepth 1 -name '*.csv' | read -r ignored; then
    echo 'Use a fresh resource-test output directory' >&2
    exit 2
fi
"${CC:-cc}" -std=c89 -pedantic -Wdeclaration-after-statement -Wall -Wextra -Werror -O2 \
    "$here/shader_compile_resources_test.c" -o "$out/resource-test"
for mode in base candidate; do
    slow=0
    if test "$mode" = candidate; then slow=1; fi
    sample=0
    while test "$sample" -lt 7; do
        XE_RESOURCE_TEST_SLOW=$slow XE_SHADER_BENCH_CSV="$out/$mode.csv" \
        XE_SHADER_BENCH_CPU_CSV="$out/$mode-cpu.csv" \
        XE_SHADER_BENCH_RESOURCE_CSV="$out/$mode-resources.csv" "$out/resource-test"
        sample=$((sample + 1))
    done
done
# Opt-out must still work and must not create a resource log.
env -u XE_SHADER_BENCH_RESOURCE_CSV XE_SHADER_BENCH_CSV="$out/disabled.csv" \
    XE_SHADER_BENCH_CPU_CSV="$out/disabled-cpu.csv" "$out/resource-test"
python3 - "$here" "$out" <<'PY'
import json,sys
from pathlib import Path
sys.path.insert(0,sys.argv[1])
import shader_compile_ci as ci
out=Path(sys.argv[2]); wall={};observations={}
for mode in ('base','candidate'):
    wall[mode]=ci.read_samples(out/(mode+'.csv'))
    observations[mode]=ci.read_resource_samples(out/(mode+'-resources.csv'),wall[mode],7)
    faults=[x for x in observations[mode] if x['case']==1]
    blocked=[x for x in observations[mode] if x['case']==2]
    assert len(faults)==len(blocked)==7
    assert all(x['minor_faults']>=256 for x in faults), faults
    assert all(x['voluntary_switches']>=1 for x in blocked), blocked
report=ci.compare(wall['base'],wall['candidate'],7)
assert report[0]['case']==0 and report[0]['failures'],report
(out/'controls.json').write_text(json.dumps(dict(observations=observations,comparison=report),indent=2))
print('PASS: 42 resource records, fault/wait controls, opt-out and real slowdown rejected with unchanged gates')
PY
