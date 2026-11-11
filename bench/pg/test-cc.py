#!/usr/bin/env python3
"""Check native wrapper C dispatch and complete failure logging without fallback."""
import json,os,pathlib,subprocess,tempfile,unittest
class Wrapper(unittest.TestCase):
    def test_c_stdin_and_failures(self):
        with tempfile.TemporaryDirectory(prefix='pg-cc-test-') as td:
            root=pathlib.Path(td);vpp=root/'vpp';clang=root/'clang';log=root/'fail.jsonl'
            vpp.write_text('#!/usr/bin/env python3\nimport sys\nsys.stderr.write("vpp-only:"+"x"*10000)\nsys.exit(7)\n');vpp.chmod(0o755)
            clang.write_text('#!/bin/sh\necho unexpected-clang >&2\nexit 0\n');clang.chmod(0o755)
            env={**os.environ,'VPP':str(vpp),'VCC_PG_CLANG':str(clang),'VCC_PG_LOG':str(log),'VCC_PG_FALLBACK':'0'}
            wrapper=str(pathlib.Path(__file__).with_name('cc'))
            for args in [['-c','probe.c','-o','x.o'],['-c','-x','c','-','-o','x.o'],['-S','probe.c','-o','x.s']]:
                r=subprocess.run([wrapper,*args],input='int x;',text=True,capture_output=True,env=env)
                self.assertEqual(r.returncode,7)
                self.assertNotIn('unexpected-clang',r.stderr)
                self.assertEqual(len(r.stderr),10009)
            rows=[json.loads(line) for line in log.read_text().splitlines()]
            self.assertEqual(len(rows),3)
            self.assertTrue(all(len(row['err'])==10009 and not row['fallback'] for row in rows))
if __name__=='__main__':unittest.main()
