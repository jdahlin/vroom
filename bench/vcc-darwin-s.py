#!/usr/bin/env python3
"""vcc-darwin-s.py: vcc -S on Darwin, assembled by Apple's assembler (TODO.md T3).

    bench/vcc-darwin-s.py [-j N] [-v] [PAT]

For each test of the vcc suite and gcc-torture/execute: vcc builds an object
directly (its own Mach-O writer) and also writes -S text, which clang (the
system assembler, `as`) assembles.  Both are linked by clang and run
natively; they must agree on exit status and output.  A test whose direct
build does not compile, link or pass is skipped (vcc-ctests.py covers those):
what is checked here is that the text says the same as the object.
macOS only.
"""
import concurrent.futures, os, re, subprocess, sys, tempfile

B = os.path.dirname(os.path.abspath(__file__))
R = os.path.dirname(B)
T = f'{R}/ccs/vcc/tests'
VPP = os.environ.get('VCC_BIN', f'{R}/build/ccs') + '/vpp'
CLANG = 'clang'
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
        rc, _, err = sh([CLANG, '-c', '-x', 'assembler', f'{wd}/b.s', '-o', f'{wd}/b.o'])
        if rc:
            return 'fail', src, 'as: ' + err.decode(errors='replace')[:600]
        rc, _, err = sh([CLANG, f'{wd}/b.o', '-o', f'{wd}/b', '-lm'])
        if rc:
            return 'fail', src, 'ld: ' + err.decode(errors='replace')[:600]
        rb = sh([f'{wd}/b'], cwd=wd, timeout=10)
        if rb[0] != ra[0] or (ra[1] is not None and rb[1] != ra[1]):
            return 'fail', src, f'run: object {ra[0]}, text {rb[0]}'
    return 'pass', src, ''


def main(av):
    j, verbose, pat = 6, '-v' in av, None
    av = [a for a in av if a != '-v']
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
        for st, src, msg in ex.map(one, srcs):
            n[st] += 1
            if st == 'fail' or (verbose and st != 'pass'):
                print(f'{st.upper()} {os.path.relpath(src, T)}: {msg}')
    print(f'darwin -S: {n["pass"]} pass, {n["fail"]} fail, {n["skip"]} skipped')
    return 1 if n['fail'] else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
