#!/usr/bin/env python3
"""Offline injected oracle failures; no compiler or execution of test sources."""
import contextlib
import io
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
                         patch.object(m,runner,return_value=('timeout','',b'')):
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

    def test_braced_gcc_options(self):
        ct = self.modules[0]
        for directive in ('dg-options', 'dg-additional-options'):
            for options in ('"-fwrapv"', '{ "-fwrapv" }'):
                with self.subTest(directive=directive, options=options):
                    self.assertEqual(ct.dg_flags('/* { ' + directive + ' ' + options + ' } */'), ['-fwrapv'])
                    self.assertEqual(ct.dg_flags('/* { ' + directive + ' ' + options + ' { target aarch64*-*-* } } */'), ['-fwrapv'])
                    self.assertEqual(ct.dg_flags('/* { ' + directive + ' ' + options + ' { target s390x-linux-gnu } } */'), [])

    def test_guest_fault_is_reference_failure_with_diagnostic(self):
        ct = self.modules[0]
        with tempfile.TemporaryDirectory() as td:
            with patch.object(ct, 'OUT', td), patch.object(ct, 'sh', return_value=(0,b'',b'')), \
                 patch.object(ct, 'link', return_value=(0,'')), \
                 patch.object(ct, 'qemu', return_value=(125,'',b'runtime: unhandled guest exception\n')):
                result = self.invoke(ct, Path(td))
                self.assertEqual(result['skip'], 'ref-fail')
                self.assertIn('unhandled guest exception', result['why'])

    def test_native_signals_are_explicit_runtime_exclusions(self):
        m = self.modules[1]
        for rc, name in ((-6, 'SIGABRT'), (-11, 'SIGSEGV')):
            with self.subTest(signal=name), tempfile.TemporaryDirectory() as td:
                with patch.object(m, 'OUT', td), patch.object(m, 'sh', return_value=(0,b'',b'')), \
                     patch.object(m, 'link', return_value=(0,'')), \
                     patch.object(m, 'run', return_value=(rc,'',b'assertion detail')):
                    result = self.invoke(m, Path(td))
                    self.assertEqual(result['skip'], 'ref-runtime-fail')
                    self.assertEqual(result['rc'], rc)
                    self.assertEqual(result['phase'], 'native-run')
                    self.assertEqual(result['signal'], name)
                    self.assertEqual(result['stderr'], 'assertion detail')
                    self.assertIn(name, result['why'])
                    self.assertIn('assertion detail', result['why'])

    def test_runner_process_signal_remains_fatal(self):
        for m in (self.modules[0], self.modules[2]):
            runner = 'qemu' if m.__name__ == 'vcc-ctests' else 'run'
            with self.subTest(module=m.__name__), tempfile.TemporaryDirectory() as td:
                with patch.object(m, 'OUT', td), patch.object(m, 'sh', return_value=(0,b'',b'')), \
                     patch.object(m, 'link', return_value=(0,'')), \
                     patch.object(m, runner, return_value=(-11,'',b'runner crashed')):
                    result = self.invoke(m, Path(td))
                    self.assertEqual(result['error'], 'oracle-run')
                    self.assertIn('signal 11', result['why'])

    def test_wasm_explicit_label_requirement_precedes_compilers(self):
        wasm = self.modules[2]
        directive = '/* { dg-require-effective-target label_values } */'
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / 'case.c'
            path.write_text(directive + '\nint main(void){return 0;}')
            with patch.object(wasm, 'reference', side_effect=AssertionError('oracle must not run')), \
                 patch.object(wasm, 'sh', side_effect=AssertionError('compiler must not run')):
                result = wasm.one(('gcc', 'case', str(path)))
                self.assertEqual(result[1:3], ('skip', 'unsupported-target'))
                self.assertIn('label_values', result[3])
        for suite, text in [('vcc', directive), ('gcc', 'int label_values;'),
                            ('gcc', '/* { dg-require-effective-target indirect_jumps } */'),
                            ('gcc', '/* { dg-require-effective-target label_values { target x86_64-*-* } } */')]:
            with self.subTest(suite=suite, text=text):
                self.assertIsNone(wasm.unsupported_requirement(suite, text))
        for name in ('20071220-1', '20071220-2', '990208-1'):
            src = Path(wasm.T) / 'gcc-torture/execute' / (name + '.c')
            self.assertIn('label_values', wasm.unsupported_requirement('gcc', src.read_text()))


    def test_wasm_backend_fatal_is_not_a_capability_skip(self):
        wasm = self.modules[2]
        for message in ("WebAssembly hasn't implemented computed gotos",
                        'relocations for function or section offsets are only supported in metadata sections'):
            err = ('fatal error: error in backend: ' + message +
                   '\nPLEASE submit a bug report\nclang: error: clang frontend command failed with exit code 70').encode()
            with tempfile.TemporaryDirectory() as td, patch.object(wasm, 'OUT', td), \
                 patch.object(wasm, 'sh', return_value=(1, b'', err)):
                result = self.invoke(wasm, Path(td))
                self.assertEqual(result['error'], 'oracle-compile')
                self.assertFalse((Path(td) / 'ref/vcc/sample.json').exists())


    def test_wasm_unexpected_results_always_name_the_case(self):
        wasm = self.modules[2]
        for status in ('fail', 'error'):
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                wasm.report_case('gcc', 'example', status, 'oracle-run', 'timed out')
            self.assertIn(status.upper() + ' oracle-run gcc/example: timed out', output.getvalue())
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            wasm.report_case('gcc', 'example', 'xfail', 'run', 'known')
            wasm.report_case('gcc', 'example', 'skip', 'unsupported-target', 'label_values')
        self.assertEqual(output.getvalue(), '')


    def test_missing_tool(self):
        rc,_,err=self.modules[0].sh(['/no/such/compiler'])
        self.assertEqual(rc,'tool-error')
        self.assertIn(b'No such file',err)

if __name__ == '__main__':
    unittest.main(warnings="ignore")
