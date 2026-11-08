#!/usr/bin/env python3
"""vcc-sema.py: the vcc front end (parser + sema) over the kernel's C TUs.

  bench/vcc-sema.py run [-j N] [--kernel] [PAT]     vpp -fsyntax-only on every C TU
  bench/vcc-sema.py layout [-j N] [--kernel] [PAT]  vcc's layout/constant claims, checked by clang

`layout` has vpp write _Static_assert lines (sizeof/_Alignof/offsetof of every
file-scope struct, union, enum and typedef, sizeof of every global, every
enumerator's value and type) and appends them to clang's own -E output of the
TU (work/ppref, from vcc-diff.py ref); clang -fsyntax-only on that file then
fails on any claim vcc got wrong.

--bin-dir DIR (or VCC_BIN=DIR) picks the vpp binary, as in vcc-diff.py.
"""
import os, re, subprocess, sys, tempfile, time
from multiprocessing import Pool
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import importlib.util
spec = importlib.util.spec_from_file_location('vd', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'vcc-diff.py'))
vd = importlib.util.module_from_spec(spec); spec.loader.exec_module(vd)
B, CL, VPP = vd.B, vd.CL, vd.VPP
TMP = tempfile.mkdtemp(prefix='vcc-sema-')

def c_tus(pat):
    return [(o, a) for o, a in vd.tus(pat) if a[-1].endswith('.c') and '-E' not in a]

def run(t):
    obj, a = t
    t0 = time.time()
    p = subprocess.run([VPP, '-fsyntax-only'] + a, cwd=B, capture_output=True, text=True)
    return obj, p.returncode, p.stderr[-2000:], time.time() - t0

CHECK_FLAGS = re.compile(r'^(--target=|-std=|-f(un)?signed-char|-fms-extensions|-fms-anonymous-structs|-fexperimental-late-parse-attributes|-fno-strict-aliasing|-fshort-wchar|-m)')

def layout(t):
    obj, a = t
    chk = f'{TMP}/{obj.replace("/", "__")}.chk'
    p = subprocess.run([VPP, '--vcc-layout-checks', '-o', chk] + a, cwd=B, capture_output=True, text=True)
    if p.returncode:
        return obj, p.returncode, 'vcc: ' + p.stderr[-1500:], 0
    src = f'{TMP}/{obj.replace("/", "__")}.i'
    with open(src, 'w') as f:
        f.write(open(vd.refname(obj), errors='replace').read())
        f.write(open(chk).read())
    flags = [x for x in a[:-1] if CHECK_FLAGS.match(x)]
    q = subprocess.run([CL, '-fsyntax-only', '-w', '-Wno-implicit-int', '-x', 'cpp-output', '-ferror-limit=0'] + flags + [src],
                       capture_output=True, text=True)
    n = sum(1 for l in open(chk) if l.startswith('_Static_assert'))
    os.unlink(src); os.unlink(chk)
    # naming an SDK function marked unavailable is an error in itself: not a wrong claim
    errs = [l for l in q.stderr.splitlines() if 'error:' in l and 'is unavailable' not in l]
    return obj, len(errs) and 1, '\n'.join(errs[:8]) + (f'\n... {len(errs)} errors' if len(errs) > 8 else ''), n

if __name__ == '__main__':
    av = [x for x in sys.argv[1:] if x != '--kernel']; j = 6
    if '-j' in av: k = av.index('-j'); j = int(av[k+1]); del av[k:k+2]
    mode = av[0]; pat = av[1] if len(av) > 1 else None
    ts = c_tus(pat)
    if '--kernel' in sys.argv: ts = [t for t in ts if not t[0].startswith('scripts/')]
    with Pool(j) as pool:
        res = pool.map(run if mode == 'run' else layout, ts, chunksize=2)
    bad = [r for r in res if r[1]]
    show = int(os.environ.get('SHOW', 10))
    if os.environ.get('FIRST'):
        # histogram of the first error line per failing TU
        from collections import Counter
        h = Counter()
        for r in bad:
            m = [l for l in r[2].splitlines() if 'error' in l]
            h[re.sub(r'^.*?error: ', '', m[0]) if m else r[2][:100]] += 1
        for k, v in h.most_common(show): print(f'{v:5d}  {k}')
    else:
        for obj, rc, err, _ in bad[:show]:
            print(f'--- {obj} (rc {rc})\n{err}')
    if mode == 'run':
        tt = sorted(r[3] for r in res)
        print(f'{len(res) - len(bad)}/{len(res)} ok; median {tt[len(tt)//2]*1e3:.0f} ms, total {sum(tt):.1f} s')
    else:
        print(f'{len(res) - len(bad)}/{len(res)} ok; {sum(r[3] for r in res)} checks')
    os.rmdir(TMP)
