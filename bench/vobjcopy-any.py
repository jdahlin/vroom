#!/usr/bin/env python3
"""vnm/vobjcopy against llvm-nm/llvm-objcopy beyond the build's own uses
(bench/vobjcopy-cmp.sh covers those): N random kernel .o files of a build
tree, with options the build does not run.  Each line counts outcomes:
same (status and bytes), refused by us, both fail, or DIFFERENT.

  bench/vobjcopy-any.py BUILD-DIR [N=200]
"""
import os, sys, subprocess, tempfile, random, collections
from multiprocessing.pool import ThreadPool
R = os.path.dirname(os.path.dirname(os.path.abspath(__file__))); B = f"{R}/build/ccs"; L = "/opt/homebrew/opt/llvm/bin"
W = sys.argv[1]
objs = sorted(os.path.join(d, f) for d, _, fs in os.walk(W) for f in fs if f.endswith('.o') and '/scripts/' not in d + '/' and not f.endswith('.mod.o'))
random.seed(1); objs = random.sample(objs, min(int(sys.argv[2]) if len(sys.argv) > 2 else 200, len(objs)))
T = tempfile.mkdtemp()
NM = [[], ['-p', '-S'], ['-n'], ['--help']]
OC = [['-j', '.text'], ['-j', '.text', '-j', '.rela.text'], ['-j', '.data', '-j', '.rodata*', '-w'], ['-j', '.init.text', '-S'], ['-S'], ['-O', 'binary']]
res = collections.Counter(); ex = {}
def one(o):
    out = []
    for a in NM:
        r = subprocess.run([f"{L}/llvm-nm"] + a + [o], capture_output=True)
        m = subprocess.run([f"{B}/vnm"] + a + [o], capture_output=True)
        k = 'nm ' + ' '.join(a)
        out.append((k, 'same' if (r.returncode, r.stdout) == (m.returncode, m.stdout) else
                    ('vnm refuses' if m.returncode and not r.returncode else 'DIFFERENT'), o))
    for i, a in enumerate(OC):
        b = os.path.join(T, f"{objs.index(o)}-{os.path.basename(o)}{i}")  # (basenames repeat across directories)
        r = subprocess.run([f"{L}/llvm-objcopy"] + a + [o, b + '.r'], capture_output=True)
        m = subprocess.run([f"{B}/vobjcopy"] + a + [o, b + '.m'], capture_output=True)
        k = 'objcopy ' + ' '.join(a)
        if r.returncode or m.returncode:
            v = 'both fail' if r.returncode and m.returncode else ('vobjcopy refuses' if m.returncode else 'llvm fails, we succeed')
        else:
            v = 'same' if open(b + '.r', 'rb').read() == open(b + '.m', 'rb').read() else 'DIFFERENT'
        out.append((k, v, o))
        for s in ('.r', '.m'):
            if os.path.exists(b + s): os.unlink(b + s)
    return out
for rs in ThreadPool(6).map(one, objs):
    for k, v, o in rs:
        res[(k, v)] += 1; ex.setdefault((k, v), o)
bad = 0
for (k, v), n in sorted(res.items()):
    bad += v == 'DIFFERENT'
    print(f"{k:34s} {v:22s} {n:4d}" + (f"   e.g. {ex[(k,v)]}" if v != 'same' else ''))
sys.exit(1 if bad else 0)
