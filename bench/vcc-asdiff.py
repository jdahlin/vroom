#!/usr/bin/env python3
"""vcc's own assembler on the emitter's text, against clang -c of the same text.

  vcc-asdiff.py [PAT] [-j N]

Per kernel C TU: vpp -S -> X.s; clang -c -x assembler X.s and vasm X.s must
give byte-identical objects.  Objects go to $OBJ/asd (default $TMPDIR/vcc-obj).
"""
import os, subprocess, sys
from multiprocessing import Pool
import importlib.util
spec = importlib.util.spec_from_file_location('vo', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'vcc-obj.py'))
vo = importlib.util.module_from_spec(spec); spec.loader.exec_module(vo)
VASM = os.path.join(os.path.dirname(vo.VPP), 'vasm')
D = f'{vo.OBJ}/asd'

def one(t):
    obj, a = t
    base = f'{D}/{obj.replace("/", "__")}'
    p = subprocess.run([vo.VPP, '-S', '-o', base + '.s'] + a, cwd=vo.B, capture_output=True, text=True)
    if p.returncode: return obj, 'vpp: ' + p.stderr[-300:]
    tgt = [x for x in a if x.startswith(('--target=', '-march=', '-mcpu='))]
    q = subprocess.run([vo.CL, '-c', '-x', 'assembler', '-w'] + tgt + ['-o', base + '.ref.o', base + '.s'], capture_output=True, text=True)
    if q.returncode: return obj, 'clang: ' + q.stderr[-300:]
    r = subprocess.run([VASM, '-o', base + '.o', base + '.s'], capture_output=True, text=True)
    if r.returncode: return obj, 'vasm: ' + (r.stdout + r.stderr)[-300:]
    if open(base + '.o', 'rb').read() != open(base + '.ref.o', 'rb').read(): return obj, 'differs'
    return obj, None

if __name__ == '__main__':
    av = sys.argv[1:]; j = 6
    if '-j' in av: k = av.index('-j'); j = int(av[k+1]); del av[k:k+2]
    pat = av[0] if av else None
    os.makedirs(D, exist_ok=True)
    with Pool(j) as pool:
        res = pool.map(one, vo.c_tus(pat), chunksize=2)
    bad = [r for r in res if r[1]]
    kinds = {}
    for o, m in bad: kinds.setdefault(m.split(':')[0].split('\n')[0], []).append(o)
    for k, v in kinds.items(): print(f'{k}: {len(v)}  e.g. {v[:3]}')
    for o, m in bad[:int(os.environ.get('SHOW', 8))]: print(f'--- {o}\n{m}')
    print(f'{len(res) - len(bad)}/{len(res)} identical')
