#!/usr/bin/env python3
"""Offline injected oracle failures; no compiler or execution of test sources."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parent

def load(name):
    spec = importlib.util.spec_from_file_location(name, HERE / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

class OracleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.modules = [load(n) for n in ('vcc-ctests', 'vcc-x86', 'vcc-wasm')]

    def invoke(self, m, root):
        path = root / 'sample.c'
        path.write_text('int main(void) { return 0; }\n')
        args = ('vcc', 'sample', str(path), [])
        if m.__name__ == 'vcc-ctests':
            args += ('runtime-hash',)
        return m.reference(*args, str(root))

    def test_compile_failures_are_errors_and_not_cached(self):
        failures = [('timeout', b'', 'timed out after 60s'),
                    (-11, b'', 'signal 11'),
                    ('tool-error', b'No such file', 'could not execute'),
                    (1, b'', 'no compiler diagnostic'),
                    (1, b'clang: error: frontend command failed due to signal', 'tool failure')]
        for m in self.modules:
            for rc, err, why in failures:
                with self.subTest(module=m.__name__, rc=rc), tempfile.TemporaryDirectory() as td:
                    root = Path(td)
                    with patch.object(m, 'OUT', td), patch.object(m, 'sh', return_value=(rc,b'',err)) as sh:
                        result = self.invoke(m, root)
                        self.assertEqual(result['error'], 'oracle-compile')
                        self.assertIn(why, result['why'])
                        self.assertFalse((root / 'ref/vcc/sample.json').exists())
                        self.invoke(m, root)
                        self.assertEqual(sh.call_count, 2)

    def test_real_language_rejection_remains_skip(self):
        for m in self.modules:
            with self.subTest(module=m.__name__), tempfile.TemporaryDirectory() as td:
                with patch.object(m, 'OUT', td), patch.object(m, 'sh', return_value=(1,b'',b'error: unsupported type')):
                    result = self.invoke(m, Path(td))
                    self.assertEqual(result['skip'], 'clang-rejects')
                    self.assertTrue((Path(td)/'ref/vcc/sample.json').exists())

    def test_link_and_run_failures(self):
        for m in self.modules:
            for stage in ('link', 'run'):
                with self.subTest(module=m.__name__,stage=stage), tempfile.TemporaryDirectory() as td:
                    runner = 'qemu' if m.__name__ == 'vcc-ctests' else 'run'
                    with patch.object(m,'OUT',td), patch.object(m,'sh',return_value=(0,b'',b'')), \
                         patch.object(m,'link',return_value=('timeout','') if stage=='link' else (0,'')), \
                         patch.object(m,runner,return_value=(-9,'',b'killed')):
                        result=self.invoke(m,Path(td))
                        self.assertEqual(result['error'],'oracle-'+stage)
                        self.assertFalse((Path(td)/'ref/vcc/sample.json').exists())

    def test_case_timeout_is_explicit_and_bounded(self):
        ct=self.modules[0]
        with tempfile.TemporaryDirectory() as td:
            p=Path(td)/'case.c'
            for text,expected in [('int x;',60),('/* vcc-ref-timeout: 300 */',300)]:
                p.write_text(text)
                self.assertEqual(ct.reference_timeout(p),expected)
            p.write_text('/* vcc-ref-timeout: 601 */')
            with self.assertRaises(ValueError): ct.reference_timeout(p)

    def test_error_propagates_through_result_and_xfail_wrappers(self):
        for m in self.modules:
            with self.subTest(module=m.__name__), tempfile.TemporaryDirectory() as td:
                path=Path(td)/'sample.c'
                path.write_text('/* vcc-xfail: known bug */\n/* vcc-xfail-wasm: known bug */\nint main(void){return 0;}')
                if m.__name__=='vcc-ctests': m.init(False,'runtime-hash')
                with patch.object(m,'OUT',td), patch.object(m,'reference',return_value={'error':'oracle-compile','why':'timed out after 60s'}):
                    result=m.one(('vcc','sample',str(path)))
                    self.assertEqual(result[1:3],('error','oracle-compile'))

    def test_link_silent_failure_and_execution_timeout(self):
        ct=self.modules[0]
        self.assertEqual(ct.oracle_error(1, '', 'link', 60)['error'],'oracle-link')
        self.assertIn('20s',ct.oracle_error('timeout', '', 'run', 20)['why'])
        self.assertIsNone(ct.oracle_error(1, 'undefined symbol: missing', 'link', 60))
        self.assertIsNone(ct.oracle_error(1, '', 'run', 20))

    def test_missing_tool(self):
        rc,_,err=self.modules[0].sh(['/no/such/compiler'])
        self.assertEqual(rc,'tool-error')
        self.assertIn(b'No such file',err)

if __name__ == '__main__':
    unittest.main(warnings="ignore")
