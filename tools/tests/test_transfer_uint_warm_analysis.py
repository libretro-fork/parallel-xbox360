"""Contract failures must not silently turn into a passing timing report."""
import csv
import tempfile
import unittest
from pathlib import Path
from transfer_uint_warm_analysis import analyze,distribution


FIELDS='pair msaa scale pixels samples repeat trial order reference start available_start end available_end bits period_ns host_seconds gpu_seconds ia_primitives vs_invocations fs_invocations available_stats'.split()


def samples(reverse=False):
    rows=[]
    for pair in range(10):
        df=(6,4,7,5,4,14,5,15,5,4)[pair]
        for msaa in range(3):
            for scale in (1,2,4):
                pixels=512*((80*scale)>>(int(df in (5,7,15))+int(msaa==2)))*((16*scale)>>int(msaa!=0))
                for repeat in (64,512):
                    for trial in range(8):
                        for order in (0,1):
                            reference=int(((order^(trial&1))==0) != reverse)
                            ticks=100000*(repeat//64)
                            values=(pair,msaa,scale,pixels,1<<msaa,repeat,trial,order,reference,1024,1,1024+ticks,1,64,1,0.1,ticks*1e-9,2*repeat,6*repeat,pixels*(1<<msaa)*repeat,1)
                            rows.append(dict(zip(FIELDS,values)))
    return rows


class Contract(unittest.TestCase):
    def run_rows(self,rows,reverse=False):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'samples.csv'
            with path.open('w',newline='') as file:
                writer=csv.DictWriter(file,fieldnames=FIELDS)
                writer.writeheader();writer.writerows(rows)
            return analyze(path,reverse)

    def test_both_orders(self):
        for reverse in (False,True):
            report=self.run_rows(samples(reverse),reverse)
            self.assertTrue(report['accepted'])
            self.assertEqual(len(report['cases']),180)

    def test_distribution(self):
        report=distribution([4.,1.,3.,2.,7.,6.,5.])
        self.assertEqual(report['median'],4.)
        self.assertAlmostEqual(report['p95'],6.7)
        self.assertAlmostEqual(report['p99'],6.94)
        self.assertEqual(report['maximum'],7.)
        self.assertEqual(report['outlier_trials'],[5,6,7,1,3,4,2])

    def test_wrong_reference(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'samples.csv'
            with path.open('w',newline='') as file:
                writer=csv.DictWriter(file,fieldnames=FIELDS)
                writer.writeheader();writer.writerows(samples())
            with self.assertRaises(ValueError):analyze(path,parent=True)

    def test_wrong_order(self):
        with self.assertRaises(ValueError):self.run_rows(samples(True))

    def test_missing_and_duplicate(self):
        rows=samples()
        with self.assertRaises(ValueError):self.run_rows(rows[:-1])
        rows[-1]=rows[0]
        with self.assertRaises(ValueError):self.run_rows(rows)

    def test_invalid_evidence(self):
        for field,value in (('available_start',0),('available_end',0),('available_stats',0),('ia_primitives',0),('vs_invocations',0),('fs_invocations',0),('period_ns',0),('gpu_seconds','nan'),('host_seconds',0),('end',1<<64),('pixels',0)):
            rows=samples();rows[0][field]=value
            with self.subTest(field=field),self.assertRaises(ValueError):self.run_rows(rows)

    def test_repeatability_boundary(self):
        for count in (4,5,7):
            rows=samples()
            for row in rows:
                if (row['pair'],row['msaa'],row['scale'],row['repeat'],row['reference'])==(0,0,1,64,0) and 1<=row['trial']<=count:
                    row['end']=row['start']+200000
                    row['gpu_seconds']=0.0002
            report=self.run_rows(rows)
            self.assertEqual(report['accepted'],count<5)
            self.assertEqual(len(report['failures']),int(count>=5))

    def test_insufficient_work_scaling(self):
        rows=samples()
        for row in rows:
            if row['repeat']==512:
                row['end']=row['start']+100000
                row['gpu_seconds']=0.0001
        report=self.run_rows(rows)
        self.assertFalse(report['accepted'])
        self.assertTrue(any(not item['valid'] for item in report['work_scaling']))


if __name__=='__main__':unittest.main()
