#!/usr/bin/env python3
"""vcc-darwin-s.py: vcc -S on Darwin, assembled by Apple's assembler (TODO.md T3).

    bench/vcc-darwin-s.py [-j N] [-v] [--clang] [PAT]

--clang: clang's -S of each test assembled by vpp -c and by clang; both
linked by clang must run the same (Apple's assembly dialect as input).

For each test of the vcc suite and gcc-torture/execute: vcc builds an object
directly (its own Mach-O writer) and also writes -S text, which clang (the
system assembler, `as`) assembles.  Both are linked by clang and run
natively; they must agree on exit status and output.  vpp -c of the
-S text must also be byte-identical to the direct object.  A test whose direct
build does not compile, link or pass is skipped (vcc-ctests.py covers those):
what is checked here is that the text says the same as the object.
macOS only.
"""
import concurrent.futures, os, re, subprocess, sys, tempfile

B = os.path.dirname(os.path.abspath(__file__))
R = os.path.dirname(B)
T = f'{R}/src/tests'
VPP = os.environ.get('VCC_BIN', f'{R}/build/src') + '/vpp'
CLANG = 'clang'
# text has no branch relaxation (func.c: every cbz/b.cond far in a huge function)
NOIDENT = {'branch_far.c'}
FL = ['-w', '-O0', '-fno-stack-protector']


def sh(cmd, cwd=None, timeout=20):
    try:
        p = subprocess.run(cmd, cwd=cwd, capture_output=True, timeout=timeout)
        return p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        return -1, b'', b'timeout'


def flags(src):
    s = open(src, 'rb').read().decode('latin-1')
    if re.search(r'vcc-xfail|vcc-noref|dg-skip-if|dg-require', s):
        return None
    return [x for m in re.finditer(r'/\*\s*vcc-flags:(.*?)\*/', s) for x in m.group(1).split()]


def one(src):
    fl = flags(src)
    if fl is None:
        return 'skip', src, ''
    d = os.path.dirname(src)
    with tempfile.TemporaryDirectory() as wd:
        rc, _, err = sh([VPP] + FL + fl + ['-c', src, '-o', f'{wd}/a.o'], cwd=d)
        if rc or sh([CLANG, f'{wd}/a.o', '-o', f'{wd}/a', '-lm'])[0]:
            return 'skip', src, ''
        ra = sh([f'{wd}/a'], cwd=wd, timeout=10)
        if ra[0] != 0:
            return 'skip', src, ''
        if sh([f'{wd}/a'], cwd=wd, timeout=10)[1] != ra[1]:
            ra = (ra[0], None)      # output varies run to run (addresses): status only
        rc, _, err = sh([VPP] + FL + fl + ['-S', src, '-o', f'{wd}/b.s'], cwd=d)
        if rc:
            return 'fail', src, '-S: ' + err.decode(errors='replace')[:400]
        rc, _, err = sh([VPP] + FL + ['-c', f'{wd}/b.s', '-o', f'{wd}/v.o'])
        if rc:
            return 'fail', src, 'vpp -c .s: ' + err.decode(errors='replace')[:400]
        if os.path.basename(src) not in NOIDENT and open(f'{wd}/v.o', 'rb').read() != open(f'{wd}/a.o', 'rb').read():
            return 'fail', src, 'vpp -c of -S differs from the direct object'
        rc, _, err = sh([CLANG, '-c', '-x', 'assembler', f'{wd}/b.s', '-o', f'{wd}/b.o'], timeout=60)
        if rc:
            return 'fail', src, 'as: ' + err.decode(errors='replace')[:600]
        rc, _, err = sh([CLANG, f'{wd}/b.o', '-o', f'{wd}/b', '-lm'])
        if rc:
            return 'fail', src, 'ld: ' + err.decode(errors='replace')[:600]
        rb = sh([f'{wd}/b'], cwd=wd, timeout=10)
        if rb[0] != ra[0] or (ra[1] is not None and rb[1] != ra[1]):
            return 'fail', src, f'run: object {ra[0]}, text {rb[0]}'
    return 'pass', src, ''


def one_clang(src):
    """clang -S, assembled by vpp and by clang: both run the same"""
    fl = flags(src)
    if fl is None:
        return 'skip', src, ''
    d = os.path.dirname(src)
    with tempfile.TemporaryDirectory() as wd:
        if sh([CLANG] + FL + fl + ['-S', src, '-o', f'{wd}/c.s'], cwd=d)[0]:
            return 'skip', src, ''
        if sh([CLANG, '-c', f'{wd}/c.s', '-o', f'{wd}/c.o'])[0] or sh([CLANG, f'{wd}/c.o', '-o', f'{wd}/c', '-lm'])[0]:
            return 'skip', src, ''
        rc_ = sh([f'{wd}/c'], cwd=wd, timeout=10)
        if rc_[0] < 0:
            return 'skip', src, ''
        if sh([f'{wd}/c'], cwd=wd, timeout=10)[1] != rc_[1]:
            rc_ = (rc_[0], None)
        rc, _, err = sh([VPP, '-c', f'{wd}/c.s', '-o', f'{wd}/v.o'])
        if rc:
            return 'fail', src, 'vpp -c: ' + err.decode(errors='replace')[:300]
        rc, _, err = sh([CLANG, f'{wd}/v.o', '-o', f'{wd}/v', '-lm'])
        if rc:
            return 'fail', src, 'ld: ' + err.decode(errors='replace')[:300]
        rv = sh([f'{wd}/v'], cwd=wd, timeout=10)
        if rv[0] != rc_[0] or (rc_[1] is not None and rv[1] != rc_[1]):
            return 'fail', src, f'run: clang as {rc_[0]}, vpp {rv[0]}'
    return 'pass', src, ''


def main(av):
    j, verbose, pat = 6, '-v' in av, None
    fn = one_clang if '--clang' in av else one
    av = [a for a in av if a not in ('-v', '--clang')]
    if '-j' in av:
        i = av.index('-j'); j = int(av[i + 1]); del av[i:i + 2]
    if av:
        pat = av[0]
    srcs = sorted(f'{T}/vcc/{f}' for f in os.listdir(f'{T}/vcc') if f.endswith('.c'))
    srcs += sorted(f'{T}/gcc-torture/execute/{f}' for f in os.listdir(f'{T}/gcc-torture/execute') if f.endswith('.c'))
    if pat:
        srcs = [s for s in srcs if pat in s]
    n = {'pass': 0, 'fail': 0, 'skip': 0}
    with concurrent.futures.ThreadPoolExecutor(j) as ex:
        for st, src, msg in ex.map(fn, srcs):
            n[st] += 1
            if st == 'fail' or (verbose and st != 'pass'):
                print(f'{st.upper()} {os.path.relpath(src, T)}: {msg}')
    print(f'darwin -S{" (clang)" if fn is one_clang else ""}: {n["pass"]} pass, {n["fail"]} fail, {n["skip"]} skipped')
    return 1 if n['fail'] else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
