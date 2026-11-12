"""Offline rejection tests for numeric GPU evidence, not GPU performance claims."""
import csv
from pathlib import Path
import tempfile
import unittest

from transfer_identity_warm_analysis import analyze


def corpus(extra=False, flat=False):
    rows = []
    for view in range(4):
        for scale in (1, 2, 4):
            for repeat in (64, 512):
                for trial in range(8):
                    for order in (0, 1):
                        reference = int((order ^ (trial & 1)) == 0)
                        multiplier = 2 if extra and not reference else 1
                        ticks = (64000 if flat else repeat * 1000) * multiplier
                        start = (1 << 36) - 100
                        pixels = 320 * 208 * scale * scale
                        rows.append(dict(view=view, scale=scale, pixels=pixels, repeat=repeat,
                                         trial=trial, order=order, reference=reference, start=start,
                                         available_start=1, end=(start + ticks) % (1 << 36),
                                         available_end=1, bits=36, period_ns=1,
                                         host_seconds=.01, gpu_seconds=ticks * 1e-9,
                                         ia_primitives=2 * repeat * multiplier,
                                         vs_invocations=24,
                                         fs_invocations=pixels * repeat * multiplier,
                                         available_stats=1, work_multiplier=multiplier))
    return rows


class EvidenceTest(unittest.TestCase):
    def report(self, rows, **kwargs):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'samples.csv'
            with path.open('w', newline='', encoding='utf-8') as output:
                writer = csv.DictWriter(output, fieldnames=rows[0].keys())
                writer.writeheader()
                writer.writerows(rows)
            return analyze(path, **kwargs)

    def test_equal_work_wrap_and_warmup(self):
        rows = corpus()
        report = self.report(rows)
        self.assertTrue(report['accepted'])
        self.assertEqual((report['sample_count'], len(report['cases'])), (384, 24))
        # The retained first trial must not manufacture a measured regression.
        for row in rows:
            if row['trial'] == 0 and not row['reference']:
                row['gpu_seconds'] *= 4
                row['end'] = (row['start'] + int(round(row['gpu_seconds'] * 1e9))) % (1 << 36)
        self.assertTrue(self.report(rows)['accepted'])

    def test_real_extra_work_and_flat_response(self):
        rows = corpus(extra=True)
        with self.assertRaises(ValueError):
            self.report(rows)
        report = self.report(rows, extra_work=True)
        self.assertFalse(report['accepted'])
        self.assertEqual(len(report['failures']), 24)
        self.assertFalse(self.report(corpus(flat=True))['accepted'])

    def test_reject_missing_duplicate_and_order(self):
        rows = corpus()
        with self.assertRaises(ValueError):
            self.report(rows[:-1])
        rows[-1] = rows[0].copy()
        with self.assertRaises(ValueError):
            self.report(rows)
        with self.assertRaises(ValueError):
            self.report(corpus(), reverse=True)

    def test_reject_corrupt_queries_and_hidden_work(self):
        corruptions = dict(pixels=1, work_multiplier=2, ia_primitives=1,
                           vs_invocations=0, fs_invocations=0, available_stats=0,
                           available_start=0, available_end=0, bits=35,
                           period_ns=float('nan'), start=-1, end=1,
                           host_seconds=0, gpu_seconds=float('nan'))
        for field, value in corruptions.items():
            with self.subTest(field=field):
                rows = corpus()
                rows[0][field] = value
                with self.assertRaises(ValueError):
                    self.report(rows)


if __name__ == '__main__':
    unittest.main()
