#!/usr/bin/env python3
"""The onboarding verifier must distinguish useful diagnostics from snippets."""
import importlib.util
from pathlib import Path
import tempfile
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
            onboarding.validate_cases([])

    def test_every_case_is_one_file_with_its_sources_and_messages(self):
        cases = onboarding.load_cases()
        onboarding.validate_cases(cases)
        for case in cases:
            self.assertEqual(Path(case['_path']).suffix, '.test')
            for variant in ('broken', 'fixed', 'valid'):
                self.assertIn(case[variant], case['_sections'])
            styles = ('link',) if case['phase'] == 'link' else ('vcc', 'narrated')
            for target in onboarding.TARGETS:
                for style in styles:
                    self.assertIsNotNone(onboarding.expected_message(case, target, style))

    def test_exact_wording_and_display_are_verified(self):
        expected = "broken.c:3:5: error: expected ';'\n    3 | int n\n      |      ^\nnote: declaration ends here\n"
        self.assertIn('missing expected message', onboarding.compare_message(None, expected, 'test'))
        self.assertIsNone(onboarding.compare_message(expected, expected, 'test'))
        for actual in (expected.replace('expected', 'missing'),
                       expected.replace('      ^', '     ^'),
                       expected.replace('note: declaration ends here\n', '')):
            self.assertIn('snapshot differs', onboarding.compare_message(expected, actual, 'test'))

    def test_empty_expectation_does_not_hide_new_output(self):
        self.assertIsNone(onboarding.compare_message('', '', 'test'))
        self.assertIn('snapshot differs', onboarding.compare_message('', 'warning: integer division\n', 'test'))

    def test_fixture_roundtrip_preserves_exact_text_and_deduplicates_targets(self):
        with tempfile.TemporaryDirectory() as td:
            case = dict(onboarding.load_cases()[0])
            case['_path'] = str(Path(td) / 'example.test')
            case['_sections'] = dict(case['_sections'])
            diagnostic = 'broken.c:2:14: error: example\n      |       ^  \n\n'
            for target in onboarding.TARGETS:
                case['_sections']['stderr.' + target] = diagnostic
            onboarding.save_case(case)
            loaded = onboarding.parse_case(Path(case['_path']).read_text(), Path(case['_path']))
            self.assertEqual(loaded['_sections']['broken.c'], case['_sections']['broken.c'])
            self.assertEqual(loaded['_sections']['stderr'], diagnostic)
            self.assertNotIn('stderr.' + onboarding.TARGETS[0], loaded['_sections'])
            for target in onboarding.TARGETS:
                self.assertEqual(onboarding.expected_message(loaded, target, 'vcc'), diagnostic)

    def test_duplicate_sections_are_rejected(self):
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            onboarding.parse_case('id="B01"\n=== stderr ===\nfirst\n=== stderr ===\nsecond\n', Path('test'))


if __name__ == '__main__':
    unittest.main()
