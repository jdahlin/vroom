#!/usr/bin/env python3
"""
Diagnostics: vcc's own expected output, with clang as the reference point
(PLAN-diag.md test layers 1 and 4).

Every ccs/vcc/tests/diag/*.c is a small piece of broken C.  vcc runs
-fsyntax-only on it (no colours, same target) from that directory; a test
passes when
  - layer 4: vcc's stderr is exactly X.c.stderr (vcc's own snapshot: the text
    may be better than clang's), and
  - layer 1: the diagnostics' first lines name the same (file, line, column,
    level, -W flag) as clang's, in order, and the exit status agrees.
A deliberate difference from clang in layer 1 (a better place, fewer
follow-on errors) is declared in the case: /* diag-differs: why */.
A test with /* diag-mode: -c */ is compiled (-c -o /dev/null) instead, for
diagnostics that come from code generation (the error attribute).
The summary also counts the cases whose text is still identical to clang's.

  bench/vcc-diag.py [-v] [-j N] [PATTERN]   -v: the diff of each failure
  bench/vcc-diag.py --bless [PATTERN]       write X.c.stderr from vcc's output
"""
import difflib, os, re, subprocess, sys
from multiprocessing import Pool
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hostenv import llvm, BUILD

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
D = f'{R}/ccs/vcc/tests/diag'
CL = llvm('clang')
VPP = f'{BUILD}/vpp'
FL = ['--target=aarch64-linux-gnu', '-fsyntax-only', '-fno-color-diagnostics']


def run(cmd, name):
    fl = FL
    if re.search(r'/\*\s*diag-mode:\s*-c\s*\*/', open(os.path.join(D, name), errors='replace').read()):
        fl = [f for f in FL if f != '-fsyntax-only'] + ['-c', '-o', '/dev/null']
    p = subprocess.run(cmd + fl + [name], cwd=D, capture_output=True, timeout=30)
    return p.returncode, p.stderr.decode(errors='replace')


HEAD = re.compile(r'^(\S+):(\d+):(\d+): (error|warning|note|fatal error|remark): (.*?)(?: \[(?:-Werror,)?-W([^\],]+)\])?$', re.M)


def heads(out):
    """layer 1: (file, line, col, level, flag) of every diagnostic"""
    return [(m.group(1), m.group(2), m.group(3), m.group(4), m.group(6) or '') for m in HEAD.finditer(out)]


def one(name, bless=False):
    rc_c, out_c = run([CL], name)
    rc_v, out_v = run([VPP], name)
    src = open(os.path.join(D, name), errors='replace').read()
    snap = os.path.join(D, name + '.stderr')
    if bless:
        open(snap, 'w').write(out_v)
    want = open(snap, errors='replace').read() if os.path.exists(snap) else None
    differs = re.search(r'/\*\s*diag-differs:', src) is not None
    l1 = differs or heads(out_c) == heads(out_v)
    ok = want == out_v and l1 and (rc_c == 0) == (rc_v == 0)
    why = []
    if want is None:
        why.append('no .stderr (run --bless)')
    elif want != out_v:
        why.append('text differs from .stderr')
    if not l1:
        why.append('locations/levels/flags differ from clang')
    if (rc_c == 0) != (rc_v == 0):
        why.append(f'exit {rc_v}, clang {rc_c}')
    return name, ok, out_c, out_v, want or '', ', '.join(why), out_c == out_v


def main():
    args = sys.argv[1:]
    verbose = '-v' in args
    j = 6
    if '-j' in args:
        j = int(args[args.index('-j') + 1])
    pat = [a for a in args if not a.startswith('-') and not a.isdigit()]
    names = sorted(f for f in os.listdir(D) if f.endswith('.c') and (not pat or re.search(pat[0], f)))
    bless = '--bless' in args
    with Pool(j) as pool:
        res = pool.starmap(one, [(n, bless) for n in names])
    fails = [r for r in res if not r[1]]
    for name, ok, out_c, out_v, want, why, same in res:
        if ok:
            continue
        print(f'FAIL {name}: {why}')
        if verbose:
            if want != out_v:
                for l in difflib.unified_diff(want.splitlines(), out_v.splitlines(), '.stderr', 'vcc', lineterm='', n=2):
                    print('    ' + l)
            if heads(out_c) != heads(out_v):
                for l in difflib.unified_diff([' '.join(h) for h in heads(out_c)], [' '.join(h) for h in heads(out_v)],
                                              'clang', 'vcc', lineterm='', n=1):
                    print('    ' + l)
    same = sum(r[6] for r in res)
    print(f'{len(res) - len(fails)}/{len(res)} pass: vcc\'s snapshot, clang\'s locations; {same} text-identical to clang')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
