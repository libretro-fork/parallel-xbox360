"""Offline rejection tests for numeric GPU evidence, not GPU performance claims."""
import csv
from pathlib import Path
import tempfile
import unittest

from transfer_identity_msaa_warm_analysis import analyze


from test_transfer_identity_msaa_warm_analysis import corpus as vulkan_corpus


def corpus(extra=False, flat=False, diagnostic=False, native_cap=1, shading_cap=1):
    rows = [row for row in vulkan_corpus(extra, flat, diagnostic, native_cap, shading_cap)
            if row['mode'] == 0]
    for row in rows:
        row['covered_samples'] = row['fs_invocations']
        row['fs_invocations'] //= 1 << row['msaa']
    return rows


class EvidenceTest(unittest.TestCase):
    def report(self, rows, **kwargs):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'samples.csv'
            with path.open('w', newline='', encoding='utf-8') as output:
                writer = csv.DictWriter(output, fieldnames=rows[0].keys())
                writer.writeheader()
                writer.writerows(rows)
            return analyze(path, backend="d3d12", **kwargs)

    def test_equal_work_wrap_and_warmup(self):
        rows = corpus()
        report = self.report(rows)
        self.assertTrue(report['accepted'])
        self.assertEqual((report['sample_count'], len(report['cases'])), (6912, 432))
        # The retained first trial must not manufacture a measured regression.
        for row in rows:
            if row['trial'] == 0 and not row['reference']:
                row['gpu_seconds'] *= 4
                row['end'] = (row['start'] + int(round(row['gpu_seconds'] * 1e9))) % (1 << 36)
        self.assertTrue(self.report(rows)['accepted'])

    def test_diagnostic_cannot_qualify(self):
        rows = corpus(diagnostic=True)
        with self.assertRaises(ValueError):
            self.report(rows)
        self.assertFalse(self.report(rows, diagnostic=True)['accepted'])

    def test_sparse_capabilities(self):
        for native in (0, 1):
            for shading in (1,):
                self.assertTrue(self.report(corpus(native_cap=native, shading_cap=shading))['accepted'])

    def test_reject_coverage(self):
        for value in (0, 1, -1):
            rows = corpus()
            rows[0]['covered_samples'] = value
            with self.assertRaises(ValueError):
                self.report(rows)

    def test_reject_sample_metadata(self):
        for field, value in dict(msaa=3, source_msaa=3, native=2, mode=1,
                samples=2, source_samples=2, active_samples=2, cap_native2=0,
                cap_shading=0, dynamic_rendering=2, tiles_x=1, tiles_y=1, width=1, height=1).items():
            with self.subTest(field=field):
                rows = corpus()
                rows[0][field] = value
                with self.assertRaises(ValueError):
                    self.report(rows)

    def test_real_extra_work_and_flat_response(self):
        rows = corpus(extra=True)
        with self.assertRaises(ValueError):
            self.report(rows)
        report = self.report(rows, extra_work=True)
        self.assertFalse(report['accepted'])
        self.assertEqual(len(report['failures']), 432)
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
