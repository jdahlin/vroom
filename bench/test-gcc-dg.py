#!/usr/bin/env python3
"""Check gcc.dg harness contract selection independently of compiler results."""
import contextlib
import importlib.util
import io
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('gcc_dg', Path(__file__).with_name('vcc-gcc-dg.py'))
dg = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dg)


class Configuration(unittest.TestCase):
    def config(self, source, target='x86_64-linux-gnu', torture=False):
        return dg.configuration(source, target, torture)

    def test_root_default_and_replacement(self):
        self.assertEqual(self.config('{ dg-do run }')[:3], ('run', ['-ansi', '-pedantic-errors'], []))
        self.assertEqual(self.config('{ dg-do run }\n{ dg-options "-std=c99 -O2" }')[:3], ('run', ['-std=c99', '-O2'], []))
        self.assertEqual(self.config('{ dg-do run }\n{ dg-additional-options "-O2" }')[1], ['-ansi', '-pedantic-errors', '-O2'])

    def test_zero_limit_rejected_before_discovery(self):
        with patch.object(dg.sys, 'argv', ['vcc-gcc-dg.py', '--limit', '0']), contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as raised:
                dg.main()
            self.assertEqual(raised.exception.code, 2)

    def test_torture_default(self):
        self.assertEqual(self.config('', torture=True)[:3], ('compile', [], []))

    def test_conditional_defaults(self):
        src = '{ dg-options "-std=c99" { target aarch64*-*-* } }'
        self.assertEqual(self.config(src)[1], ['-ansi', '-pedantic-errors'])
        self.assertEqual(self.config(src, 'aarch64-linux-gnu')[1], ['-std=c99'])

    def test_companion(self):
        self.assertEqual(self.config('{ dg-do run }\n{ dg-additional-sources "a.c b.c" }')[2], ['a.c', 'b.c'])
        self.assertEqual(self.config('{ dg-additional-sources "a.c" { target aarch64*-*-* } }')[2], [])
        self.assertIsNone(self.config('{ dg-additional-sources "a.c" { target mystery } }')[0])

    def test_contracts_not_dropped(self):
        for source in ('{ dg-do run }\n{ dg-output "expected text" }',
                       '{ dg-final { scan-assembler "foo" } }',
                       '{ dg-error "expected error" }',
                       '{ dg-do compile }\n{ dg-do run }',
                       '{ dg-do run { target mystery } }'):
            self.assertIsNone(self.config(source)[0], source)

    def test_nested_selector(self):
        src = '{ dg-do run { target { x86_64-*-* && { ! ia32 } } } }'
        self.assertEqual(self.config(src)[0], 'run')
        self.assertIsNone(self.config(src, 'aarch64-linux-gnu')[0])


class ErrorRows(unittest.TestCase):
    def test_line_contracts(self):
        source = '\n'.join(['int a; /* { dg-line first } */',
                             'int b; /* { dg-error "error" } */',
                             '/* { dg-error "other" "" { target *-*-* } .-2 } */',
                             '/* { dg-error "again" "" { target *-*-* } first } */'])
        flags, rows, reason = dg.error_configuration(source, 'x86_64-linux-gnu')
        self.assertEqual(flags, ['-ansi', '-pedantic-errors'])
        self.assertEqual(rows, {1, 2})
        self.assertFalse(reason)

    def test_target_contracts(self):
        src = '{ dg-error "error" "" { target aarch64*-*-* } }'
        self.assertEqual(dg.error_configuration(src, 'x86_64-linux-gnu')[1], set())
        self.assertEqual(dg.error_configuration(src, 'aarch64-linux-gnu')[1], {1})
        self.assertIsNone(dg.error_configuration(src.replace('aarch64*-*-*', 'mystery'), 'x86_64-linux-gnu')[0])

    def test_unsupported_contracts(self):
        for source in ('{ dg-error "driver" "" { target *-*-* } 0 }',
                       '{ dg-error "error" "" { target *-*-* } missing_label }',
                       '#line 4 "other.c"\n{ dg-error "error" }',
                       '{ dg-error "error" }\n{ dg-prune-output ".*" }',
                       '{ dg-do run }\n{ dg-error "error" }'):
            self.assertIsNone(dg.error_configuration(source, 'x86_64-linux-gnu')[0], source)

    def test_all_error_rows_and_no_notes(self):
        rows, unlocated = dg.error_locations('a.c:3:2: error: first\na.c:3:8: error: second\n'
            'inc.h:9: error: extra\na.c:5:2: note: error: described here\n'
            '  6 | error: source excerpt\nclang: error: bad option\n', '/tmp')
        self.assertEqual(rows, {('/tmp/a.c', 3), ('/tmp/inc.h', 9)})
        self.assertEqual(unlocated, ['clang: error: bad option'])
        self.assertEqual(dg.compare_error_rows({('a.c', 1)}, {('a.c', 2)}),
                         ([('a.c', 1)], [('a.c', 2)]))

    def run_case(self, ref, actual):
        with tempfile.TemporaryDirectory() as wd:
            root = Path(wd)
            source = root / 'case.c'
            source.write_text('int a; /* { dg-error "error" } */\nint b;\n')
            def output(rows, rc=None):
                return (int(bool(rows)) if rc is None else rc, b'', ''.join(f'{source}:{r}:1: error: message\n' for r in rows))
            args = SimpleNamespace(target='x86_64-linux-gnu', compile_timeout=1)
            with patch.object(dg, 'ROOT', root), patch.object(dg, 'invoke', side_effect=[output(ref), output(actual)]):
                return dg.one_errors(source, args, [], 'vpp')

    def test_complete_oracle_comparison(self):
        self.assertEqual(self.run_case([1], [1])['status'], 'pass')
        self.assertEqual(self.run_case([], [])['status'], 'pass')
        missing = self.run_case([1], [])
        self.assertEqual(missing['status'], 'fail')
        self.assertEqual(missing['missing_error_rows'][0][1], 1)
        extra = self.run_case([1], [1, 2])
        self.assertEqual(extra['status'], 'fail')
        self.assertEqual(extra['extra_error_rows'][0][1], 2)
        self.assertEqual(self.run_case([], [1])['status'], 'fail')
        self.assertEqual(self.run_case([2], [2])['status'], 'skip')


if __name__ == '__main__':
    unittest.main()
