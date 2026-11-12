"""Reject incomplete or mislabeled evidence before reporting a hardware pass."""
import csv
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from transfer_uint_warm_ci import qualify,source_manifest,comparison_refs
from test_transfer_uint_warm_analysis import samples,FIELDS


class Qualification(unittest.TestCase):
    def fixture(self,directory):
        root=Path(directory)
        for name,reference in (('reference',1),('prepared',0),('forward',0),('reverse',0)):
            (root/(name+'.log')).write_text(
                'TRANSFER_WARM_DEVICE Fixture GPU vendor1 device2 driver3 api4198400 native2=1 sampleShading=1 dynamicRendering=0\n'
                'TRANSFER_WARM_LIMITS storage4294967295 image32768\n'
                'TRANSFER_PARENT_LIBRARY /fixture/parent.so abi1 packet112\n'
                'PASS: 90 exact production-bank images, reference='+str(reference)+', 1x2x4x/native2 and raw format crossings\n',encoding='utf-8')
        for name,reverse in (('forward',False),('reverse',True)):
            with (root/(name+'.csv')).open('w',newline='') as file:
                writer=csv.DictWriter(file,fieldnames=FIELDS+['parent'])
                writer.writeheader()
                for row in samples(reverse):
                    row['parent']=1;writer.writerow(row)
        return root

    def test_complete(self):
        with tempfile.TemporaryDirectory() as directory:
            report=qualify(self.fixture(directory))
            self.assertTrue(report['accepted'])
            self.assertEqual(len(report['forward']['cases']),180)
            self.assertEqual(len(report['reverse']['cases']),180)

    def test_missing_images_or_changed_identity(self):
        for original,replacement in (('PASS: 90','PASS: 89'),('driver3','driver4'),('abi1','abi2'),('TRANSFER_PARENT_LIBRARY','UNRELATED_LIBRARY'),('vendor1 device2',''),('TRANSFER_WARM_DEVICE','MISSING_DEVICE')):
            with self.subTest(original=original),tempfile.TemporaryDirectory() as directory:
                root=self.fixture(directory);path=root/'reverse.log'
                path.write_text(path.read_text().replace(original,replacement))
                with self.assertRaises(ValueError):qualify(root)

    def test_legacy_mislabeled_as_parent(self):
        with tempfile.TemporaryDirectory() as directory:
            root=self.fixture(directory);path=root/'forward.csv'
            with path.open(newline='') as file:rows=list(csv.DictReader(file))
            rows[0]['parent']='0'
            with path.open('w',newline='') as file:
                writer=csv.DictWriter(file,fieldnames=FIELDS+['parent']);writer.writeheader();writer.writerows(rows)
            with self.assertRaises(ValueError):qualify(root)

    def test_first_parent_and_explicit_anchor(self):
        def resolve(source,*args):
            value=args[-1]
            if value=='H^1':return 'B'
            return {'base':'B','head':'H','anchor':'A','wrong':'W','B':'B','H':'H','A':'A'}[value.replace('^{commit}','')]
        with patch('transfer_uint_warm_ci.git',side_effect=resolve):
            self.assertEqual(comparison_refs('base','head'),('B','B','H'))
            self.assertEqual(comparison_refs('base','head','anchor',configured='anchor'),('B','A','H'))
            self.assertEqual(comparison_refs('head','head',self_check=True),('H','H','H'))
            for base,head,pinned,self_check,configured in (
                ('wrong','head',None,False,None),('base','head',None,True,None),
                ('base','head','anchor',False,None),('base','head','anchor',False,'wrong'),
                ('head','head','anchor',True,'anchor')):
                with self.assertRaises(ValueError):comparison_refs(base,head,pinned,self_check,configured)

    def test_source_identity_and_dirty_policy(self):
        with tempfile.TemporaryDirectory() as directory:
            source=Path(directory);(source/'libretro').mkdir()
            (source/'libretro/core_transfer_uint_vk.c').write_text('/* fixture */')
            with patch('transfer_uint_warm_ci.git',side_effect=('expected','wrong')):
                with self.assertRaises(ValueError):source_manifest(source,'revision')
            with patch('transfer_uint_warm_ci.git',side_effect=('expected','expected',' M file')):
                with self.assertRaises(ValueError):source_manifest(source,'revision')
            with patch('transfer_uint_warm_ci.git',side_effect=('expected','expected',' M file')):
                self.assertTrue(source_manifest(source,'revision',True)['dirty'])


if __name__=='__main__':unittest.main()
