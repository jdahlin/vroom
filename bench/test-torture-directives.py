#!/usr/bin/env python3
"""Regression checks for conservative target filtering in the torture harness."""
import tempfile
from pathlib import Path
import unittest
from torture_directives import flags_for, legacy_options, selector, words, replaces_default_options


class Directives(unittest.TestCase):
    def test_target_triples(self):
        self.assertTrue(selector('x86_64-*-linux*', 'x86_64-linux-gnu'))
        self.assertTrue(selector('aarch64*-*-*', 'aarch64-linux-gnu'))
        self.assertFalse(selector('arm*-*-*', 'aarch64-linux-gnu'))

    def test_unknown_logic(self):
        self.assertIsNone(selector(['!', 'mystery'], 'x86_64-linux-gnu'))
        self.assertFalse(selector(['m68k-*-*', '&&', 'coldfire_fpu'], 'x86_64-linux-gnu'))
        self.assertIsNone(selector(['x86_64-*-*', '&&', 'mystery'], 'x86_64-linux-gnu'))
        self.assertTrue(selector(['mystery', '||', 'x86_64-*-*'], 'x86_64-linux-gnu'))

    def test_conditional_flags(self):
        src = '/* { dg-options "-msse" { target { i?86-*-* x86_64-*-* } } } */'
        self.assertEqual(flags_for(src)[0], ['-msse'])
        self.assertEqual(flags_for(src, 'aarch64-linux-gnu')[0], [])
        self.assertFalse(replaces_default_options(src, 'aarch64-linux-gnu'))
        self.assertTrue(replaces_default_options(src, 'x86_64-linux-gnu'))
        src = '/* { dg-options "-msse" { target { ! mystery } } } */'
        self.assertIsNone(flags_for(src)[0])

    def test_skip_and_xfail(self):
        self.assertEqual(flags_for('{ dg-skip-if "bpf only" { bpf-*-* } }')[0], [])
        self.assertIsNone(flags_for('{ dg-skip-if "all" { *-*-* } }')[0])
        self.assertEqual(flags_for('{ dg-xfail-if "known bug" { *-*-* } }')[0], [])
        self.assertIsNone(flags_for('{ dg-skip-if "flags" { *-*-* } { "-O2" } }')[0])
        self.assertEqual(flags_for('{ dg-skip-if "flags" { bpf-*-* } { "-O2" } }')[0], [])

    def test_action_and_unknown_contract(self):
        self.assertEqual(flags_for('{ dg-do run }', action='run')[0], [])
        self.assertIsNone(flags_for('{ dg-do run }')[0])
        self.assertIsNone(flags_for('{ dg-error "message" }')[0])
        self.assertIsNone(flags_for('{ dg-require-effective-target mystery }')[0])
        self.assertEqual(flags_for('{ dg-require-effective-target int128 }')[0], [])

    def test_malformed(self):
        for src in ['{ dg-options "unterminated }', '{ dg-options "-g"',
                    '{ dg-do compile } { dg-options "-g" }']:
            self.assertIsNone(flags_for(src)[0])
        self.assertEqual(words('{ target { ia32 && sse2_runtime } }'),
                         [['target', ['ia32', '&&', 'sse2_runtime']]])

    def test_legacy_script(self):
        with tempfile.TemporaryDirectory() as wd:
            path = Path(wd) / 'test.x'
            path.write_text('set additional_flags -fno-builtin-abs\nreturn 0\n')
            self.assertEqual(legacy_options(path, '0'), (['-fno-builtin-abs'], ''))
            path.write_text('load_lib target-supports.exp\nif [istarget "nvptx-*-*"] {\nreturn 1\n}\nreturn 0\n')
            self.assertEqual(legacy_options(path, '0'), (None, ''))
            path.write_text('set unknown_flag 1\nreturn 0\n')
            self.assertTrue(legacy_options(path, '0')[1])


if __name__ == '__main__':
    unittest.main()
