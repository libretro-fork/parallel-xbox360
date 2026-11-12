"""Untimed analysis of matched, typed numeric-image GPU timestamps.

Use the existing integer-transfer gate unchanged: paired median >5% AND
>100ns/draw with >=5/7 paired exceedances; 512/64 work scaling >=2.
Trial zero is retained as warmup. These are procedural SS transfers only.
"""
import argparse
import csv
import json
import math
from pathlib import Path
import statistics

from transfer_uint_warm_analysis import distribution


def analyze(path, reverse=False, extra_work=False):
    groups = {}
    rows = 0
    with Path(path).open(newline='', encoding='utf-8') as data:
        for row in csv.DictReader(data):
            rows += 1
            if rows > 384:
                raise ValueError('too many numeric GPU samples')
            view, scale, repeat = (int(row[field]) for field in ('view', 'scale', 'repeat'))
            trial, order, reference = (int(row[field]) for field in ('trial', 'order', 'reference'))
            if view not in range(4) or scale not in (1, 2, 4) or repeat not in (64, 512):
                raise ValueError('wrong numeric corpus')
            if trial not in range(8) or order not in (0, 1) or reference not in (0, 1):
                raise ValueError('wrong pairing metadata')
            if reference != int(((order ^ (trial & 1)) == 0) != reverse):
                raise ValueError('wrong alternating pair order')
            pixels = 320 * 208 * scale * scale
            multiplier = 2 if extra_work and not reference else 1
            if int(row['pixels']) != pixels or int(row['work_multiplier']) != multiplier:
                raise ValueError('wrong image size or hidden extra work')
            if int(row['ia_primitives']) != 2 * repeat * multiplier:
                raise ValueError('missing/excess geometry')
            if int(row['vs_invocations']) <= 0 or int(row['fs_invocations']) < pixels * repeat * multiplier:
                raise ValueError('missing vertex/fragment work')
            if not all(int(row[key]) == 1 for key in ('available_stats', 'available_start', 'available_end')):
                raise ValueError('unavailable GPU queries')
            bits, period = int(row['bits']), float(row['period_ns'])
            start, end = int(row['start']), int(row['end'])
            seconds, host = float(row['gpu_seconds']), float(row['host_seconds'])
            if not (36 <= bits <= 64 and math.isfinite(period) and period > 0):
                raise ValueError('invalid timestamp device')
            if not (0 <= start < 1 << bits and 0 <= end < 1 << bits):
                raise ValueError('timestamp range')
            if not (math.isfinite(seconds) and math.isfinite(host) and 0 < seconds <= host):
                raise ValueError('invalid elapsed interval')
            if host >= (1 << bits) * period * 1e-9:
                raise ValueError('ambiguous GPU wrap')
            decoded = ((end - start) & ((1 << bits) - 1)) * period * 1e-9
            if abs(decoded - seconds) > max(1e-12, abs(decoded) * 1e-12):
                raise ValueError('incorrect timestamp decoding')
            slots = groups.setdefault((view, scale, repeat), {})
            if (trial, reference) in slots:
                raise ValueError('duplicate pair member')
            slots[trial, reference] = seconds
    if rows != 384 or len(groups) != 24 or any(len(slots) != 16 for slots in groups.values()):
        raise ValueError('incomplete numeric GPU corpus')
    cases, failures, scaling = [], [], []
    for key, slots in sorted(groups.items()):
        pairs = [(slots[i, 1], slots[i, 0]) for i in range(1, 8)]
        ratios = [candidate / parent for parent, candidate in pairs]
        deltas = [(candidate - parent) / key[2] for parent, candidate in pairs]
        repeatable = sum(candidate > parent * 1.05 and (candidate - parent) / key[2] > 1e-7
                         for parent, candidate in pairs) >= 5
        item = dict(key=key, paired_median_ratio=statistics.median(ratios),
                    median_seconds_per_draw_delta=statistics.median(deltas),
                    repeatable_regression=repeatable,
                    reference_distribution=distribution([a for a, b in pairs]),
                    candidate_distribution=distribution([b for a, b in pairs]),
                    pairs=[dict(reference=a, candidate=b) for a, b in pairs])
        cases.append(item)
        if item['paired_median_ratio'] > 1.05 and item['median_seconds_per_draw_delta'] > 1e-7 and repeatable:
            failures.append(item)
        if key[2] == 64:
            for reference in (0, 1):
                small = statistics.median(slots[i, reference] for i in range(1, 8))
                large = statistics.median(groups[key[:2] + (512,)][i, reference] for i in range(1, 8))
                scaling.append(dict(key=key[:2], reference=reference, ratio=large / small, valid=large / small >= 2))
    return dict(corpus_version=1, sample_count=rows, reverse_order=reverse, extra_work_control=extra_work,
                accepted=not failures and all(item['valid'] for item in scaling), failures=failures,
                work_scaling=scaling, cases=cases, threshold=__doc__.splitlines()[2:4],
                coverage='typed numeric SS ownership transfers; no MSAA throughput or gameplay/cold-cache signoff')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', required=True)
    parser.add_argument('--out', required=True)
    parser.add_argument('--reverse', action='store_true')
    parser.add_argument('--extra-work', action='store_true')
    args = parser.parse_args()
    report = analyze(args.input, args.reverse, args.extra_work)
    Path(args.out).write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('Numeric transfer groups:', len(report['cases']), 'regressions:', len(report['failures']))
    return 0 if report['accepted'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
