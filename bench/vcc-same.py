#!/usr/bin/env python3
"""vcc-same.py: are vcc's outputs byte-identical with another build's?

  bench/vcc-same.py --base DIR [-j N] [--pat RE] [--modes M,...] [--vflags F] [--base-vflags F]

For a change that must not alter code generation (a refactor; a new target
that must leave arm64 alone, PLAN-wasm.md): every C file of the test suites
(src/test: gcc-torture, tcc-tests2, vcc) is compiled by DIR/vpp and by
this tree's vpp in each mode, and the outputs (exit status, object or text)
are compared.  Modes (default all): c0 c1 c2 c3 (-c -O0/-O1/-O2/-O3, Linux),
s2 s3 (-S -O2/-O3, Linux), e (-E), d2 (-c -O2, arm64 Darwin), ds (-S -O2,
Darwin), x0 x2 (-S -O0/-O2, x86-64 Linux), xc (-c -O2, x86-64 Linux), xe (-E,
x86-64 Linux).  c0, c1 and x0 are the direct emitter, the -O2 and -O3 modes
VIR (doc/OPTIMIZATION.md).  --vflags 'F' adds flags to this tree's vpp alone,
--base-vflags to DIR's (an option's effect against the base: --vflags
'-fno-rotate-loops' --modes s2 shows what rotation changes).  The flags
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
T = f'{R}/src/test'
VPP = os.path.join(os.environ.get('VCC_BIN') or BUILD, 'vpp')
RES = subprocess.run([llvm('clang'), '-print-resource-dir'], capture_output=True, text=True).stdout.strip() + '/include'
COMMON = ['-nostdinc', '-I', f'{T}/rt/include', '-isystem', RES, '-fno-PIE', '-fno-asynchronous-unwind-tables',
          '-fno-stack-protector', '-w', '-fno-strict-aliasing', '-ffp-contract=off']
LNX, DAR, X86 = '--target=aarch64-linux-gnu', '--target=arm64-apple-darwin', '--target=x86_64-linux-gnu'
MODES = {
    'c0': [LNX, '-c', '-O0'], 'c1': [LNX, '-c', '-O1'], 'c2': [LNX, '-c', '-O2'], 'c3': [LNX, '-c', '-O3'],
    's2': [LNX, '-S', '-O2'], 's3': [LNX, '-S', '-O3'],
    'e': [LNX, '-E', '-O2'], 'd2': [DAR, '-c', '-O2'], 'ds': [DAR, '-S', '-O2'],
    'x0': [X86, '-S', '-O0'], 'x2': [X86, '-S', '-O2'], 'xc': [X86, '-c', '-O2'], 'xe': [X86, '-E', '-O2'],
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
    base, mode, path, bfl, vfl = job
    tag = f'{os.getpid()}-{mode}'
    tmp = os.path.join(os.environ.get('TMPDIR', '/tmp'), f'vcc-same-{tag}')
    a = run(f'{base}/vpp', MODES[mode] + bfl, path, tmp + '-a')
    b = run(VPP, MODES[mode] + vfl, path, tmp + '-b')
    return mode, path, a, b


def main(av):
    if '--base' not in av:
        sys.exit(__doc__)
    base = os.path.abspath(av[av.index('--base') + 1])
    j = int(av[av.index('-j') + 1]) if '-j' in av else 6
    pat = av[av.index('--pat') + 1] if '--pat' in av else None
    modes = av[av.index('--modes') + 1].split(',') if '--modes' in av else list(MODES)
    vfl = av[av.index('--vflags') + 1].split() if '--vflags' in av else []
    bfl = av[av.index('--base-vflags') + 1].split() if '--base-vflags' in av else []
    jobs = [(base, m, f, bfl, vfl) for f in files(pat) for m in modes]
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
