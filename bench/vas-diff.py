#!/usr/bin/env python3
"""vas-diff.py: assembler parity with clang over the kernel's .S TUs.

  bench/vas-diff.py ref [-j N] [PAT]   clang -E and clang -c for every .S TU -> $VAS_REF
  bench/vas-diff.py cmp [-j N] [PAT]   vas on the preprocessed text, compare with clang's .o
  bench/vas-diff.py show PAT           detailed diff for one TU
  --vpp: preprocess with our vpp instead of using clang's -E output

TUs and argv come from work/build-o0/**/.*.o.cmd (as in vcc-diff.py); each
command runs in work/build-o0 (relative .incbin paths).  The comparison is
byte-for-byte; on a mismatch it says which part differs (header, section
contents, symbols, relocations) using llvm-readelf.
"""
import os, re, subprocess, sys, difflib, importlib.util
from multiprocessing import Pool
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hostenv import llvm, BUILD, KTREE, ASREF, LINUX
R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = R if LINUX or os.path.isdir(f'{R}/work/build-o0') else '/Users/johandahlin/dev/vroom'
spec = importlib.util.spec_from_file_location('vd', f'{R}/bench/vcc-diff.py')
vd = importlib.util.module_from_spec(spec); spec.loader.exec_module(vd)
vd.B = B = KTREE if LINUX or ROOT == R else f'{ROOT}/work/build-o0'
REF = ASREF if LINUX or ROOT == R else f'{ROOT}/work/asref'
CL = llvm('clang'); RE = llvm('llvm-readelf')
VAS = os.environ.get('VAS', f'{BUILD}/vasm')
VPP = os.environ.get('VPP', f'{BUILD}/vpp')
USE_VPP = os.environ.get('VAS_VPP') == '1'

def tus(pat=None):
    return [t for t in vd.tus(pat) if t[1][-1].endswith('.S') and '-c' not in t[1]
            and not t[0].endswith('.lds')]

def name(obj): return f'{REF}/{obj.replace("/", "__")}'

def mkref(t):
    obj, a = t
    n = name(obj)
    p = subprocess.run([CL] + a + ['-E', '-o', n[:-2] + '.s'], cwd=B, capture_output=True, text=True)
    q = subprocess.run([CL] + a + ['-c', '-o', n], cwd=B, capture_output=True, text=True)
    return obj, p.returncode or q.returncode, (p.stderr + q.stderr)[-300:]

def readelf(f):
    p = subprocess.run([RE, '-S', '-s', '-r', '-x', '.text', f], capture_output=True, text=True)
    return [l for l in p.stdout.splitlines() if not l.startswith('Key') and 'section headers, starting' not in l]

def detail(ref, out):
    a, b = open(ref, 'rb').read(), open(out, 'rb').read()
    if a == b: return ''
    d = list(difflib.unified_diff(readelf(ref), readelf(out), 'clang', 'vas', n=1, lineterm=''))
    if not d:
        i = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]), min(len(a), len(b)))
        return f'  sizes {len(a)} vs {len(b)}, first difference at byte {i:#x}'
    return '\n'.join('  ' + l for l in d[:int(os.environ.get('LINES', 30))])

def cmp(t):
    obj, a = t
    n = name(obj)
    out = n[:-2] + '.vas.o'
    src = n[:-2] + '.s'
    if USE_VPP:
        src = n[:-2] + '.vpp.s'
        p = subprocess.run([VPP, '-o', src] + a, cwd=B, capture_output=True, text=True)
        if p.returncode: return obj, 2, 'vpp: ' + p.stderr.strip()[-600:]
    p = subprocess.run([VAS, '-o', out, src], cwd=B, capture_output=True, text=True)
    if p.returncode: return obj, 2, p.stderr.strip()[-600:]
    if open(n, 'rb').read() == open(out, 'rb').read(): return obj, 0, ''
    return obj, 1, detail(n, out)

if __name__ == '__main__':
    av = sys.argv[1:]; j = 2
    if '--vpp' in av: av.remove('--vpp'); os.environ['VAS_VPP'] = '1'; USE_VPP = True
    if '-j' in av: k = av.index('-j'); j = int(av[k + 1]); del av[k:k + 2]
    mode = av[0]; pat = av[1] if len(av) > 1 else None
    os.makedirs(REF, exist_ok=True)
    ts = tus(pat)
    if mode == 'show':
        for t in ts: print(cmp(t)[2] or 'identical')
        sys.exit(0)
    with Pool(j) as pool: res = pool.map(mkref if mode == 'ref' else cmp, ts)
    bad = [r for r in res if r[1]]
    for obj, rc, msg in bad[:int(os.environ.get('SHOW', 20))]:
        print(f'--- {obj} ({"error" if rc == 2 else "differs"})\n{msg}')
    print(f'{len(res) - len(bad)}/{len(res)} identical')
    sys.exit(1 if bad else 0)
