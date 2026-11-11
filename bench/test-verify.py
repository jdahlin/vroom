#!/usr/bin/env python3
"""Check that external diagnostic contracts do not require clang wording."""
import importlib.util
from pathlib import Path
import re
import unittest

spec = importlib.util.spec_from_file_location('vcc_verify', Path(__file__).with_name('vcc-verify.py'))
verify = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verify)


class DiagnosticContracts(unittest.TestCase):
    def setUp(self):
        self.path = '/tmp/example.c'
        self.expected = [('error', self.path, 3, 1, 1, 'clang wording')]
        self.actual = [('error', self.path, 3, 'vcc explains the same error differently')]

    def check(self, actual=None, expected=None):
        return verify.check(self.expected if expected is None else expected,
                            self.actual if actual is None else actual,
                            self.path, match_text=False)

    def test_default_contract_accepts_independent_wording(self):
        self.assertEqual(self.check(), ([], []))

    def test_optional_message_contract_still_checks_clang_reference(self):
        missing, extra = verify.check(self.expected, self.actual, self.path)
        self.assertEqual((len(missing), len(extra)), (1, 1))

    def test_regex_is_also_ignored_for_location_contract(self):
        expected = [('error', self.path, 3, 1, 1, re.compile('clang.*wording'))]
        self.assertEqual(self.check(expected=expected), ([], []))

    def test_wrong_severity_file_or_line_is_rejected(self):
        for actual in [('warning', self.path, 3, 'x'),
                       ('error', '/tmp/other.c', 3, 'x'),
                       ('error', self.path, 4, 'x')]:
            with self.subTest(actual=actual):
                missing, extra = self.check([actual])
                self.assertEqual((len(missing), len(extra)), (1, 1))

    def test_missing_and_additional_diagnostics_are_rejected(self):
        self.assertTrue(self.check([])[0])
        self.assertTrue(self.check(self.actual * 2)[1])

    def test_count_range_and_any_line_remain_enforced(self):
        expected = [('error', self.path, None, 2, 3, 'unused wording')]
        self.assertEqual(self.check(self.actual * 2, expected), ([], []))
        self.assertTrue(self.check(self.actual, expected)[0])
        self.assertTrue(self.check(self.actual * 4, expected)[1])

    def test_no_diagnostics_contract_does_not_hide_errors(self):
        self.assertEqual(self.check([], []), ([], []))
        self.assertTrue(self.check(expected=[])[1])


if __name__ == '__main__':
    unittest.main()
