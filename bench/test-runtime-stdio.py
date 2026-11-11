#!/usr/bin/env python3
"""Native stdio ABI regression, compiled and executed by Clang and vcc."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('x86', Path(__file__).with_name('vcc-x86.py'))
x86 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(x86)

class NativeStdio(unittest.TestCase):
    def test_reopen_and_scan(self):
        with tempfile.TemporaryDirectory(prefix='vroom-native-stdio-') as td, \
             patch.object(x86, 'OUT', td), patch.object(x86, 'RTO', td + '/rt.o'), \
             patch.object(x86, 'NOCACHE', True):
            x86.rt_obj()
            result = x86.one(('vcc', 'stdio-native', x86.ct.RT + '/stdio-native.c'))
            self.assertEqual(result[1], 'pass', result)

if __name__ == '__main__':
    unittest.main(warnings='ignore')
