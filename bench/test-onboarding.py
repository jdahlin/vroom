#!/usr/bin/env python3
"""The onboarding verifier must distinguish useful diagnostics from snippets."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('onboarding', Path(__file__).with_name('vcc-onboarding.py'))
onboarding = importlib.util.module_from_spec(spec)
spec.loader.exec_module(onboarding)


class DiagnosticContracts(unittest.TestCase):
    def setUp(self):
        self.want = [{'line': 3, 'severity': 'warning', 'pattern': 'address.*int'}]
        self.record = {'level': 'warning', 'message': 'expected address of an int',
                       'location': {'file': 'broken.c', 'line': 3, 'column': 5}}

    def test_meaning_location_and_level(self):
        check = onboarding.check_expectations
        self.assertEqual(check([self.record], self.want, 'broken.c'), [])
        for key, value in [('line', 4), ('file', 'fixed.c')]:
            bad = dict(self.record, location=dict(self.record['location'], **{key: value}))
            self.assertEqual(check([bad], self.want, 'broken.c'), self.want)
        bad = dict(self.record, level='note')
        self.assertEqual(check([bad], self.want, 'broken.c'), self.want)

    def test_source_or_rendered_text_cannot_satisfy_contract(self):
        bad = dict(self.record, message='unrelated warning', rendered='expected address of an int')
        self.assertEqual(onboarding.check_expectations([bad], self.want, 'broken.c'), self.want)

    def test_structured_help_and_children_are_evidence(self):
        helped = dict(self.record, message='incorrect argument', help='pass an address of an int')
        self.assertEqual(onboarding.check_expectations([helped], self.want, 'broken.c'), [])
        parent = {'level': 'error', 'children': [self.record]}
        self.assertEqual(onboarding.check_expectations([parent], self.want, 'broken.c'), [])

    def test_clang_parser_does_not_read_quoted_error_as_diagnostic(self):
        text = ('broken.c:3:5: warning: expected an address\n'
                '    3 | puts("x.c:8:9: error: example");\n')
        self.assertEqual(len(onboarding.clang_records(text)), 1)

    def test_empty_or_partial_corpus_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'exactly 25'):
            onboarding.validate_manifest([])


if __name__ == '__main__':
    unittest.main()
