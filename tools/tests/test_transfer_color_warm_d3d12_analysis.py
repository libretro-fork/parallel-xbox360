import copy
import csv
from pathlib import Path
import tempfile
import unittest
from transfer_color_warm_d3d12_analysis import FIELDS,analyze,expected_cases


def rows(control=False,self_check=False,reverse=False):
    result=[]
    for sx,sy,n,s,d,c,repeat in sorted(expected_cases(control)):
        base_width,base_height=(160,16) if control else (2560,64)
        sw=base_width*(1 if n&1 else sx);sh=base_height*(1 if n&1 else sy)
        dw=base_width*(1 if n&2 else sx);dh=base_height*(1 if n&2 else sy)
        if not control:sw>>=int(s in (5,15));dw>>=int(d in (5,15))
        for trial in range(2 if control else 8):
            for order in range(2):
                ref=int((((order^(trial&1))==0)!=reverse))
                result.append(dict(zip(FIELDS,(
                    int(not control),int(control),int(self_check),int(reverse),sx,sy,n,s,d,c,sw,sh,
                    4 if control else 32,4 if control else 32,dw,dh,repeat,trial,order,ref,
                    1000000000,100,100+10000*repeat,1.0,6*repeat,2*repeat,6*repeat,dw*dh*repeat))))
    return result


class AnalysisTests(unittest.TestCase):
    def check(self,data,**kwargs):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'test.csv'
            with path.open('w',newline='') as f:
                writer=csv.DictWriter(f,fieldnames=FIELDS);writer.writeheader();writer.writerows(data)
            return analyze(path,**kwargs)

    def test_complete_domain_and_controls_cannot_qualify_performance(self):
        result=self.check(rows());self.assertTrue(result['accepted']);self.assertEqual(result['sample_count'],12960)
        self.assertEqual(len(result['cases']),810)
        control=self.check(rows(True,True),controls_only=True,self_check=True,hardware=False)
        self.assertTrue(control['controls_valid']);self.assertFalse(control['accepted'])
        with self.assertRaises(ValueError):self.check(rows(True,True))
        null=self.check(rows(self_check=True),self_check=True)
        self.assertTrue(null['controls_valid']);self.assertFalse(null['accepted'])
        self.assertTrue(self.check(rows(reverse=True),reverse=True)['accepted'])

    def test_regression_and_insufficient_gpu_work_scaling_rejected(self):
        data=rows()
        for row in data:
            if not row['reference']:row['end']=100+20000*row['repeat']
        self.assertFalse(self.check(data)['accepted'])
        data=rows()
        for row in data:row['end']=100+10000*64
        self.assertFalse(self.check(data)['accepted'])

    def test_missing_duplicate_foreign_mode_and_metadata_rejected(self):
        data=rows(True)
        for item in (data[:-1],data+[data[0]]):
            with self.assertRaises(ValueError):self.check(item,controls_only=True,hardware=False)
        mutations=dict(ia_vertices=6,ia_primitives=1,vs_invocations=0,ps_invocations=0,
                       end=100,start=-1,frequency=0,host_seconds='nan',order=2,
                       reference=2,source_pitch=0,dest_pitch=5,source_width=1,width=0,
                       native_class=3,value_convert=1,source=14,self_check=1,hardware=1)
        for field,value in mutations.items():
            changed=copy.deepcopy(data);changed[0][field]=value
            with self.subTest(field=field),self.assertRaises(ValueError):
                self.check(changed,controls_only=True,hardware=False)

    def test_clock_change_and_missing_host_interval_rejected(self):
        data=rows(True);data[0]['frequency']=2000000000
        with self.assertRaises(ValueError):self.check(data,controls_only=True,hardware=False)
        data=rows(True);data[0]['host_seconds']=None
        with self.assertRaises(ValueError):self.check(data,controls_only=True,hardware=False)


if __name__=='__main__':unittest.main()
