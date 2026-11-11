#!/usr/bin/env python3
"""Accounting regressions: failed compiles may leave objects; batch status is authoritative."""
import json, pathlib, subprocess, sys, tempfile, unittest

FAKE = r'''#!/usr/bin/env python3
import pathlib,sys
args=sys.argv[1:]
def job(a):
 o=pathlib.Path(a[a.index('-o')+1]);o.write_bytes(b'object even when compilation fails')
 bad=any(x.endswith('bad.c') for x in a)
 if bad: print('deliberate compiler error',file=sys.stderr)
 return int(bad)
if '--batch' in args:
 rows=[x.split('\x1f')[1:] for x in pathlib.Path(args[args.index('--batch')+1]).read_text().splitlines()]
 statuses=[job(x) for x in rows]
 pathlib.Path(args[args.index('--stats')+1]).write_text(''.join(str(x)+'\t0\t0\t0\t0\t0\t0\ttest.c\n' for x in statuses))
 sys.exit(any(statuses))
sys.exit(job(args))
'''
class Accounting(unittest.TestCase):
    def check(self, mode, bad):
        with tempfile.TemporaryDirectory(prefix='pg-speed-test-') as tmp:
            root=pathlib.Path(tmp); cc=root/'compiler';cc.write_text(FAKE);cc.chmod(0o755)
            jobs=[]
            for name in ['good.c','bad.c' if bad else 'good2.c']:
                (root/name).write_text('int x;\n')
                jobs.append(dict(file=name,directory=str(root),output='same.o',arguments=[str(cc),'-O2','-g','-c',name,'-o','same.o']))
            (root/'compile_commands.json').write_text(json.dumps(jobs))
            out=root/'results'
            r=subprocess.run([sys.executable,str(pathlib.Path(__file__).with_name('speed.py')),'--build-dir',str(root),'--output',str(out),'--clang',str(cc),'--vpp',str(cc),'-r','2','-j','2']+([mode] if mode else []),capture_output=True,text=True)
            self.assertEqual(r.returncode,int(bad),r.stdout+r.stderr)
            records=json.loads((out/'results.json').read_text())
            self.assertEqual(len(records),2 if mode else 12)
            for row in records:
                self.assertEqual(row['attempted'],2)
                self.assertEqual(row['successful'],1 if bad else 2)
                self.assertEqual(row['complete'],not bad)
                self.assertTrue(all(j['object_present'] for j in row['jobs']))
            if bad:
                self.assertNotIn('successful jobs/s',r.stdout)
                self.assertNotIn('vs matching clang',r.stdout)
                self.assertIn('deliberate compiler error',''.join(p.read_text() for p in out.rglob('*.stderr')))
            else: self.assertIn('successful jobs/s',r.stdout)
            self.assertEqual(len(json.loads((out/'corpus.json').read_text())['jobs']),2)
    def test_process_failure_with_output(self): self.check('vpp',True)
    def test_batch_failure_with_output(self): self.check('vpp-batch',True)
    def test_successful_repeat(self): self.check('vpp-O0',False)
    def test_default_modes(self): self.check(None,False)
if __name__=='__main__':unittest.main()
