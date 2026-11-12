#!/usr/bin/env python3
"""Reject invalid provenance/coverage rather than accepting a misleading GPU pass."""
import tempfile
import unittest
from pathlib import Path
from unittest import mock
from transfer_uint_warm_d3d12_ci import build_record,validate_log,qualify


class Protocol(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        self.parent=self.root/'parent.dll'
        self.lines=[
            'MSAA_D3D12_ADAPTER hardware1 vendor4318 device11141 subsystem1392710754 revision161',
            'D3D12_WARM_DRIVER 00200000:00100835',
            'D3D12_WARM_ID luid00000000:00013bcb dynamic_control0 reverse0',
            'MSAA_D3D12 hardware1 native2=1',
            'D3D12_PARENT_LIBRARY abi1 kind1 packet4 validated_modules90 bytes194344 '+str(self.parent),
            'PASS: 90 warmed D3D12 cases, both image implementations, exact GPU work counts; 2880 raw samples']

    def log(self,lines=None,name='forward.log'):
        path=self.root/name
        path.write_text('\n'.join(self.lines if lines is None else lines)+'\n',encoding='utf-8')
        return path

    def test_real_hardware_and_complete_coverage(self):
        identity=validate_log(self.log(),False,False,self.parent,False)
        self.assertIn('hardware1',identity['adapter'])
        for old,new in [('00200000:00100835','00000000:00000000'),('luid00000000:00013bcb','luid00000000:00000000'),('hardware1','hardware0'),('native2=1','native2=0'),('90 warmed','89 warmed'),('2880 raw','2879 raw')]:
            with self.subTest(new=new),self.assertRaises(ValueError):
                validate_log(self.log([line.replace(old,new) for line in self.lines]),False,False,self.parent,False)

    def test_driver_missing_or_duplicated(self):
        for lines in ([line for line in self.lines if 'WARM_DRIVER' not in line],self.lines+[self.lines[1]]):
            with self.assertRaises(ValueError):validate_log(self.log(lines),False,False,self.parent,False)

    def test_wrong_producer_path_kind_or_validation(self):
        for old,new in [('parent.dll','other.dll'),('kind1 packet4','kind2 packet112'),('validated_modules90','validated_modules0'),('abi1','abi2')]:
            with self.subTest(new=new),self.assertRaises(ValueError):
                validate_log(self.log([line.replace(old,new) for line in self.lines]),False,False,self.parent,False)

    def test_wrong_order_or_control(self):
        for old,new in [('reverse0','reverse1'),('dynamic_control0','dynamic_control1')]:
            with self.assertRaises(ValueError):
                validate_log(self.log([line.replace(old,new) for line in self.lines]),False,False,self.parent,False)

    def test_mixed_devices_between_orders(self):
        self.log()
        self.log([line.replace('reverse0','reverse1').replace('device11141','device11142') for line in self.lines],'reverse.log')
        with mock.patch('transfer_uint_warm_d3d12_ci.analyze',return_value={'accepted':True,'cases':[]}):
            with self.assertRaises(ValueError):qualify(self.root,False)

    def test_failed_analysis_cannot_pass(self):
        self.log();self.log([line.replace('reverse0','reverse1') for line in self.lines],'reverse.log')
        with mock.patch('transfer_uint_warm_d3d12_ci.analyze',return_value={'accepted':False,'cases':[]}):
            self.assertFalse(qualify(self.root,False)['accepted'])

    def build(self,folder,compiler):
        path=self.root/folder;path.mkdir()
        (path/'obj/Windows').mkdir(parents=True)
        (path/'obj/Windows/xenia-native-dxil.lib').write_bytes(b'archive-v1')
        values=dict(CMAKE_HOME_DIRECTORY=str(self.root),CMAKE_BUILD_TYPE='Release',CMAKE_C_FLAGS='',CMAKE_C_FLAGS_RELEASE='/O2',
            CMAKE_CXX_FLAGS='/EHsc',CMAKE_CXX_FLAGS_RELEASE='/O2',CMAKE_C_COMPILER=compiler,CMAKE_CXX_COMPILER=compiler,XENIA_ENABLE_LTO='OFF')
        (path/'CMakeCache.txt').write_text('\n'.join(k+':STRING='+v for k,v in values.items())+'\n')
        return path

    def test_archive_identity_and_source_ownership(self):
        compiler=self.root/'fake-cl.exe';compiler.write_bytes(b'test compiler');compiler.chmod(0o700)
        path=self.build('build',str(compiler));first=build_record(path,self.root)
        (path/'obj/Windows/xenia-native-dxil.lib').write_bytes(b'changed')
        self.assertNotEqual(first,build_record(path,self.root))
        with self.assertRaises(ValueError):build_record(path,self.root/'other-source')

    def test_same_compiler_alias_is_not_a_build_difference(self):
        compiler=self.root/'fake-cl.exe';compiler.write_bytes(b'test compiler');compiler.chmod(0o700)
        one=self.build('one','cl');two=self.build('two',str(compiler))
        with mock.patch('transfer_uint_warm_d3d12_ci.shutil.which',return_value=str(compiler)):
            a=build_record(one,self.root);b=build_record(two,self.root)
        self.assertEqual(a['options'],b['options'])
        self.assertEqual(a['compiler_sha256'],b['compiler_sha256'])
        self.assertNotEqual(a['compiler_requested'],b['compiler_requested'])


if __name__=='__main__':unittest.main()
