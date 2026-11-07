#!/usr/bin/env python3
"""Code generator throughput over kernel TUs (VCC_EMIT_STATS from vpp -S).

    vcc-emit-stats.py [PAT] [-j N] [--threads T] [--repeat R]

Per TU, vpp -S runs with VCC_EMIT_STATS=1 (and VCC_EMIT_REPEAT=R: the
generator runs R times over the same AST; the last run's numbers count, so
first-touch costs are left out when R > 1).  Prints totals: instructions,
machine-code bytes (4 per instruction), generation time, printing time, and
MB/s of machine code; plus the slowest TUs.  Do not run while other heavy
jobs are running (check uptime).
"""
import os, re, subprocess, sys
from multiprocessing import Pool
import importlib.util
spec = importlib.util.spec_from_file_location('vd', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'vcc-diff.py'))
vd = importlib.util.module_from_spec(spec); spec.loader.exec_module(vd)
B, VPP = vd.B, vd.VPP
RX = re.compile(r'(\d+) functions, (\d+) objects, (\d+) inlined, (\d+) instructions .*?setup ([\d.]+) ms, emit ([\d.]+) ms '
                r'\((\d+) threads; ([\d.]+) ms printing, ([\d.]+) ms prepass')

def run(t):
    obj, a, env = t
    p = subprocess.run([VPP, '-S', '-o', '/dev/null'] + a, cwd=B, capture_output=True, text=True, env=env)
    ms = RX.findall(p.stderr)
    return obj, p.returncode, ms[-1] if ms else None

if __name__ == '__main__':
    av = sys.argv[1:]; j = 2; thr = 1; rep = 1
    for f in ('-j', '--threads', '--repeat'):
        if f in av:
            k = av.index(f); v = int(av[k+1]); del av[k:k+2]
            if f == '-j': j = v
            elif f == '--threads': thr = v
            else: rep = v
    pat = av[0] if av else None
    env = dict(os.environ, VCC_EMIT_STATS='1', VCC_THREADS=str(thr), VCC_EMIT_REPEAT=str(rep))
    ts = [(o, a, env) for o, a in vd.tus(pat) if a[-1].endswith('.c') and '-E' not in a and not o.startswith('scripts/')]
    with Pool(j) as pool:
        res = pool.map(run, ts, chunksize=4)
    ok = [(o, m) for o, rc, m in res if m]
    ins = sum(int(m[3]) for o, m in ok)
    gen = sum(float(m[5]) for o, m in ok)
    prt = sum(float(m[7]) for o, m in ok)
    setup = sum(float(m[4]) for o, m in ok)
    fns = sum(int(m[0]) for o, m in ok)
    inl = sum(int(m[2]) for o, m in ok)
    print(f'{len(ok)}/{len(res)} TUs, {fns} functions, {inl} inlined, {ins} instructions ({ins * 4 / 1e6:.2f} MB code)')
    print(f'emit {gen:.0f} ms total ({gen / max(len(ok), 1):.2f} ms/TU; printing {prt:.0f} ms), setup {setup:.0f} ms; '
          f'{ins * 4 / 1e6 / (gen / 1e3):.0f} MB/s, {gen * 1e6 / max(ins, 1):.1f} ns/instruction; {thr} threads per TU')
    for o, m in sorted(ok, key=lambda x: -float(x[1][5]))[:5]:
        print(f'  {o}: {m[5]} ms, {m[3]} instructions')
