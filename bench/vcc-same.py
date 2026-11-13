#!/usr/bin/env python3
"""vcc-same.py: are vcc's outputs byte-identical with another build's?

  bench/vcc-same.py --base DIR [-j N] [--pat RE] [--modes M,...]

For a change that must not alter code generation (a refactor; a new target
that must leave arm64 alone, PLAN-wasm.md): every C file of the test suites
(src/tests: gcc-torture, tcc-tests2, vcc) is compiled by DIR/vpp and by
this tree's vpp in each mode, and the outputs (exit status, object or text)
are compared.  Modes (default all): c0 c1 c2 (-c -O0/-O1/-O2, Linux), s2 (-S
-O2, Linux), e (-E), d2 (-c -O2, arm64 Darwin), ds (-S -O2, Darwin).  The flags
are vcc-ctests.py's (its freestanding runtime headers), so no system headers
are involved.  The kernel side is `bench/vbench.py check --base DIR`.

DIR is a bin dir such as `bench/vbench.py mkbase REV` prints.  Exit 1 when
anything differs.
"""
import hashlib, os, re, subprocess, sys
from multiprocessing import Pool
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hostenv import llvm, BUILD

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
T = f'{R}/src/tests'
VPP = os.path.join(os.environ.get('VCC_BIN') or BUILD, 'vpp')
RES = subprocess.run([llvm('clang'), '-print-resource-dir'], capture_output=True, text=True).stdout.strip() + '/include'
COMMON = ['-nostdinc', '-I', f'{T}/rt/include', '-isystem', RES, '-fno-PIE', '-fno-asynchronous-unwind-tables',
          '-fno-stack-protector', '-w', '-fno-strict-aliasing', '-ffp-contract=off']
LNX, DAR = '--target=aarch64-linux-gnu', '--target=arm64-apple-darwin'
MODES = {
    'c0': [LNX, '-c', '-O0'], 'c1': [LNX, '-c', '-O1'], 'c2': [LNX, '-c', '-O2'], 's2': [LNX, '-S', '-O2'],
    'e': [LNX, '-E', '-O2'], 'd2': [DAR, '-c', '-O2'], 'ds': [DAR, '-S', '-O2'],
}


def files(pat):
    out = []
    for sub in ('gcc-torture/execute', 'tcc-tests2', 'vcc'):
        d = f'{T}/{sub}'
        out += [f'{d}/{f}' for f in sorted(os.listdir(d)) if f.endswith('.c')]
    return [f for f in out if not pat or re.search(pat, f)]


def run(vpp, args, path, tmp):
    try:
        p = subprocess.run([vpp] + args + COMMON + [path, '-o', tmp], cwd=os.path.dirname(path),
                           capture_output=True, timeout=60)
        rc = p.returncode
    except subprocess.TimeoutExpired:
        return 'timeout'
    try:
        data = open(tmp, 'rb').read()
        os.unlink(tmp)
    except OSError:
        data = b''
    return f'{rc}:{hashlib.sha1(data).hexdigest()}'


def one(job):
    base, mode, path = job
    tag = f'{os.getpid()}-{mode}'
    tmp = os.path.join(os.environ.get('TMPDIR', '/tmp'), f'vcc-same-{tag}')
    a = run(f'{base}/vpp', MODES[mode], path, tmp + '-a')
    b = run(VPP, MODES[mode], path, tmp + '-b')
    return mode, path, a, b


def main(av):
    if '--base' not in av:
        sys.exit(__doc__)
    base = os.path.abspath(av[av.index('--base') + 1])
    j = int(av[av.index('-j') + 1]) if '-j' in av else 6
    pat = av[av.index('--pat') + 1] if '--pat' in av else None
    modes = av[av.index('--modes') + 1].split(',') if '--modes' in av else list(MODES)
    jobs = [(base, m, f) for f in files(pat) for m in modes]
    bad = 0
    with Pool(j) as pool:
        for mode, path, a, b in pool.imap_unordered(one, jobs, chunksize=8):
            if a != b:
                bad += 1
                if bad <= 30:
                    print(f'DIFF {mode} {os.path.relpath(path, R)}  base {a[:12]} new {b[:12]}')
    print(f'{len(jobs) - bad}/{len(jobs)} outputs identical ({len(jobs) // max(1, len(modes))} files x {",".join(modes)})')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
