#!/usr/bin/env python3
"""TSVC_2 (work/testsuites/TSVC_2, `bench/fetch-testsuites.sh TSVC_2`) built natively on
macOS: tsvc.c by vpp -O2 through the direct emitter and through VIR (VCC_VIR=bl), the
helpers and the reference by clang.  Every loop's checksum must equal clang -O3's.

    bench/vcc-tsvc.py [--iterations N] [--bin-dir DIR] [-v] [--why]

`iterations` (100000 upstream) is lowered to N (default 256, the least at which every outer loop runs; s176's is run once): seconds, and the
checksums still cover every loop.  -v lists each loop's times, --why the vectorizer's reasons
for the loops it left (emit/vir/vec.inc).  RESEARCH-vectors.md."""
import os, re, shutil, subprocess, sys

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
arg = lambda f, d: sys.argv[sys.argv.index(f) + 1] if f in sys.argv else d
bind = os.path.abspath(arg('--bin-dir', os.environ.get('VCC_BIN', os.path.join(root, 'build/ccs'))))
iters = int(arg('--iterations', '256'))
src = os.path.join(root, 'work/testsuites/TSVC_2/src')
out = os.path.join(root, 'work/build-tsvc')
clang = os.environ.get('VCC_TSVC_CLANG', '/opt/homebrew/opt/llvm/bin/clang')
if not os.path.exists(clang):
    clang = shutil.which('clang')
if not os.path.isdir(src):
    sys.exit('no work/testsuites/TSVC_2: run bench/fetch-testsuites.sh TSVC_2')

def run(cmd, env=None, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, env=env, **kw)
    if r.returncode:
        sys.exit(f"failed: {' '.join(cmd)}\n{r.stderr[-2000:]}")
    return r

os.makedirs(out, exist_ok=True)
for f in os.listdir(src):
    if f.endswith(('.c', '.h')):
        s = open(os.path.join(src, f)).read()
        if f == 'common.h':
            s, n = re.subn(r'#define iterations \d+', f'#define iterations {iters}', s)
            if n != 1:
                sys.exit('common.h: no `#define iterations`')
        if f == 'tsvc.c' and iters < 32000:  # s176's outer loop, 4*(iterations/LEN_1D), would not run: once
            s, n = re.subn(r'4\*\(iterations/LEN_1D\)', '1', s)
            if n != 1:
                sys.exit('tsvc.c: s176 has changed')
        open(os.path.join(out, f), 'w').write(s)
sdk = run(['xcrun', '--show-sdk-path']).stdout.strip() if sys.platform == 'darwin' else None
for f in ('common', 'dummy'):
    run([clang, '-O2', '-c', f + '.c', '-o', f + '.o'], cwd=out)

cyc = {}

def build(name, cmd, env=None):
    run(cmd + ['-c', 'tsvc.c', '-o', f'tsvc_{name}.o'], cwd=out, env=env)
    run([clang, f'tsvc_{name}.o', 'common.o', 'dummy.o', '-lm', '-o', f'tsvc_{name}'], cwd=out)
    rows = {}
    exe = os.path.join(out, f'tsvc_{name}')
    r = run((['/usr/bin/time', '-l'] if sys.platform == 'darwin' else []) + [exe], cwd=out)
    for line in r.stdout.split('\n')[1:]:
        p = line.split()
        if len(p) == 3:
            rows[p[0]] = (float(p[1]), p[2])
    m = re.search(r'(\d+)\s+cycles elapsed', r.stderr)  # (the machine is shared: cycles say more than seconds)
    cyc[name] = int(m.group(1)) / 1e9 if m else 0
    return rows

vpp = [os.path.join(bind, 'vpp'), '-O2'] + (['-isysroot', sdk] if sdk else [])
env0 = {k: v for k, v in os.environ.items() if k != 'VCC_VIR'}
ref = build('clang', [clang, '-O3'])
runs = [('vcc', build('vcc', vpp, env0)), ('vir', build('vir', vpp, dict(env0, VCC_VIR=os.environ.get('VCC_VIR', 'bl'))))]
# the vectorizer's own account (VCC_VIR_VEC=r): one line per innermost loop it looked at
rep = subprocess.run(vpp + ['-c', 'tsvc.c', '-o', os.devnull], cwd=out, capture_output=True, text=True,
                     env=dict(env0, VCC_VIR=os.environ.get('VCC_VIR', 'bl'), VCC_VIR_VEC='r')).stderr
loops = [l for l in rep.split('\n') if l.startswith('vec: ')]
vec = [l for l in loops if l.endswith(': vectorized')]
bad = 0
for name, rows in runs:
    wrong = [k for k in ref if rows.get(k, (0, None))[1] != ref[k][1]]
    bad += len(wrong)
    for k in wrong:
        print(f'FAIL {name} {k}: checksum {rows.get(k, (0, "missing"))[1]}, clang {ref[k][1]}')
    print(f'{name}: {len(ref) - len(wrong)}/{len(ref)} checksums, {sum(t for t, _ in rows.values()):.2f} s'
          f' (clang -O3 {sum(t for t, _ in ref.values()):.2f} s)'
          + (f', {cyc[name]:.2f} G cycles (clang {cyc["clang"]:.2f})' if cyc.get('clang') else ''))
print(f'vectorized: {len(vec)} of {len(loops)} loops, in {len({l.split()[1] for l in vec})} of {len(ref)} functions')
if '--why' in sys.argv:
    why = {}
    for l in loops:
        if ': no: ' in l:
            why[l.split(': no: ')[1]] = why.get(l.split(': no: ')[1], 0) + 1
    for k, v in sorted(why.items(), key=lambda x: -x[1]):
        print(f'{v:4} {k}')
if '-v' in sys.argv:
    print(f"{'loop':8} {'clang':>7} " + ' '.join(f'{n:>7}' for n, _ in runs))
    for k in ref:
        print(f'{k:8} {ref[k][0]:7.3f} ' + ' '.join(f'{r.get(k, (0,))[0]:7.3f}' for _, r in runs))
sys.exit(1 if bad else 0)
