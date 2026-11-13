#!/usr/bin/env python3
"""vas-obj.py: whole-object parity of vasm with clang -c on small .s files.

  bench/vas-obj.py [DIR...]     default: src/tests/vas-obj

Each file is assembled by clang (--target=aarch64-linux-gnu; x86-*.s files
--target=x86_64-linux-gnu, vasm --target=x86_64) and by vasm;
the objects must be byte-identical.  On a mismatch llvm-readelf shows the
difference.  A file clang rejects must fail in vasm too, with clang's first
errors at the same file:lines.  Directives, symbol tables and relocations, <1 s.
"""
import glob, os, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hostenv import llvm, BUILD
R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VAS = os.environ.get('VAS', f'{BUILD}/vasm')
CL = llvm('clang'); RE = llvm('llvm-readelf')

def main():
    dirs = sys.argv[1:] or [f'{R}/src/tests/vas-obj']
    files = sorted(f for d in dirs for f in glob.glob(f'{d}/*.s'))
    ok = bad = 0
    with tempfile.TemporaryDirectory() as t:
        for f in files:
            c, v = f'{t}/c.o', f'{t}/v.o'
            x86 = os.path.basename(f).startswith('x86-')
            p = subprocess.run([CL, '--target=x86_64-linux-gnu' if x86 else '--target=aarch64-linux-gnu', '-c', f, '-o', c],
                               capture_output=True, text=True)
            q = subprocess.run([VAS] + (['--target=x86_64'] if x86 else []) + [f, '-o', v], capture_output=True, text=True)
            if p.returncode:    # an error test: the same file:line of every error
                import re
                ce = re.findall(r'^(\S+?):(\d+):\d+: (?:error|note)', p.stderr, re.M)
                ve = re.findall(r'^(\S+?):(\d+): (?:error|note)', q.stderr, re.M)
                if q.returncode and ce and ce == ve: ok += 1
                else:
                    bad += 1
                    print('DIFF', os.path.basename(f), 'clang:', ce, 'vasm:', ve or 'no error')
                continue
            if q.returncode or open(c, 'rb').read() != open(v, 'rb').read():
                bad += 1
                print('DIFF', os.path.basename(f), q.stderr.strip()[:300])
                if not p.returncode and not q.returncode:
                    a = subprocess.run([RE, '-a', c], capture_output=True, text=True).stdout.splitlines()
                    b = subprocess.run([RE, '-a', v], capture_output=True, text=True).stdout.splitlines()
                    import difflib
                    for l in list(difflib.unified_diff(a, b, 'clang', 'vasm', lineterm='', n=0))[:30]: print('  ' + l)
            else: ok += 1
    print(f'vas-obj: {ok}/{ok + bad} identical')
    sys.exit(1 if bad else 0)

if __name__ == '__main__': main()
