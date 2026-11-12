"""Qualification identity/failure controls; no timing or compiler required."""
import copy
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
import primitive_ranges_ci as gate


class ProtocolTests(unittest.TestCase):
    def test_source_identity_mutation_and_dirty_rejection(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            gpu = root/'src/xenia/gpu'
            gpu.mkdir(parents=True)
            for name in ('primitive_processor.cc', 'primitive_processor.h'):
                (gpu/name).write_text('parent')
            with patch.object(gate, 'git', side_effect=['abc', '']):
                before = gate.source_record(root, 'abc')
            self.assertEqual(before['kind'], 1)
            (root/'libretro').mkdir()
            (root/'libretro/core_primitive_ranges.h').write_text('fixed')
            with patch.object(gate, 'git', side_effect=['abc', '']):
                after = gate.source_record(root, 'abc')
            self.assertNotEqual(before, after)
            self.assertEqual(after['kind'], 2)
            for replies in (['wrong'], ['abc', ' M file']):
                with patch.object(gate, 'git', side_effect=replies), self.assertRaises(ValueError):
                    gate.source_record(root, 'abc')

    def test_build_and_link_input_identity(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            build = root/'build'
            libraries = build/'obj/Windows'
            libraries.mkdir(parents=True)
            archive = libraries/'xenia-gpu.lib'
            archive.write_bytes(b'actual')
            objects = build/'libretro/CMakeFiles/xenia-libretro.dir'
            objects.mkdir(parents=True)
            for name in ('core_events.c.obj', 'core_disc.c.obj'):
                (objects/name).write_bytes(b'object')
            (build/'CMakeCache.txt').write_text('CMAKE_MAKE_PROGRAM:FILEPATH=ninja\n')
            compiler = str((root/'cl.exe').resolve())
            record = {'build': str(build), 'options': {'CMAKE_BUILD_TYPE': 'Release',
                      'XENIA_ENABLE_LTO': 'OFF', 'CMAKE_CXX_COMPILER': gate.os.path.normcase(compiler)}}
            command = 'cl -I%s -I%s -c %s/src/xenia/gpu/primitive_processor.cc' % (build.as_posix(), root.as_posix(), root.as_posix())
            with patch.object(gate, 'native_build_record', side_effect=lambda *_: copy.deepcopy(record)), \
                 patch.object(gate.shutil, 'which', return_value=compiler), \
                 patch.object(gate.subprocess, 'check_output', return_value=command):
                before = gate.build_record(build, root)
                self.assertEqual(before['compile_command'], 'cl -I<BUILD> -I<SOURCE> -c <SOURCE>/src/xenia/gpu/primitive_processor.cc')
                archive.write_bytes(b'changed')
                after = gate.build_record(build, root)
                self.assertNotEqual(before['link_inputs'], after['link_inputs'])
                with patch.object(gate.subprocess, 'check_output', return_value=command+'\n'+command), self.assertRaises(ValueError):
                    gate.build_record(build, root)
                record['options']['XENIA_ENABLE_LTO'] = 'ON'
                with self.assertRaises(ValueError):
                    gate.build_record(build, root)

    def test_compile_flags_must_match(self):
        one = {'options': {'mode': 'Release'}, 'compile_command': 'cl /O2'}
        two = copy.deepcopy(one)
        gate.matched_builds([one, two])
        two['compile_command'] = 'cl /Od'
        with self.assertRaises(ValueError):
            gate.matched_builds([one, two])

    def test_controls_cannot_qualify_and_both_orders_required(self):
        for self_check, negative in ((False, False), (True, False), (False, True)):
            report = gate.finish_report({'forward': {'accepted': True}, 'reverse': {'accepted': True}}, self_check, negative)
            self.assertEqual(report['qualified'], not self_check and not negative)
        report = gate.finish_report({'forward': {'accepted': True}, 'reverse': {'accepted': False}}, False, False)
        self.assertFalse(report['qualified'])
        self.assertFalse(report['accepted'])

    def test_busy_runner_rejects(self):
        with patch.object(gate.subprocess, 'check_output', return_value='"retroarch"'), self.assertRaises(ValueError):
            gate.idle()

    def test_negative_requires_the_injected_case_in_both_orders(self):
        gate.validate_negative({'forward': {'failures': [0]}, 'reverse': {'failures': [0]}})
        for failed in ([], [19]):
            with self.assertRaises(ValueError):
                gate.validate_negative({'forward': {'failures': [0]}, 'reverse': {'failures': failed}})
        with patch.object(gate.subprocess, 'check_output', return_value=''):
            gate.idle()


if __name__ == '__main__':
    unittest.main()
