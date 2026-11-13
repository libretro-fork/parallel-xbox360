#!/usr/bin/env python3
"""Negative controls for GPU color performance acceptance, not runtime code."""
import csv
from pathlib import Path
import tempfile
import unittest
from transfer_color_warm_analysis import analyze

FIELDS = ('scale source dest width height repeat trial order reference start '
          'available_start end available_end bits period_ns host_seconds gpu_seconds '
          'ia_primitives vs_invocations fs_invocations available_stats dynamic_rendering packed_address same_pipeline').split()


class ColorGateTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name) / 'samples.csv'
        self.rows = []
        for scale, (sx, sy) in enumerate(((1, 1), (3, 5), (4, 4))):
            for sf in (0, 1, 2, 3, 4, 5, 14, 15):
                for df in (0, 1, 2, 3, 4, 5, 14, 15):
                    if sf == df or (sf >= 4 and df >= 4):
                        continue
                    w, h = 2560 * sx // (2 if df in (5, 15) else 1), 64 * sy
                    for repeat in (64, 512):
                        for trial in range(8):
                            for order in range(2):
                                ref = int((order ^ (trial & 1)) == 0)
                                ticks = repeat * 10000
                                self.rows.append(dict(zip(FIELDS, (scale, sf, df, w, h, repeat,
                                    trial, order, ref, 100, 1, 100 + ticks, 1, 64, 1,
                                    1, ticks * 1e-9, 2 * repeat, 6 * repeat, w * h * repeat, 1, 0, 0, 0))))
        self.assertEqual(len(self.rows), 4224)

    def run_rows(self, reverse=False, dynamic=False, packed=False, self_check=False):
        with self.path.open('w', newline='') as stream:
            writer = csv.DictWriter(stream, fieldnames=FIELDS)
            writer.writeheader()
            writer.writerows(self.rows)
        return analyze(self.path, reverse, dynamic, packed, self_check)

    def test_pipeline_control_identity(self):
        self.rows[0]['same_pipeline'] = 1
        with self.assertRaises(ValueError):
            self.run_rows()
        for row in self.rows:
            row['same_pipeline'] = 1
        self.assertTrue(self.run_rows(self_check=True)['accepted'])

    def test_producer_identity(self):
        self.rows[0]['packed_address'] = 1
        with self.assertRaises(ValueError):
            self.run_rows()
        for row in self.rows:
            row['packed_address'] = 1
        self.assertTrue(self.run_rows(packed=True)['accepted'])

    def test_rendering_identity(self):
        self.rows[0]['dynamic_rendering'] = 1
        with self.assertRaises(ValueError):
            self.run_rows()
        for row in self.rows:
            row['dynamic_rendering'] = 1
        self.assertTrue(self.run_rows(dynamic=True)['accepted'])

    def test_equal_work_passes(self):
        report = self.run_rows()
        self.assertTrue(report['accepted'])
        self.assertEqual(len(report['cases']), 264)

    def test_reversed_pairs(self):
        for row in self.rows:
            row['reference'] ^= 1
        self.assertTrue(self.run_rows(True)['accepted'])
        with self.assertRaises(ValueError):
            self.run_rows()

    def test_missing_and_duplicate_rejected(self):
        row = self.rows.pop()
        with self.assertRaises(ValueError):
            self.run_rows()
        self.rows.append(dict(self.rows[-1]))
        with self.assertRaises(ValueError):
            self.run_rows()
        self.rows[-1] = row

    def test_lost_work_and_timestamp_rejected(self):
        for field, bad in (('fs_invocations', 0), ('ia_primitives', 0),
                           ('available_stats', 0), ('available_start', 0),
                           ('available_end', 0), ('width', 1), ('gpu_seconds', float('nan'))):
            old = self.rows[0][field]
            self.rows[0][field] = bad
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.run_rows()
            self.rows[0][field] = old

    def test_repeatable_slowdown_fails(self):
        for row in self.rows:
            if not row['reference'] and row['trial']:
                row['end'] += row['repeat'] * 2000
                row['gpu_seconds'] = (row['end'] - row['start']) * 1e-9
        report = self.run_rows()
        self.assertFalse(report['accepted'])
        self.assertEqual(len(report['failures']), 264)

    def test_initial_warmup_retained_but_excluded(self):
        for row in self.rows:
            if not row['trial'] and not row['reference']:
                row['end'] += 1000000
                row['gpu_seconds'] = (row['end'] - row['start']) * 1e-9
        self.assertTrue(self.run_rows()['accepted'])


if __name__ == '__main__':
    unittest.main()
