#!/usr/bin/env python3
"""vcc-diff.py: preprocessor parity with clang over the kernel TUs.

  bench/vcc-diff.py ref [-j N]          clang -E -P for every C TU -> work/ppref/
  bench/vcc-diff.py cmp [-j N] [PAT]    vpp --cmp against the references
  bench/vcc-diff.py args TU             the clang argv for one TU (for vpp)

TUs come from work/build-o0/**/.*.o.cmd (savedcmd_X := ...); each command
runs in work/build-o0.  The comparison is a token stream (kind, spelling),
done by vpp itself (it lexes the reference with vcc's lexer).
"""
import glob, hashlib, os, re, shlex, subprocess, sys
from multiprocessing import Pool
R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
B = f'{R}/work/build-o0'; REF = f'{R}/work/ppref'
CL = '/opt/homebrew/opt/llvm/bin/clang'; VPP = f'{R}/build/ccs/vpp'

def tu_args(cmd):
    line = open(cmd, errors='replace').readline()
    m = re.match(r'savedcmd_(\S+) := (.*)', line)
    if not m: return None
    a = shlex.split(m.group(2))
    if not a or not a[-1].endswith('.c') or '-c' not in a: return None
    a = [x for x in a[1:] if not x.startswith('-Wp,-MMD')]
    i = a.index('-c'); a = a[:i] + a[i+3:]
    return m.group(1), a

def tus(pat=None):
    out = []
    for c in sorted(glob.glob(f'{B}/**/.*.o.cmd', recursive=True)):
        t = tu_args(c)
        if t and (not pat or re.search(pat, t[0])): out.append(t)
    return out

def refname(obj): return f'{REF}/{obj.replace("/", "__")}.i'

def mkref(t):
    obj, a = t
    p = subprocess.run([CL] + a + ['-E', '-P', '-o', refname(obj)], cwd=B, capture_output=True, text=True)
    return obj, p.returncode, p.stderr[-300:]

def cmp(t):
    obj, a = t
    p = subprocess.run([VPP, '--cmp', refname(obj)] + a, cwd=B, capture_output=True, text=True)
    return obj, p.returncode, (p.stdout + p.stderr)[-1500:]

if __name__ == '__main__':
    av = [a for a in sys.argv[1:] if a != '--kernel']; j = 6
    if '-j' in av: k = av.index('-j'); j = int(av[k+1]); del av[k:k+2]
    mode = av[0]; pat = av[1] if len(av) > 1 else None
    if mode == 'args':
        for obj, a in tus(pat): print(shlex.join(a)); break
        sys.exit(0)
    ts = tus(pat)
    if '--kernel' in sys.argv: ts = [t for t in ts if not t[0].startswith('scripts/')]
    with Pool(j) as pool:
        res = pool.map(mkref if mode == 'ref' else cmp, ts, chunksize=4)
    bad = [r for r in res if r[1]]
    for obj, rc, err in bad[:int(os.environ.get('SHOW', 20))]:
        print(f'--- {obj} (rc {rc})\n{err}')
    print(f'{len(res) - len(bad)}/{len(res)} ok')
