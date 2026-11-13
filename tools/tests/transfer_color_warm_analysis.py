#!/usr/bin/env python3
"""Strict test-only acceptance of paired single-sample color GPU work."""
import argparse
import csv
import json
import math
from pathlib import Path
import statistics
from transfer_uint_warm_analysis import distribution

FORMATS = (0, 1, 2, 3, 4, 5, 14, 15)
SCALES = ((1, 1), (3, 5), (4, 4))



def require(value, reason):
    if not value:
        raise ValueError(reason)


def analyze(path, reverse=False, dynamic=False, packed=False, self_check=False, short_control=False):
    require(not short_control or self_check, "short workload requires identical-pipeline control")
    workloads = (8, 64) if short_control else (64, 512)
    expected = {(scale, sf, df, repeat) for scale in range(3)
                for sf in FORMATS for df in FORMATS for repeat in workloads
                if sf != df and not (sf >= 4 and df >= 4)}
    groups = {}
    count = 0
    with Path(path).open(newline='', encoding='utf-8') as stream:
        for row in csv.DictReader(stream):
            count += 1
            require(count <= 4224, 'excess color samples')
            key = tuple(int(row[x]) for x in ('scale', 'source', 'dest', 'repeat'))
            require(key in expected, 'wrong color corpus')
            require(int(row['dynamic_rendering']) == int(dynamic), 'wrong rendering interface')
            require(int(row['packed_address']) == int(packed), 'wrong shader producer')
            require(int(row['same_pipeline']) == int(self_check), 'wrong pipeline control')
            scale, sf, df, repeat = key
            trial, order, ref = (int(row[x]) for x in ('trial', 'order', 'reference'))
            require(0 <= trial < 8 and order in (0, 1) and ref in (0, 1), 'bad paired sample')
            require(ref == int(((order ^ (trial & 1)) == 0) != reverse), 'wrong paired order')
            sx, sy = SCALES[scale]
            width, height = 2560 * sx >> int(df in (5, 15)), 64 * sy
            require((int(row['width']), int(row['height'])) == (width, height), 'wrong image extent')
            require(int(row['available_stats']) != 0 and
                    int(row['ia_primitives']) == 2 * repeat and
                    int(row['vs_invocations']) > 0 and
                    int(row['fs_invocations']) >= width * height * repeat, 'inactive or missing GPU work')
            start, end, bits = (int(row[x]) for x in ('start', 'end', 'bits'))
            period, host, seconds = (float(row[x]) for x in ('period_ns', 'host_seconds', 'gpu_seconds'))
            require(36 <= bits <= 64 and 0 <= start < (1 << bits) and
                    0 <= end < (1 << bits), 'timestamp range')
            require(int(row['available_start']) and int(row['available_end']), 'unavailable timestamps')
            require(all(math.isfinite(x) for x in (period, host, seconds)) and
                    period > 0 and 0 < seconds <= host < (1 << bits) * period * 1e-9,
                    'invalid timestamp duration')
            decoded = ((end - start) & ((1 << bits) - 1)) * period * 1e-9
            require(abs(decoded - seconds) <= max(1e-12, abs(decoded) * 1e-12), 'timestamp decode mismatch')
            slots = groups.setdefault(key, {})
            require((trial, ref) not in slots, 'duplicate paired slot')
            slots[trial, ref] = seconds
    require(count == 4224 and set(groups) == expected, 'missing color samples')
    results, failures, scaling = [], [], []
    for key in sorted(groups):
        slots = groups[key]
        require(set(slots) == {(t, r) for t in range(8) for r in (0, 1)}, 'incomplete pairs')
        pairs = [(slots[t, 1], slots[t, 0]) for t in range(1, 8)]
        ratios = [b / a for a, b in pairs]
        deltas = [(b - a) / key[3] for a, b in pairs]
        regression = (statistics.median(ratios) > 1.05 and
                      statistics.median(deltas) > 1e-7 and
                      sum(b / a > 1.05 and (b - a) / key[3] > 1e-7 for a, b in pairs) >= 5)
        item = dict(key=key, paired_median_ratio=statistics.median(ratios),
                    median_seconds_per_draw_delta=statistics.median(deltas),
                    repeatable_regression=regression,
                    reference_distribution=distribution([a for a, b in pairs]),
                    candidate_distribution=distribution([b for a, b in pairs]),
                    pairs=[dict(reference=a, candidate=b) for a, b in pairs])
        results.append(item)
        if regression:
            failures.append(item)
        if key[3] == workloads[0]:
            larger = groups[key[:3] + (workloads[1],)]
            for ref in (0, 1):
                ratio = statistics.median(larger[t, ref] for t in range(1, 8)) / statistics.median(slots[t, ref] for t in range(1, 8))
                scaling.append(dict(key=key[:3], reference=ref, ratio=ratio, valid=ratio >= 2))
    return dict(corpus_version=4, sample_count=count, reverse_order=reverse, dynamic_rendering=dynamic, packed_address=packed, self_check=self_check, workloads=workloads,
                accepted=not failures and all(x['valid'] for x in scaling),
                failures=failures, cases=results, work_scaling=scaling,
                threshold='paired median >5% and >100ns per draw, >=5/7 exceedances; work512/work64 >=2; declared before execution',
                coverage='132 procedural large-image single-sample color transfers; no cold-gameplay acceptance')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('csv')
    parser.add_argument('--out', required=True)
    parser.add_argument('--reverse', action='store_true')
    parser.add_argument('--dynamic', action='store_true')
    parser.add_argument('--packed', action='store_true')
    parser.add_argument('--self-check', action='store_true')
    parser.add_argument('--short-control', action='store_true')
    args = parser.parse_args()
    report = analyze(args.csv, args.reverse, args.dynamic, args.packed, args.self_check, args.short_control)
    Path(args.out).write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('Color GPU groups:', len(report['cases']), 'regressions:', len(report['failures']))
    raise SystemExit(0 if report['accepted'] else 1)
