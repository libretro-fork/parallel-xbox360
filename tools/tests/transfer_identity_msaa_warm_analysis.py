"""Untimed analysis of matched, typed numeric-image GPU timestamps.

Use the existing integer-transfer gate unchanged: paired median >5% AND
>100ns/draw with >=5/7 paired exceedances; 512/64 work scaling >=2.
Trial zero is retained as warmup. These are procedural MSAA transfers only. Small diagnostic images never qualify.
"""
import argparse
import csv
import json
import math
from pathlib import Path
import statistics

from transfer_uint_warm_analysis import distribution


def analyze(path, reverse=False, extra_work=False, diagnostic=False, backend="vulkan"):
    if backend not in ("vulkan", "d3d12"):
        raise ValueError("unsupported backend")
    groups = {}
    rows = 0
    capabilities = None
    expected_keys = set()
    with Path(path).open(newline='', encoding='utf-8') as data:
        for row in csv.DictReader(data):
            rows += 1
            if rows > (6912 if backend == "d3d12" else 11520):
                raise ValueError('too many numeric GPU samples')
            view, scale, repeat = (int(row[field]) for field in ('view', 'scale', 'repeat'))
            trial, order, reference = (int(row[field]) for field in ('trial', 'order', 'reference'))
            if view not in range(4) or scale not in (1, 2, 4) or repeat not in (64, 512):
                raise ValueError('wrong numeric corpus')
            if trial not in range(8) or order not in (0, 1) or reference not in (0, 1):
                raise ValueError('wrong pairing metadata')
            if reference != int(((order ^ (trial & 1)) == 0) != reverse):
                raise ValueError('wrong alternating pair order')
            msaa, source, native, mode, native_cap, shading_cap, dynamic = (
                int(row[field]) for field in ('msaa', 'source_msaa', 'native', 'mode',
                                             'cap_native2', 'cap_shading', 'dynamic_rendering'))
            if msaa not in range(3) or source not in range(3) or native not in (0, 1) or mode not in (0, 1):
                raise ValueError('invalid sample shape')
            if native_cap not in (0, 1) or shading_cap not in (0, 1) or dynamic not in (0, 1):
                raise ValueError('invalid capabilities')
            if backend == "d3d12" and (mode or dynamic or not shading_cap):
                raise ValueError('D3D12 corpus requires builtin sample mode')
            if capabilities is None:
                capabilities = native_cap, shading_cap, dynamic
                expected_keys = {(n, v, d, a, z, k, repeat_count)
                    for n in range(native_cap + 1) for v in range(4)
                    for d in range(3) for a in range(3) for z in (1, 2, 4)
                    for k in range(2 if d and backend == "vulkan" else 1) if not (d and not shading_cap and not k)
                    for repeat_count in (64, 512)}
            if capabilities != (native_cap, shading_cap, dynamic):
                raise ValueError('capabilities changed during run')
            key = native, view, msaa, source, scale, mode, repeat
            if key not in expected_keys:
                raise ValueError('unsupported sample policy')
            tx, ty = (1, 1) if diagnostic else (16, 8)
            width, height = (80 * tx * scale) >> (msaa == 2), (16 * ty * scale) >> (msaa != 0)
            samples = 4 if msaa == 1 and not native else 1 << msaa
            source_samples = 4 if source == 1 and not native else 1 << source
            expected = dict(tiles_x=tx, tiles_y=ty, width=width, height=height,
                            samples=samples, source_samples=source_samples, active_samples=1 << msaa)
            if any(int(row[field]) != value for field, value in expected.items()):
                raise ValueError('wrong sample/image metadata')
            passes = (1 << msaa) if mode else 1
            pixels = width * height
            multiplier = 2 if extra_work and not reference else 1
            if int(row['pixels']) != pixels or int(row['work_multiplier']) != multiplier:
                raise ValueError('wrong image size or hidden extra work')
            if int(row['ia_primitives']) != 2 * repeat * multiplier * passes:
                raise ValueError('missing/excess geometry')
            fragment_factor = 1 if backend == "d3d12" else 1 << msaa
            if backend == "d3d12" and int(row['covered_samples']) != pixels * repeat * multiplier * (1 << msaa):
                raise ValueError('missing/excess covered samples')
            if int(row['vs_invocations']) <= 0 or int(row['fs_invocations']) < pixels * repeat * multiplier * fragment_factor:
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
            slots = groups.setdefault(key, {})
            if (trial, reference) in slots:
                raise ValueError('duplicate pair member')
            slots[trial, reference] = seconds
    if not expected_keys or set(groups) != expected_keys or rows != len(expected_keys) * 16 or any(len(slots) != 16 for slots in groups.values()):
        raise ValueError('incomplete numeric GPU corpus')
    cases, failures, scaling = [], [], []
    for key, slots in sorted(groups.items()):
        pairs = [(slots[i, 1], slots[i, 0]) for i in range(1, 8)]
        draws = key[-1] * ((1 << key[2]) if key[5] else 1)
        ratios = [candidate / parent for parent, candidate in pairs]
        deltas = [(candidate - parent) / draws for parent, candidate in pairs]
        repeatable = sum(candidate > parent * 1.05 and (candidate - parent) / draws > 1e-7
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
        if key[-1] == 64:
            for reference in (0, 1):
                small = statistics.median(slots[i, reference] for i in range(1, 8))
                large = statistics.median(groups[key[:-1] + (512,)][i, reference] for i in range(1, 8))
                scaling.append(dict(key=key[:-1], reference=reference, ratio=large / small, valid=large / small >= 2))
    return dict(corpus_version=1, backend=backend, sample_count=rows, reverse_order=reverse, extra_work_control=extra_work,
                accepted=not diagnostic and not failures and all(item['valid'] for item in scaling), failures=failures,
                work_scaling=scaling, cases=cases, threshold=__doc__.splitlines()[2:4],
                diagnostic=diagnostic, coverage='typed MSAA ownership transfers; no gameplay/cold-cache signoff')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--backend', choices=('vulkan', 'd3d12'), default='vulkan')
    parser.add_argument('--input', required=True)
    parser.add_argument('--out', required=True)
    parser.add_argument('--reverse', action='store_true')
    parser.add_argument('--extra-work', action='store_true')
    parser.add_argument('--diagnostic', action='store_true', help='validate small-image data without throughput acceptance')
    args = parser.parse_args()
    report = analyze(args.input, args.reverse, args.extra_work, args.diagnostic, args.backend)
    Path(args.out).write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('Numeric transfer groups:', len(report['cases']), 'regressions:', len(report['failures']))
    return 0 if report['accepted'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
