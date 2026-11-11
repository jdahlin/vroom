#!/usr/bin/env python3
"""Checkout-only tests of Kefir mapping, ABI combinations and oracle failures."""
import importlib.util
import json
from pathlib import Path
from types import SimpleNamespace
import sys
import tempfile
import unittest
from unittest.mock import patch

spec=importlib.util.spec_from_file_location('kefir',Path(__file__).with_name('vcc-kefir.py'))
h=importlib.util.module_from_spec(spec);spec.loader.exec_module(h)


class Kefir(unittest.TestCase):
    def test_profile_options_never_silently_drop_semantics(self):
        flags,adapt,why=h.translate_flags(['--target','host-none','-O1','-std=c23','-ffast-math','-D','X=42','-funsigned-char'])
        self.assertIsNone(why)
        self.assertEqual(flags,['-O1','-std=c23','-ffast-math','-D','X=42','-funsigned-char'])
        self.assertTrue(adapt)
        for flag in ['--print-ast','-Woptimizer-pipeline=op-simplify','-Wcodegen-imprecise-decimal-bitint-conv','-fpreprocessed']:
            self.assertIn(flag,h.translate_flags([flag])[2])

    def test_profile_base_append_substitute_override_and_unknown_environment(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);case=self.fixture(root)
            src=root/case['test'][0];pr=Path(str(src)+'.profile')
            pr.write_text('KEFIR_CFLAGS="${KEFIR_CFLAGS/host-none/host} -O1 -std=c23"')
            r=h.profile(src,root,root/'logs')
            self.assertFalse(r.get('error'))
            self.assertIn('-fPIC',r['flags'])
            self.assertIn('-std=c23',r['flags'])
            self.assertIn('host',r['raw_flags'])
            pr.write_text('KEFIR_CFLAGS="-O2"')
            self.assertEqual(h.profile(src,root,root/'logs')['flags'],['-O2'])
            pr.write_text('export SPECIAL_SEMANTICS=yes')
            self.assertIn('unmapped profile statement',h.profile(src,root,root/'logs')['error'])

    def test_driver_signal_and_silent_build_are_infrastructure_errors(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)
            for i,code in enumerate(["import sys;sys.stderr.write('clang: error: clang frontend command failed due to signal');sys.exit(1)", 'import sys;sys.exit(1)']):
                for stage in ('compile','assemble','link'):
                    r=h.invoke([sys.executable,'-c',code],root,root/(str(i)+stage),1,stage=stage)
                    self.assertTrue(r['error'])
            r=h.invoke([sys.executable,'-c','import sys;sys.exit(1)'],root,root/'runtime',1,stage='run')
            self.assertIsNone(r['error'])

    def test_assembler_failure_points_to_assembler_record(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);case=self.fixture(root)
            args=SimpleNamespace(clang='clang',target='x86_64-linux-gnu',compile_timeout=1,timeout=1)
            def invoke(cmd,cwd,prefix,timeout,*args,**kwargs):
                return {'returncode':1 if str(prefix).endswith('-assemble') else 0,'error':None}
            with patch.object(h,'invoke',side_effect=invoke):
                result=h.one(case,args,root,root,[],root/'host.h')
            self.assertEqual(result['status'],'vcc-fail')
            self.assertTrue(all(v['log'].endswith('-assemble.json') for v in result['compile_failures'].values()))

    def test_arm_guards_cannot_create_empty_passes(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)
            (root/'lib.kefir.c').write_text('#ifdef __x86_64__\nint test(void){return 1;}\n#endif\n')
            self.assertIsNone(h.eligibility(root,'x86_64-linux-gnu'))
            self.assertIn('target-specific',h.eligibility(root,'aarch64-linux-gnu'))
            (root/'lib.kefir.c').write_text('int test(void){return 1;}')
            self.assertIsNone(h.eligibility(root,'aarch64-linux-gnu'))
            (root/'lib.kefir.c').write_text('#ifdef __KEFIRCC_DECIMAL_SUPPORT__\nint test(void){return 1;}\n#endif')
            self.assertIn('decimal capability',h.eligibility(root,'x86_64-linux-gnu'))
            self.assertIn('decimal capability',h.eligibility(root,'aarch64-linux-gnu'))
            (root/'run.profile').write_text('export SPECIAL_BEHAVIOR=1')
            self.assertEqual(h.eligibility(root,'x86_64-linux-gnu'),'unmapped run.profile contract')

    def fixture(self,root):
        pair=root/'source/tests/end2end/test';pair.mkdir(parents=True)
        (pair/'main.host.c').write_text('int main(void){return 0;}')
        (pair/'lib.kefir.c').write_text('int test(void){return 1;}')
        return h.inventory(root,'x86_64-linux-gnu')[0]

    def test_four_combinations_and_live_assertions(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);case=self.fixture(root)
            args=SimpleNamespace(clang='clang',target='x86_64-linux-gnu',compile_timeout=1,timeout=1)
            with patch.object(h,'invoke',return_value={'returncode':0,'error':None}) as run:
                result=h.one(case,args,root,root,[],root/'host.h')
            self.assertEqual(result['status'],'pass')
            self.assertEqual(set(result['combinations']),{'clang-clang','clang-vcc','vcc-clang','vcc-vcc'})
            self.assertEqual(set(result['combinations'].values()),{'pass'})
            commands=[c.args[0] for c in run.call_args_list]
            host=[c for c in commands if any('main.host.c' in str(a) for a in c)]
            self.assertEqual(len(host),2)
            self.assertTrue(all(c.index('-UNDEBUG')>c.index('-DNDEBUG') for c in host))
            self.assertEqual(len([c for c in commands if '-latomic' in c]),4)

    def test_reference_rejection_prevents_candidate_execution(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);case=self.fixture(root)
            args=SimpleNamespace(clang='clang',target='x86_64-linux-gnu',compile_timeout=1,timeout=1)
            with patch.object(h,'invoke',return_value={'returncode':1,'error':None}) as run:
                result=h.one(case,args,root,root,[],root/'host.h')
            self.assertEqual(result['status'],'reference-compile-fail')
            self.assertEqual(run.call_count,1)
            with patch.object(h,'invoke',return_value={'returncode':-11,'error':'signal'}):
                result=h.one(case,args,root,root/'second',[],root/'host.h')
            self.assertEqual(result['status'],'reference-error')

    def test_crash_timeout_missing_tool_are_errors(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)
            commands=[([sys.executable,'-c','import os,signal;os.kill(os.getpid(),signal.SIGTERM)'],1,'signal'),
                      ([sys.executable,'-c','import time;time.sleep(2)'],0.05,'timeout'),
                      (['/no-such-kefir-test-compiler'],1,'tool-error')]
            for i,(cmd,timeout,kind) in enumerate(commands):
                r=h.invoke(cmd,root,root/str(i),timeout)
                self.assertEqual(r['error'],kind)
                self.assertEqual(json.loads((root/(str(i)+'.json')).read_text())['error'],kind)

    def test_oracle_errors_remain_fatal_alongside_passes(self):
        self.assertTrue(h.failed_run({'pass':10,'reference-error':1}))
        self.assertTrue(h.failed_run({'pass':10,'harness-error':1}))
        self.assertFalse(h.failed_run({'pass':10,'reference-compile-fail':1,'unsupported':1}))
        self.assertTrue(h.failed_run({'unsupported':10}))

    def test_snapshot_inputs_are_inventory_only(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);case=self.fixture(root)
            snap=root/'source/tests/end2end/snapshot';snap.mkdir()
            (snap/'lib.kefir.asmgen.c').write_text('int x;')
            (snap/'lib.kefir.asmgen.c.profile').write_text('require_asmgen_decimal_support')
            inv=h.inventory(root,'x86_64-linux-gnu')
            self.assertEqual([c['kind'] for c in inv],['compiler-internal-snapshot','runtime-pair'])
            self.assertIn('require_asmgen_decimal_support',next(iter(inv[0]['raw_profiles'].values())))


if __name__=='__main__':unittest.main()
