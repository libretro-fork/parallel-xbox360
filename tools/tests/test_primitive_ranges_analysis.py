import unittest
from primitive_ranges_analysis import compare, parse, profile


def fixture(kind=1, reverse=False):
    order = list(reversed(range(32))) if reverse else list(range(32))
    log = ['PRIMITIVE_AFFINITY group0 cpu2', 'PRIMITIVE_CONTROL slow0',
           'PRIMITIVE_OWNER %d cases32 reverse%d' % (kind, reverse)]
    csv = []
    for i in order:
        log.append('PRIMITIVE_CASE %d width%d input%d rows%d host%d repeats%d' % ((i,) + profile(i)))
        csv.append('primitive_ranges.warm,%d,0.1' % i)
    checksum = sum((profile(i)[3] ^ profile(i)[2]) * profile(i)[4] for i in range(32)) & 0xffffffff
    log.append('PASS: persistent actual range collection, independent before/after rows, checksum%d' % checksum)
    return '\n'.join(log), '\n'.join(csv)


class AnalysisTests(unittest.TestCase):
    def test_valid_roles_and_orders(self):
        for kind in (1, 2):
            for reverse in (False, True):
                self.assertEqual(set(parse(*fixture(kind, reverse), kind, reverse)), set(range(32)))

    def test_work_and_owner_rejections(self):
        log, csv = fixture()
        for bad in (log.replace('OWNER 1', 'OWNER 2'), log.replace('width2', 'width4', 1),
                    log.replace('cases32', 'cases31'), log.replace('checksum', 'wrongchecksum'),
                    log.replace('cpu2', 'cpu3'), log.replace('slow0', 'slow1')):
            with self.assertRaises(ValueError):
                parse(bad, csv, 1, False)

    def test_timing_rejections(self):
        log, csv = fixture()
        for bad in (csv + '\n' + csv.splitlines()[0], '\n'.join(csv.splitlines()[:-1]),
                    csv.replace('0.1', 'nan', 1), csv.replace('0.1', '0', 1)):
            with self.assertRaises(ValueError):
                parse(log, bad, 1, False)

    def test_strict_matched_sample_counts(self):
        good = [{i: 1000.0 for i in range(32)} for _ in range(7)]
        with self.assertRaises(ValueError):
            compare(good[:-1], good)

    def test_negative_control(self):
        base = [{i: 1000.0 for i in range(32)} for _ in range(7)]
        slow = [{i: 1200.0 if i == 13 else 1000.0 for i in range(32)} for _ in range(7)]
        self.assertEqual(compare(base, slow)['failures'], [13])
        self.assertTrue(compare(base, base)['accepted'])

    def test_single_noisy_pair_is_retained(self):
        base = [{i: 1000.0 for i in range(32)} for _ in range(7)]
        head = [{i: 9000.0 if i == 0 and n == 0 else 1000.0 for i in range(32)} for n in range(7)]
        result = compare(base, head)
        self.assertTrue(result['accepted'])
        self.assertEqual(result['cases'][0]['head_max_ns'], 9000.0)
        self.assertEqual(result['cases'][0]['exceedances'], 1)


if __name__ == '__main__':
    unittest.main()
