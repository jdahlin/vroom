#!/usr/bin/env python3
"""
Diagnostics parity: vcc's error and warning output against clang's.

Every ccs/vcc/tests/diag/*.c is a small piece of broken C.  Both compilers
run -fsyntax-only on it (no colours, same target) from that directory;
the test passes when vcc prints exactly what clang prints on stderr
(locations, messages, source snippets, carets, ranges, fix-its, notes and
the "N errors generated." line) and exits with the same status.
A test with /* diag-mode: -c */ is compiled (-c -o /dev/null) instead, for
diagnostics that come from code generation (the error attribute).
Where vcc says more than clang on purpose (PLAN-diag.md: better where we can),
X.c.expect holds vcc's output: the test passes when vcc prints that, with the
same diagnostic lines (file:line:col: level) as clang and the same exit status.

  bench/vcc-diag.py [-v] [-j N] [PATTERN]   -v: show the diff of each failure
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


def heads(out):
    return [m.group(0) for m in re.finditer(r'^\S+:\d+:\d+: (?:error|warning|note|fatal error):', out, re.M)]


def one(name):
    rc_c, out_c = run([CL], name)
    rc_v, out_v = run([VPP], name)
    exp = os.path.join(D, name + '.expect')
    if os.path.exists(exp):
        want = open(exp, errors='replace').read()
        ok = out_v == want and heads(out_c) == heads(out_v)
        out_c = want if heads(out_c) == heads(out_v) else out_c
    else:
        ok = out_c == out_v
    ok = ok and (rc_c == 0) == (rc_v == 0)
    return name, ok, out_c, out_v, rc_c, rc_v


def main():
    args = sys.argv[1:]
    verbose = '-v' in args
    j = 6
    if '-j' in args:
        j = int(args[args.index('-j') + 1])
    pat = [a for a in args if not a.startswith('-') and not a.isdigit()]
    names = sorted(f for f in os.listdir(D) if f.endswith('.c') and (not pat or re.search(pat[0], f)))
    with Pool(j) as pool:
        res = pool.map(one, names)
    fails = [r for r in res if not r[1]]
    for name, ok, out_c, out_v, rc_c, rc_v in res:
        if ok:
            continue
        print(f'FAIL {name}' + (f' (exit {rc_v}, clang {rc_c})' if (rc_c == 0) != (rc_v == 0) else ''))
        if verbose:
            for l in difflib.unified_diff(out_c.splitlines(), out_v.splitlines(), 'clang', 'vcc', lineterm='', n=2):
                print('    ' + l)
    nexp = sum(os.path.exists(os.path.join(D, r[0] + '.expect')) for r in res)
    print(f'{len(res) - len(fails)}/{len(res)} pass: identical to clang' + (f', or ({nexp}) to vcc\'s own .expect at clang\'s locations' if nexp else ''))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
