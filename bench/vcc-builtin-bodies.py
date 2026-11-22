#!/usr/bin/env python3
"""vcc-builtin-bodies.py: builtins with a C body (src/frontend/c/builtin-body.cpp)
leave no out-of-line helper call, at any level, for either target.

  bench/vcc-builtin-bodies.py [--vpp PATH] [-v]

Each program of src/test/vcc/builtin_body_*.c is compiled with `vpp -S` for
aarch64-linux-gnu and x86_64-linux-gnu at -O0 .. -O3.  The calls in the
assembly may name only functions the program defines itself: a call to a
runtime or libgcc helper (__popcountdi2, __ctzdi2, __addc ...) or to the
builtin's own name fails.  The exception is the binary128 comparison of
aarch64's long double (__eqtf2, __netf2, __lttf2 ...: vcc has no inline one
yet, and the iszero it had before compared the same way).  Fast (about 1 s): in ci/premerge.sh."""
import glob, os, re, subprocess, sys

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hostenv import BUILD

TARGETS = {'aarch64-linux-gnu': re.compile(r'^\s*bl\s+(\S+)'),
           'x86_64-linux-gnu': re.compile(r'^\s*call[q]?\s+([^\s*]+)')}


def main():
    vpp = f'{BUILD}/vpp'
    verbose = '-v' in sys.argv
    if '--vpp' in sys.argv:
        vpp = sys.argv[sys.argv.index('--vpp') + 1]
    failed = 0
    count = 0
    for src in sorted(glob.glob(f'{R}/src/test/vcc/builtin_body_*.c')):
        text = open(src).read()
        own = set(re.findall(r'^(?:static\s+)?\w[\w\s\*]*?\b(\w+)\(', text, re.M))
        own |= {m + s for m in ('check_',) for s in ('f', '', 'l')}
        flags = re.findall(r'vcc-flags: ([^*]*)\*/', text)
        for target, call in TARGETS.items():
            for level in ('-O0', '-O1', '-O2', '-O3'):
                cmd = [vpp, f'--target={target}', '-nostdinc', '-S', level, '-o', '-', src]
                cmd[1:1] = flags[0].split() if flags else []
                p = subprocess.run(cmd, capture_output=True, text=True)
                count += 1
                if p.returncode:
                    print(f'FAIL {os.path.basename(src)} {target} {level}: {p.stderr.strip()[:200]}')
                    failed += 1
                    continue
                bad = sorted({m.group(1) for l in p.stdout.splitlines()
                              for m in [call.match(l)] if m and m.group(1).lstrip('_').split('@')[0] not in own and not re.fullmatch(r'__[a-z]{2}tf2', m.group(1))})
                if bad:
                    print(f'FAIL {os.path.basename(src)} {target} {level}: calls {", ".join(bad)}')
                    failed += 1
                elif verbose:
                    print(f'ok   {os.path.basename(src)} {target} {level}')
    print(f'{count - failed}/{count} compiles without a helper call')
    return 1 if failed else 0


sys.exit(main())
