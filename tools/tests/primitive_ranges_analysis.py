"""Strict component benchmark parsing; no gameplay/FPS acceptance."""
import csv
import math
import re
import statistics

SAMPLES = 7
RELATIVE_LIMIT = 0.05
ABSOLUTE_NS_LIMIT = 100.0
REQUIRED_EXCEEDANCES = 5


def profile(which):
    if which not in range(32):
        raise ValueError('unknown primitive case')
    topology = which // 8
    width = 4 if (which // 4) & 1 else 2
    count = 65535 if which & 2 else 510
    stride = 3 if which & 1 else 8
    def host(n):
        if topology == 1:
            return n + 1 if n >= 2 else 0
        if topology == 2:
            return n // 4 * 6
        return (n - 2) * 3 if n >= 3 else 0
    full, tail = divmod(count, stride)
    rows = full * (host(stride - 1) != 0) + (host(tail) != 0)
    total = full * host(stride - 1) + host(tail)
    return width, count, rows, total, 128 if which & 2 else 20000


def parse(log, wall_csv, expected_kind, reverse, cpu_group=0, cpu_index=2, negative=False):
    if expected_kind not in (1, 2):
        raise ValueError('unknown owner protocol')
    log = '\n'.join(log.splitlines())
    affinity = re.findall(r'^PRIMITIVE_AFFINITY group(\d+) cpu(\d+)$', log, re.M)
    if affinity != [(str(cpu_group), str(cpu_index))]:
        raise ValueError('missing/mismatched CPU affinity')
    control = re.findall(r'^PRIMITIVE_CONTROL slow([01])$', log, re.M)
    if control != [str(int(negative))]:
        raise ValueError('missing/unexpected regression-control work')
    owner = re.findall(r'^PRIMITIVE_OWNER (\d+) cases32 reverse([01])$', log, re.M)
    if owner != [(str(expected_kind), str(int(reverse)))]:
        raise ValueError('missing/wrong owner role or order')
    records = re.findall(r'^PRIMITIVE_CASE (\d+) width(\d+) input(\d+) rows(\d+) host(\d+) repeats(\d+)$', log, re.M)
    wanted = list(reversed(range(32))) if reverse else list(range(32))
    if [int(row[0]) for row in records] != wanted:
        raise ValueError('missing, duplicate or reordered correctness cases')
    for row in records:
        if tuple(map(int, row[1:])) != profile(int(row[0])):
            raise ValueError('wrong row/work/repetition profile')
    checksum = sum((profile(i)[3] ^ profile(i)[2]) * profile(i)[4] for i in range(32)) & 0xffffffff
    passed = re.findall(r'^PASS: persistent actual range collection, independent before/after rows, checksum(\d+)$', log, re.M)
    if passed != [str(checksum)]:
        raise ValueError('missing correctness completion or work checksum')
    result, order = {}, []
    for row in csv.reader(wall_csv.splitlines()):
        if len(row) != 3 or row[0] != 'primitive_ranges.warm':
            raise ValueError('unknown/malformed timing metric')
        which, elapsed = int(row[1]), float(row[2])
        if which in result or not math.isfinite(elapsed) or elapsed <= 0:
            raise ValueError('duplicate or invalid timing')
        result[which] = elapsed * 1e9 / profile(which)[4]
        order.append(which)
    if order != wanted:
        raise ValueError('incomplete or reordered timing cases')
    return result


def percentile(values, fraction):
    return sorted(values)[max(0, math.ceil(len(values) * fraction) - 1)]


def exceeds(base, head):
    return head > base * (1 + RELATIVE_LIMIT) and head - base > ABSOLUTE_NS_LIMIT


def compare(base, head):
    if len(base) != SAMPLES or len(head) != SAMPLES:
        raise ValueError('requires seven matched pairs')
    if any(set(sample) != set(range(32)) for sample in base + head):
        raise ValueError('incomplete sample corpus')
    cases, failures = [], []
    for which in range(32):
        before = [sample[which] for sample in base]
        after = [sample[which] for sample in head]
        if any(not math.isfinite(x) or x <= 0 for x in before + after):
            raise ValueError('invalid normalized sample')
        b, h = statistics.median(before), statistics.median(after)
        count = sum(exceeds(x, y) for x, y in zip(before, after))
        failed = exceeds(b, h) and count >= REQUIRED_EXCEEDANCES
        if failed:
            failures.append(which)
        cases.append({'case': which, 'profile': profile(which), 'base_ns': before,
                      'head_ns': after, 'base_median_ns': b, 'head_median_ns': h,
                      'base_p95_ns': percentile(before, .95),
                      'head_p95_ns': percentile(after, .95),
                      'base_p99_ns': percentile(before, .99),
                      'head_p99_ns': percentile(after, .99),
                      'base_max_ns': max(before), 'head_max_ns': max(after),
                      'ratio': h / b, 'exceedances': count, 'failed': failed})
    return {'accepted': not failures, 'failures': failures, 'cases': cases,
            'sample_pairs': SAMPLES, 'relative_limit': RELATIVE_LIMIT,
            'absolute_ns_per_call_limit': ABSOLUTE_NS_LIMIT,
            'required_exceedances': REQUIRED_EXCEEDANCES,
            'coverage': 'warm CPU range collection, not renderer/gameplay acceptance'}
