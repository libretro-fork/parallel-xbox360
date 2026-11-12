#!/usr/bin/env python3
"""Test-only raw sample validation; no core or build-time dependency."""
from pathlib import Path
import argparse
import csv
import json
import math
import statistics

def distribution(values):
    ordered=sorted(values)
    def percentile(q):
        position=(len(ordered)-1)*q
        lower=int(position)
        upper=min(lower+1,len(ordered)-1)
        return ordered[lower]+(ordered[upper]-ordered[lower])*(position-lower)
    return dict(samples=len(ordered),median=statistics.median(ordered),p95=percentile(.95),p99=percentile(.99),maximum=ordered[-1],outlier_trials=sorted(range(1,len(values)+1),key=lambda trial:values[trial-1],reverse=True))


def analyze(input_path, reverse=False, parent=False):
    groups = {}
    row_count = 0
    with Path(input_path).open(newline='', encoding='utf-8') as samples:
        for row in csv.DictReader(samples):
            row_count += 1
            if row_count > 2880:
                raise ValueError('too many warmed-transfer samples')
            if int(row.get('parent', '0')) != int(parent):
                raise ValueError('wrong reference implementation')
            key = tuple((int(row[x]) for x in ('pair', 'msaa', 'scale', 'repeat')))
            trial = int(row['trial'])
            reference = int(row['reference'])
            order = int(row['order'])
            if not (0 <= key[0] < 10 and 0 <= key[1] < 3 and (key[2] in (1, 2, 4)) and (key[3] in (64, 512))):
                raise ValueError('invalid warmed-transfer sample or corpus')
            if not (0 <= trial < 8 and reference in (0, 1) and (order in (0, 1))):
                raise ValueError('invalid warmed-transfer sample or corpus')
            if not reference == int((order ^ trial & 1 == 0) != reverse):
                raise ValueError('invalid warmed-transfer sample or corpus')
            df = (6, 4, 7, 5, 4, 14, 5, 15, 5, 4)[key[0]]
            tw = 80 * key[2] >> int(df in (5, 7, 15)) + int(key[1] == 2)
            th = 16 * key[2] >> int(key[1] != 0)
            if not (int(row['pixels']) == 512 * tw * th and int(row['samples']) == 1 << key[1]):
                raise ValueError('invalid warmed-transfer sample or corpus')
            if not (int(row['available_stats']) and int(row['ia_primitives']) == 2 * key[3] and (int(row['vs_invocations']) > 0)):
                raise ValueError('invalid warmed-transfer sample or corpus')
            if not int(row['fs_invocations']) >= int(row['pixels']) * int(row['samples']) * key[3]:
                raise ValueError('invalid warmed-transfer sample or corpus')
            if not (int(row['available_start']) and int(row['available_end'])):
                raise ValueError('invalid warmed-transfer sample or corpus')
            bits = int(row['bits'])
            period = float(row['period_ns'])
            if not (36 <= bits <= 64 and math.isfinite(period) and (period > 0)):
                raise ValueError('invalid warmed-transfer sample or corpus')
            start = int(row['start'])
            end = int(row['end'])
            if not (0 <= start < 1 << bits and 0 <= end < 1 << bits):
                raise ValueError('timestamp range')
            seconds = float(row['gpu_seconds'])
            host = float(row['host_seconds'])
            if not (math.isfinite(seconds) and 0 < seconds <= host and math.isfinite(host)):
                raise ValueError('invalid warmed-transfer sample or corpus')
            if not host < (1 << bits) * period * 1e-09:
                raise ValueError('invalid warmed-transfer sample or corpus')
            decoded = (end - start & (1 << bits) - 1) * period * 1e-09
            if not abs(decoded - seconds) <= max(1e-12, abs(decoded) * 1e-12):
                raise ValueError('invalid warmed-transfer sample or corpus')
            slots = groups.setdefault(key, {})
            if not (trial, reference) not in slots:
                raise ValueError('invalid warmed-transfer sample or corpus')
            slots[trial, reference] = seconds
    if row_count != 2880:
        raise ValueError('missing warmed-transfer samples')
    if not len(groups) == 180:
        raise ValueError('invalid warmed-transfer sample or corpus')
    results = []
    failures = []
    scaling = []
    for key, slots in sorted(groups.items()):
        if not len(slots) == 16:
            raise ValueError('invalid warmed-transfer sample or corpus')
        pairs = [(slots[i, 1], slots[i, 0]) for i in range(1, 8)]
        deltas = [(b - a) / key[3] for a, b in pairs]
        ratios = [b / a for a, b in pairs]
        med = statistics.median(ratios)
        delta = statistics.median(deltas)
        repeatable = sum((b > a * 1.05 and (b - a) / key[3] > 1e-07 for a, b in pairs)) >= 5
        item = dict(key=key, reference_median=statistics.median((a for a, b in pairs)), generic_median=statistics.median((b for a, b in pairs)), paired_median_ratio=med, median_seconds_per_draw_delta=delta, repeatable_regression=repeatable, reference_distribution=distribution([a for a,b in pairs]), candidate_distribution=distribution([b for a,b in pairs]), pairs=[dict(reference=a, generic=b) for a, b in pairs])
        results.append(item)
        if med > 1.05 and delta > 1e-07 and repeatable:
            failures.append(item)
        if key[3] == 64:
            for ref in (0, 1):
                small = statistics.median((slots[i, ref] for i in range(1, 8)))
                large = statistics.median((groups[key[:3] + (512,)][i, ref] for i in range(1, 8)))
                scaling.append(dict(key=key[:3], reference=ref, ratio=large / small, valid=large / small >= 2))
    report = dict(corpus_version=1, sample_count=row_count, reverse_order=bool(reverse), parent_reference=bool(parent), accepted=not failures and all((x['valid'] for x in scaling)), failures=failures, work_scaling=scaling, cases=results, coverage='procedural ownership transfers only; no gameplay smoothness claim', threshold='paired median >5% and >100ns per draw, >=5/7 paired exceedances; work512/work64 >=2; declared before execution')
    return report

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', required=True)
    parser.add_argument('--out', required=True)
    parser.add_argument('--reverse', action='store_true')
    parser.add_argument('--parent', action='store_true')
    args = parser.parse_args()
    report = analyze(args.input, args.reverse, args.parent)
    Path(args.out).write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('Warmed transfer groups:', len(report['cases']), 'regressions:', len(report['failures']))
    return 0 if report['accepted'] else 1
if __name__ == '__main__':
    raise SystemExit(main())
