#!/usr/bin/env python3
"""Check LLVM directory selection and the actual upstream output comparator."""
import importlib.util
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('small', __file__.replace('test-vcc-small.py', 'vcc-small.py'))
small = importlib.util.module_from_spec(spec)
spec.loader.exec_module(small)


class LLVMSettings(unittest.TestCase):
    def options(self, relative, target='x86_64'):
        small.TARGET = f'--target={target}-linux-gnu'
        return small.llvm_options(small.SUITES / 'llvm-test-suite/SingleSource' / relative)

    def test_arguments_and_architecture(self):
        self.assertEqual(self.options('UnitTests/SignlessTypes/shr.c')[1], ['31415926'])
        self.assertEqual(self.options('UnitTests/Vector/SSE/sse.shift.c')[2], '0.016')
        self.assertTrue(self.options('UnitTests/Vector/SSE/sse.shift.c', 'aarch64')[3])
        self.assertFalse(self.options('UnitTests/Vector/NEON/simple.c', 'aarch64')[3])
        self.assertTrue(self.options('UnitTests/ms_struct-bitfield.c', 'aarch64')[3])

    def test_unknown_and_disabled_are_not_counted(self):
        self.assertTrue(self.options('UnitTests/NewDirectory/test.c')[3])
        self.assertTrue(self.options('UnitTests/SetjmpLongjmp/C/WhileLoop.c')[3])
        self.assertTrue(self.options('UnitTests/Integer/a.c')[3])

    def test_upstream_comparison(self):
        with tempfile.TemporaryDirectory() as d:
            small.FPCMP = small.build_fpcmp(d)
            self.assertFalse(small.matches(b'1.001\nexit 0\n', b'1.0\nexit 0\n', d))
            self.assertTrue(small.matches(b'1.001\nexit 0\n', b'1.0\nexit 0\n', d, '0.016'))
            self.assertFalse(small.matches(b'1.1\nexit 0\n', b'1.0\nexit 0\n', d, '0.016'))
            self.assertFalse(small.matches(b'1.0\nexit 1\n', b'1.0\nexit 0\n', d, '0.016'))
            self.assertFalse(small.matches(b'wrong 1.0\n', b'right 1.0\n', d, '0.016'))


if __name__ == '__main__':
    unittest.main()
