#!/usr/bin/env python3
"""
VCC_FIELD_USES (src/frontend/c/field-uses.c, doc/quality/DESIGN-field-uses.md):
the uses of selected struct fields that vpp reports, as snapshots.

Every src/test/field-uses/NAME.c starts with `/* select: OWNER.FIELD [NEW] */`
lines (the selection file); vpp -fsyntax-only runs on it from that directory
with VCC_FIELD_USES, VCC_EAGER=1, VCC_NODC=1, VPP_NOHC=1, and its stdout must be
NAME.expect (then `exit N` when the status is not 0 and the diagnostics after `stderr:`).  A case can say
`/* flags: ... */` for extra vpp flags.  Also checked: the refusals (not
-fsyntax-only, one of the three variables missing) and that the mode off
prints nothing.

  bench/vcc-field-uses.py [-v] [-j N] [--bin-dir DIR] [PATTERN]
  bench/vcc-field-uses.py --bless [PATTERN]     write NAME.expect from vpp's output
"""
import os, re, subprocess, sys, tempfile
from multiprocessing import Pool
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hostenv import BUILD

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
D = f'{R}/src/test/field-uses'
_BIN = os.environ.get('VCC_BIN', BUILD)
if '--bin-dir' in sys.argv:
    _k = sys.argv.index('--bin-dir'); _BIN = os.path.abspath(sys.argv[_k + 1]); del sys.argv[_k:_k + 2]
    os.environ['VCC_BIN'] = _BIN
VPP = f'{_BIN}/vpp'
FL = ['--target=aarch64-linux-gnu', '-fsyntax-only', '-fno-color-diagnostics', '-w']
ENV = {'VCC_EAGER': '1', 'VCC_NODC': '1', 'VPP_NOHC': '1'}


def run(name, sel, flags=(), env=ENV, fl=FL):
    with tempfile.NamedTemporaryFile('w', suffix='.sel', delete=False) as f:
        f.write(sel or '')
    try:
        e = dict(os.environ)
        for k in ENV:
            e.pop(k, None)
        e.update(env)
        if sel is not None:
            e['VCC_FIELD_USES'] = f.name
        p = subprocess.run([VPP] + fl + list(flags) + [name], cwd=D, capture_output=True, timeout=30, env=e)
    finally:
        os.unlink(f.name)
    return p.returncode, p.stdout.decode(errors='replace'), p.stderr.decode(errors='replace')


def one(name, bless=False):
    src = open(f'{D}/{name}').read()
    sel = ''.join(m.group(1) + '\n' for m in re.finditer(r'/\*\s*select:\s*(.*?)\s*\*/', src))
    m = re.search(r'/\*\s*flags:\s*(.*?)\s*\*/', src)
    flags = m.group(1).split() if m else []
    rc, out, err = run(name, sel, flags)
    got = out + (f'exit {rc}\n' if rc else '') + (f'stderr:\n{err}' if err else '')
    ex = f'{D}/{name[:-2]}.expect'
    if bless:
        open(ex, 'w').write(got)
        return name, []
    if not os.path.exists(ex):
        return name, ['no .expect (run --bless)']
    want = open(ex).read()
    why = []
    if got != want:
        why.append('output differs:\n' + diff(want, got))
    return name, why


def diff(a, b):
    import difflib
    return ''.join(difflib.unified_diff(a.splitlines(True), b.splitlines(True), 'expect', 'got'))


def checks():
    """the refusals, and the mode off"""
    bad = []
    sel = 'A.n\n'
    c = f'{D}/basic.c'
    rc, out, err = run('basic.c', sel, fl=['--target=aarch64-linux-gnu', '-S', '-o', '/dev/null'])
    if rc == 0 or 'VCC_FIELD_USES needs -fsyntax-only' not in err or out:
        bad.append(f'-S not refused: rc={rc} err={err!r}')
    for k in ENV:
        env = {x: y for x, y in ENV.items() if x != k}
        rc, out, err = run('basic.c', sel, env=env)
        if rc == 0 or 'needs VCC_EAGER=1 VCC_NODC=1 VPP_NOHC=1' not in err or out:
            bad.append(f'missing {k} not refused: rc={rc} err={err!r}')
    rc, out, err = run('basic.c', None)
    if rc or out or err:
        bad.append(f'mode off prints something: rc={rc} out={out!r} err={err!r}')
    return bad


def main():
    args = sys.argv[1:]
    j = 4
    if '-j' in args:
        k = args.index('-j'); j = int(args[k + 1]); del args[k:k + 2]
    verbose = '-v' in args
    bless = '--bless' in args
    pat = [a for a in args if not a.startswith('-')]
    names = sorted(f for f in os.listdir(D) if f.endswith('.c') and (not pat or any(p in f for p in pat)))
    with Pool(j) as pool:
        res = pool.starmap(one, [(n, bless) for n in names])
    fails = 0
    for n, why in res:
        if why:
            fails += 1
            print(f'FAIL {n}')
            for w in why:
                print('  ' + (w if verbose else w.splitlines()[0]).replace('\n', '\n  '))
    if not bless and not pat:
        for b in checks():
            fails += 1
            print('FAIL ' + b)
    print(f'field-uses: {len(names) - sum(1 for _, w in res if w)}/{len(names)} cases' + (', blessed' if bless else '') + (f', {fails} failed' if fails else ', all ok'))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
