#!/usr/bin/env python3
"""Checkout-only checks for libc-test adapter and result classification."""
import importlib.util
import json
from pathlib import Path
import tempfile
import subprocess
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('libc_test', Path(__file__).with_name('vcc-libc-test.py'))
h = importlib.util.module_from_spec(spec)
spec.loader.exec_module(h)


class Harness(unittest.TestCase):
    def test_adapter_preserves_upstream_flags(self):
        with tempfile.TemporaryDirectory() as d:
            cfg = Path(d) / 'cfg.json'
            cfg.write_text(json.dumps(dict(mode='vcc', clang='clang', vpp='vpp', target='aarch64-linux-gnu', includes=['-isystem', '/headers'], logs=d, compile_timeout=7)))
            args = ['-std=c99', '-frounding-math', '-fno-builtin', '-fPIC', '-DSHARED', '-c', '-o', 'test.lo', 'test.c']
            with patch.object(h, 'execute', return_value=0) as run:
                self.assertEqual(h.compiler(cfg, args), 0)
                compile_cmd = run.call_args_list[0].args[0]
                self.assertEqual(compile_cmd, ['vpp', '--target=aarch64-linux-gnu', '-isystem', '/headers', *args[:5], '-S', '-o', 'test.lo.s', 'test.c'])
                self.assertEqual(run.call_args_list[1].args[0], ['clang', '--target=aarch64-linux-gnu', '-c', 'test.lo.s', '-o', 'test.lo'])
            with patch.object(h, 'execute', return_value=1) as run:
                self.assertEqual(h.compiler(cfg, args), 1)
                self.assertEqual(run.call_count, 1)

    def test_link_keeps_support_libraries(self):
        with tempfile.TemporaryDirectory() as d:
            cfg = Path(d) / 'cfg.json'
            cfg.write_text(json.dumps(dict(mode='vcc', clang='clang', target='x86_64-linux-gnu', logs=d, compile_timeout=7)))
            args = ['-rdynamic', '-o', 'test.exe', 'test.o', 'libtest.a', '-lcrypt', '-ldl']
            with patch.object(h, 'execute', return_value=0) as run:
                h.compiler(cfg, args)
                self.assertEqual(run.call_args.args[0], ['clang', '--target=x86_64-linux-gnu', *args])

    def test_missing_artifacts_never_pass(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            self.assertEqual(h.outcomes(root, ['test']), {'test': 'build-fail'})
            (root / 'test.exe').touch()
            self.assertEqual(h.outcomes(root, ['test']), {'test': 'harness-error'})
            (root / 'test.run-status').write_text('1')
            (root / 'test.err').write_text('FAIL test [status 1]\n')
            self.assertEqual(h.outcomes(root, ['test']), {'test': 'runtime-fail'})
            (root / 'test.err').write_text('')
            self.assertEqual(h.outcomes(root, ['test']), {'test': 'runtime-fail'})
            (root / 'test.run-status').write_text('0')
            self.assertEqual(h.outcomes(root, ['test']), {'test': 'pass'})

    def test_compiler_abnormal_exit_is_not_reference_rejection(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            scenarios = [(-11, 'signal'), (124, 'tool-error'),
                         (127, 'tool-error'), (1, None)]
            for rc, error in scenarios:
                log = root / 'stage.json'
                with patch.object(h.subprocess, 'run', return_value=subprocess.CompletedProcess(['cc'], rc, stderr=b'')):
                    h.execute(['cc'], 1, log)
                self.assertEqual(json.loads(log.read_text())['error'], error)
                errors = h.stage_errors(root)
                self.assertEqual(bool(errors), error is not None)
                self.assertEqual(h.outcomes(root, ['test'], errors)['test'],
                                 'build-error' if error else 'build-fail')
                self.assertEqual(h.failed_run({'pass': 2, 'reference-build-fail': 1}, errors), bool(error))

    def test_timeout_and_missing_executable_are_errors(self):
        with tempfile.TemporaryDirectory() as d:
            log = Path(d) / 'stage.json'
            for exc, kind in [(subprocess.TimeoutExpired(['cc'], 1), 'timeout'),
                              (FileNotFoundError('missing compiler'), 'tool-error')]:
                with patch.object(h.subprocess, 'run', side_effect=exc):
                    self.assertNotEqual(h.execute(['cc'], 1, log), 0)
                self.assertEqual(json.loads(log.read_text())['error'], kind)
                self.assertTrue(h.failed_run({'pass': 1}, h.stage_errors(Path(d))))

    def test_linker_tool_failure_is_not_a_c_rejection(self):
        with tempfile.TemporaryDirectory() as d:
            log = Path(d) / 'stage.json'
            with patch.object(h.subprocess, 'run', return_value=subprocess.CompletedProcess(['clang'], 1, stderr=b'clang: error: unable to execute command: Executable "ld" does not exist')):
                h.execute(['clang'], 1, log)
            self.assertTrue(h.stage_errors(Path(d)))

    def test_make_rejections_do_not_mask_infrastructure_errors(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            commands = root / 'commands'
            commands.mkdir()
            (commands / 'compile.json').write_text(json.dumps({'returncode': 1, 'error': None}))
            log = root / 'make.log'
            log.write_text('make: *** [Makefile:93: build/test.exe] Error 1\n')
            observed = {'ok': 'pass', 'invalid': 'build-fail'}
            self.assertFalse(h.make_error(2, log, commands, observed))
            self.assertFalse(h.failed_run({'pass': 1, 'reference-build-fail': 1}, []))
            self.assertTrue(h.make_error(2, log, commands, {'ok': 'pass'}))
            log.write_text("make: *** No rule to make target 'missing.o'. Stop.\n")
            self.assertTrue(h.make_error(2, log, commands, observed))
            log.write_text('make: *** [Makefile:93: build/test.exe] Error 127\n')
            self.assertTrue(h.make_error(2, log, commands, observed))
            self.assertTrue(h.make_error(-11, log, commands, observed))


if __name__ == '__main__':
    unittest.main()
