#!/usr/bin/env python3
"""Real ARM guest exceptions exit nonzero; a guest loop remains a host timeout."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('ct', Path(__file__).with_name('vcc-ctests.py'))
ct = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ct)

class RuntimeExceptions(unittest.TestCase):
    def test_guest_faults_and_host_timeout(self):
        with tempfile.TemporaryDirectory(prefix='vroom-rt-exceptions-') as td, patch.object(ct, 'OUT', td):
            ct.build_rt()
            probes = {
                'normal': ('return 0;', 0),
                'status': ('return 7;', 7),
                'undefined': ('__asm__ volatile(".inst 0"); return 0;', 125),
                'breakpoint': ('__asm__ volatile("brk #0"); return 0;', 125),
                'data_abort': ('__asm__ volatile("mov x0, #0x10000000000; ldr x0, [x0]"); return 0;', 125),
                'loop': ('for (;;) __asm__ volatile("");', 'timeout'),
            }
            for name, (body, expected) in probes.items():
                with self.subTest(name=name):
                    source = Path(td) / (name + '.c')
                    source.write_text('int main(void) { ' + body + ' }\n')
                    obj, elf = str(source.with_suffix('.o')), str(source.with_suffix('.elf'))
                    rc, _, err = ct.sh([ct.CL] + ct.BASE + ['-c', str(source), '-o', obj])
                    self.assertEqual(rc, 0, err)
                    rc, err = ct.link([obj], elf)
                    self.assertEqual(rc, 0, err)
                    # Shorten only this deliberately infinite probe, never the harness policy.
                    real_sh = ct.sh
                    def short_sh(cmd, timeout=60, **kw):
                        return real_sh(cmd, timeout=1 if name == 'loop' else timeout, **kw)
                    with patch.object(ct, 'sh', side_effect=short_sh):
                        rc, out, err = ct.qemu(elf, diagnostics=True)
                    self.assertEqual(rc, expected, (out, err))
                    if expected == 125:
                        self.assertIn(b'unhandled guest exception', err)
                    elif expected == 'timeout':
                        self.assertEqual(ct.oracle_error(rc, err, 'run', 1)['error'], 'oracle-run')

if __name__ == '__main__':
    unittest.main(warnings="ignore")
