#!/usr/bin/env python3
"""zlib as a test corpus and code-quality benchmark (TODO.md T13/T15).

  bench/zlib/run.py fetch                     # zlib tarball -> work/zlib (once, SHA-256 checked)
  bench/zlib/run.py build [clang vcc vir gcc] # the library, example, minigzip, zbench
  bench/zlib/run.py test  [clang vcc vir]     # self-tests, and the same bytes as clang's build
  bench/zlib/run.py speed [-n 3] [--corpus FILE] [clang vcc vir gcc]
  bench/zlib/run.py all                       # build, test, speed

Each compiler builds zlib's 15 library files with -O2 into work/zlib/b-NAME
(no configure: zconf.h works as shipped); clang compiles the test programs
and links, so only the library differs.  `vcc` is vpp's direct emitter,
`vir` is vpp with VCC_VIR=bl (the -O2 path through VIR).
`test`: zlib's own example and infcover, a minigzip round trip checked by
the system gzip, and zbench's sizes and CRCs equal to the clang build's
(deflate is deterministic, so a miscompiled matcher shows as other bytes).
`speed`: zbench (bench/zlib/zbench.c) on the corpus, CPU time, best of N
process runs interleaved between the compilers; the default corpus is
zlib's own source tar (3.8 MB of C, text and a PDF).

Override with CLANG, GCC, VPP, VROOM_WORK.
"""
import glob, hashlib, os, shutil, subprocess, sys, time

VER = '1.4'
SHA = '02dd159783677a749c04adbdd02b55b8a2bcc7e054f51b76b549199a916ad584'
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
W = os.path.join(os.environ.get('VROOM_WORK') or os.path.join(ROOT, 'work'), 'zlib')
SRC = os.path.join(W, f'zlib-{VER}')
TAR = os.path.join(W, f'zlib-{VER}.tar')
CLANG = os.environ.get('CLANG') or ('/opt/homebrew/opt/llvm/bin/clang' if os.path.exists('/opt/homebrew/opt/llvm/bin/clang') else 'clang')
VPP = os.environ.get('VPP') or os.path.join(ROOT, 'build', 'ccs', 'vpp')
LIB = ['adler32', 'compress', 'crc32', 'deflate', 'gzclose', 'gzlib', 'gzread', 'gzwrite',
       'infback', 'inffast', 'inflate', 'inftrees', 'trees', 'uncompr', 'zutil']
FLAGS = ['-O2', '-DHAVE_UNISTD_H', '-DHAVE_STDARG_H', '-DHAVE_HIDDEN']


def find_gcc():
    if os.environ.get('GCC'):
        return os.environ['GCC']
    c = sorted(glob.glob('/opt/homebrew/bin/gcc-[0-9]*'), key=lambda p: int(p.rsplit('-', 1)[1]))
    return c[-1] if c else shutil.which('gcc') if sys.platform != 'darwin' else None


GCC = find_gcc()
CC = {'clang': (CLANG, {}), 'gcc': (GCC, {}), 'vcc': (VPP, {'VCC_VIR': '0'}), 'vir': (VPP, {'VCC_VIR': 'bl'})}


def sh(cmd, **kw):
    print('+', ' '.join(cmd), flush=True)
    return subprocess.run(cmd, check=True, **kw)


def fetch():
    if not os.path.isdir(SRC):
        os.makedirs(W, exist_ok=True)
        z = TAR + '.gz'
        if not os.path.exists(z):
            sh(['curl', '-sSfL', '-o', z, f'https://zlib.net/zlib-{VER}.tar.gz'])
        if hashlib.sha256(open(z, 'rb').read()).hexdigest() != SHA:
            sys.exit(f'{z}: SHA-256 is not {SHA}')
        sh(['tar', 'xzf', z, '-C', W])
    if not os.path.exists(TAR):
        with open(TAR, 'wb') as f:
            subprocess.run(['gzip', '-dc', TAR + '.gz'], check=True, stdout=f)


def build(name):
    fetch()
    cc, env = CC[name]
    if not cc:
        sys.exit(f'{name}: no compiler (set GCC)')
    b = os.path.join(W, 'b-' + name)
    shutil.rmtree(b, ignore_errors=True)
    os.makedirs(b)
    env = dict(os.environ, **env)
    t = time.time()
    for f in LIB:
        p = subprocess.run([cc] + FLAGS + ['-c', os.path.join(SRC, f + '.c'), '-o', f + '.o'], cwd=b, env=env)
        if p.returncode:
            sys.exit(f'{name}: {f}.c does not compile')
    ct = time.time() - t
    objs = [f + '.o' for f in LIB]
    for prog, src in (('example', os.path.join(SRC, 'test', 'example.c')),
                      ('minigzip', os.path.join(SRC, 'test', 'minigzip.c')),
                      ('infcover', os.path.join(SRC, 'test', 'infcover.c')),
                      ('zbench', os.path.join(HERE, 'zbench.c'))):
        subprocess.run([CLANG] + FLAGS + ['-w', '-I' + SRC, src] + objs + ['-o', prog], cwd=b, check=True)
    text = 0
    for o in objs:
        out = subprocess.run(['size', o], cwd=b, capture_output=True, text=True).stdout.splitlines()
        text += int(out[1].split()[0])
    print(f'{name:6} compile {ct * 1000:6.0f} ms   text {text} bytes')
    return ct, text


def zbench(name, corpus, reps=1):
    p = subprocess.run(['./zbench', corpus, str(reps)], cwd=os.path.join(W, 'b-' + name), capture_output=True, text=True)
    if p.returncode:
        print(f'{name}: zbench failed: {p.stderr.strip()}')
        return None
    return {l.split()[0]: l.split()[1:] for l in p.stdout.splitlines()}


def test(name, corpus):
    b = os.path.join(W, 'b-' + name)
    bad = []
    for prog in ('example', 'infcover'):
        p = subprocess.run(['./' + prog], cwd=b, capture_output=True, text=True)
        if p.returncode or (prog == 'example' and 'inflate with dictionary' not in p.stdout + p.stderr):
            bad.append(prog)
    data = open(corpus, 'rb').read()
    for lvl in ('-1', '-6', '-9'):
        gz = subprocess.run(['./minigzip', lvl], cwd=b, input=data, capture_output=True)
        back = subprocess.run(['./minigzip', '-d'], cwd=b, input=gz.stdout, capture_output=True)
        sysz = subprocess.run(['gzip', '-dc'], input=gz.stdout, capture_output=True)
        if gz.returncode or back.stdout != data or sysz.stdout != data:
            bad.append('minigzip' + lvl)
    r = zbench(name, corpus)
    if r is None:
        bad.append('zbench')
    print(f'{name:6} {"FAIL: " + " ".join(bad) if bad else "ok"}')
    return bad, r and {k: v[1:] for k, v in r.items()}


def main_test(names, corpus):
    res = {x: test(x, corpus) for x in names}
    rc = 1 if any(b for b, _ in res.values()) else 0
    ref = res.get('clang', (None, None))[1]
    for x in names:
        got = res[x][1]
        if ref and got and got != ref:
            diff = [k for k in ref if got.get(k) != ref[k]]
            print(f'{x}: output differs from clang\'s build: {" ".join(diff)}')
            rc = 1
    if not rc:
        print('same sizes and CRCs as clang\'s build' if ref and len(names) > 1 else 'self-tests pass')
    return rc


def speed(names, n, corpus):
    best = {}
    for _ in range(n):
        for c in names:
            r = zbench(c, corpus, 3)
            if r is None:
                return 1
            for k, v in r.items():
                best[c, k] = min(best.get((c, k), 1e9), float(v[0]))
    keys = [k for (c, k) in best if c == names[0]]
    size = os.path.getsize(corpus)
    print(f'\nzbench {os.path.basename(corpus)} ({size / 1e6:.1f} MB), CPU ms, best of {n}; ratio to {names[0]}')
    print(f'  {"":12}' + ''.join(f'{c:>16}' for c in names))
    for k in keys:
        row = f'  {k:12}'
        for c in names:
            row += f'{best[c, k] * 1000:9.2f} {best[c, k] / best[names[0], k]:5.2f}x'
        print(row)
    tot = {c: sum(best[c, k] for k in keys) for c in names}
    print(f'  {"total":12}' + ''.join(f'{tot[c] * 1000:9.2f} {tot[c] / tot[names[0]]:5.2f}x' for c in names))
    return 0


def main():
    a = sys.argv[1:]
    if not a:
        sys.exit(__doc__)
    cmd, a = a[0], a[1:]
    n, corpus = 3, TAR
    if '-n' in a:
        i = a.index('-n'); n = int(a[i + 1]); del a[i:i + 2]
    if '--corpus' in a:
        i = a.index('--corpus'); corpus = os.path.abspath(a[i + 1]); del a[i:i + 2]
    dflt = ['clang', 'vcc', 'vir']
    if cmd == 'fetch':
        fetch()
    elif cmd == 'build':
        for x in a or dflt:
            build(x)
    elif cmd == 'test':
        return main_test(a or dflt, corpus)
    elif cmd == 'speed':
        return speed(a or dflt + (['gcc'] if GCC else []), n, corpus)
    elif cmd == 'all':
        names = dflt + (['gcc'] if GCC else [])
        for x in names:
            build(x)
        return main_test(names, corpus) | speed(names, n, corpus)
    else:
        sys.exit(__doc__)
    return 0


if __name__ == '__main__':
    sys.exit(main())
