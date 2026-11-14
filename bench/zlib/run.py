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

  bench/zlib/run.py --target win64 [build|test|speed|all] [clang vcc vir vccd]

Windows x64 (doc/PLAN-win64.md WIN8): zlib cross-built for x86_64-w64-mingw32
into build/linux/win64-projects/zlib/b-NAME, everything of a build by one
toolchain: `clang` is the reference (clang, lld, compiler-rt, clang's
headers), `vcc` and `vir` are vcc's own (vpp -c with vcc's headers, var,
vcc's driver linking with vlink and libvccrt.a; VCC_VIR=0 or l), `vccd` is
zlib's own ./configure and Makefile with CC="vcc --target=..." (the shim)
and AR=var against a private vccd, which compiles and links in process.
vcc's side runs with a PATH that has no compiler, linker or object tool
(bench/win64proj.py), and every program's link map is checked for foreign
inputs.  `test` runs example, infcover and minigzip round trips (-1 -6 -9,
decompressed by the program and by the host's gzip) under Wine, compares
zbench's sizes and CRCs and minigzip -9's bytes with clang's build; `speed`
records zbench's wall time under Wine, for information.  Skips (exit 0)
without the sysroot, Wine or vcc's tools, or when the source cannot be
fetched.
"""
import glob, hashlib, os, re, shutil, subprocess, sys, time

VER = '1.4'
SHA = '02dd159783677a749c04adbdd02b55b8a2bcc7e054f51b76b549199a916ad584'
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.dirname(HERE))
import hostenv	# work/ of this checkout or of the main one (a worktree has none), or VROOM_WORK
W = os.path.join(hostenv.WORK, 'zlib')
SRC = os.path.join(W, f'zlib-{VER}')
TAR = os.path.join(W, f'zlib-{VER}.tar')
CLANG = os.environ.get('CLANG') or ('/opt/homebrew/opt/llvm/bin/clang' if os.path.exists('/opt/homebrew/opt/llvm/bin/clang') else 'clang')
VPP = os.environ.get('VPP') or os.path.join(ROOT, 'build', 'src', 'vpp')
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


# ---------------- Windows x64 (--target win64) ----------------

WFLAGS = ['-O2', '-DHAVE_UNISTD_H', '-DHAVE_STDARG_H']
WPROGS = (('example', os.path.join(SRC, 'test', 'example.c')),
          ('minigzip', os.path.join(SRC, 'test', 'minigzip.c')),
          ('infcover', os.path.join(SRC, 'test', 'infcover.c')),
          ('zbench', os.path.join(HERE, 'zbench.c')))
wp = None	# bench/win64proj.py, imported by main for --target win64


def wdir(name):
    return os.path.join(wp.OUT, 'zlib', 'b-' + name)


def wbuild(name, vccd):
    """one build: the library and the four programs; (seconds, problems)"""
    b = wdir(name)
    shutil.rmtree(b, ignore_errors=True)
    os.makedirs(b)
    if name == 'vccd':
        return wbuild_configure(b, vccd)
    bad = []
    t = time.time()
    if name == 'clang':
        cc, env = wp.CLANG_CC, wp.clang_env()
    else:
        cc, env = wp.VCC_CC, wp.vcc_env(name, vccd)
        rc, n, clang_hdrs = wp.no_clang_headers(cc + WFLAGS + ['-I' + SRC, os.path.join(SRC, 'test', 'minigzip.c')], b, env)
        if rc or not n or clang_hdrs:
            bad.append(f'headers: {clang_hdrs or "-H failed"}')
    for f in LIB:
        if subprocess.run(cc + WFLAGS + ['-c', os.path.join(SRC, f + '.c'), '-o', f + '.o'], cwd=b, env=env).returncode:
            sys.exit(f'{name}: {f}.c does not compile')
    objs = [f + '.o' for f in LIB]
    if name == 'clang':
        lib = objs
    else:
        subprocess.run([wp.VAR, 'rcs', 'libz.a'] + objs, cwd=b, env=env, check=True)
        lib = ['libz.a']
    for prog, src in WPROGS:
        if name == 'clang':
            cmd = wp.CLANG_LINK + WFLAGS + ['-w', '-I' + SRC, src] + lib + ['-o', prog + '.exe']
        else:
            cmd = wp.VCC_LINK + WFLAGS + ['-v', '-w', '-I' + SRC, src] + lib + ['-o', prog + '.exe', f'-Wl,-Map={prog}.exe.map']
        p = subprocess.run(cmd, cwd=b, env=env, capture_output=True, text=True)
        if p.returncode:
            sys.exit(f'{name}: {prog} does not build:\n{p.stderr}')
        if name != 'clang':
            f = wp.link_line_foreign(p.stderr) + wp.map_foreign(os.path.join(b, prog + '.exe.map'))
            if f:
                bad.append(f'{prog}: foreign input {" ".join(f)}')
    return time.time() - t, bad


def wbuild_configure(b, vccd):
    """zlib's ./configure and make with the vcc shim and var against the private vccd"""
    t = time.time()
    cc = [wp.SHIM, wp.TGT, f'--sysroot={wp.SYSROOT}']
    env = wp.vcc_env('vcc', vccd, CHOST='x86_64-w64-mingw32', CC=' '.join(cc), CFLAGS='-O2', AR=wp.VAR,
                     RANLIB=f'{wp.VAR} s', NM=os.path.join(wp.BUILD, 'vnm'))
    with open(os.path.join(b, 'cfg.log'), 'w') as log:
        if subprocess.run([os.path.join(SRC, 'configure'), '--static'], cwd=b, env=env, stdout=log,
                          stderr=subprocess.STDOUT).returncode:
            sys.exit(f'vccd: configure failed, see {b}/cfg.log')
    with open(os.path.join(b, 'build.log'), 'w') as log:
        # (infcover's rule links with $(CFLAGS), not $(LDFLAGS): its object first, then the link alone)
        if subprocess.run(['make', '-j6', 'libz.a', 'example.exe', 'minigzip.exe', 'infcover.o', 'LDFLAGS=-v -Wl,-Map=$@.map'],
                          cwd=b, env=env, stdout=log, stderr=subprocess.STDOUT).returncode or \
           subprocess.run(['make', 'infcover', 'CFLAGS=-O2 -v -Wl,-Map=infcover.exe.map'],
                          cwd=b, env=env, stdout=log, stderr=subprocess.STDOUT).returncode:
            sys.exit(f'vccd: make failed, see {b}/build.log')
        p = subprocess.run(cc + ['-O2', '-v', '-I' + SRC, '-I.', os.path.join(HERE, 'zbench.c'), 'libz.a', '-o', 'zbench.exe',
                                 '-Wl,-Map=zbench.exe.map'], cwd=b, env=env, capture_output=True, text=True)
        log.write(p.stderr)
        if p.returncode:
            sys.exit(f'vccd: zbench does not build:\n{p.stderr}')
    if os.path.exists(os.path.join(b, 'infcover')):
        os.replace(os.path.join(b, 'infcover'), os.path.join(b, 'infcover.exe'))
    bad = []
    log = open(os.path.join(b, 'build.log'), errors='replace').read()
    links = [l for l in log.splitlines() if l.startswith('vlink ')]
    if len(links) < len(WPROGS):
        bad.append(f'{len(links)} vlink lines in build.log for {len(WPROGS)} programs')
    for l in links:
        bad += [f'link line: {x}' for x in wp.link_line_foreign(l)]
    for prog, _ in WPROGS:
        f = wp.map_foreign(os.path.join(b, prog + '.exe.map'))
        if f:
            bad.append(f'{prog}: foreign input {" ".join(f)}')
    return time.time() - t, bad


def wzbench(name, corpus, reps=1):
    rc, out, err = wp.wine(os.path.join(wdir(name), 'zbench.exe'), [corpus, str(reps)], cwd=wdir(name))
    if rc:
        print(f'{name}: zbench failed ({rc}): {err.decode(errors="replace").strip()[-300:]}')
        return None
    return {l.split()[0]: l.split()[1:] for l in wp.crlf(out).decode().splitlines()}


def wtest(name, corpus):
    b = wdir(name)
    bad = []
    for prog in ('example', 'infcover'):
        rc, out, err = wp.wine(os.path.join(b, prog + '.exe'), cwd=b, timeout=120)
        text = (out + err).decode(errors='replace')
        if rc or (prog == 'example' and 'inflate with dictionary' not in text):
            bad.append(f'{prog}({rc})')
    data = open(corpus, 'rb').read()
    gz9 = None
    for lvl in ('-1', '-6', '-9'):
        rc, gz, _ = wp.wine(os.path.join(b, 'minigzip.exe'), [lvl], cwd=b, input=data)
        rc2, back, _ = wp.wine(os.path.join(b, 'minigzip.exe'), ['-d'], cwd=b, input=gz)
        sysz = subprocess.run(['gzip', '-dc'], input=gz, capture_output=True)
        if rc or rc2 or back != data or sysz.stdout != data:
            bad.append('minigzip' + lvl)
        gz9 = gz
    r = wzbench(name, corpus)
    if r is None:
        bad.append('zbench')
    print(f'{name:6} {"FAIL: " + " ".join(bad) if bad else "ok"} (example, infcover, minigzip -1 -6 -9, zbench)', flush=True)
    return bad, r and {k: v[1:] for k, v in r.items()}, gz9 and gz9[10:]	# (minigzip's bytes past the header's time stamp)


def win64_main(cmd, names, n, corpus):
    why = wp.missing()
    if why:
        return wp.skip('zlib --target win64', why)
    try:
        fetch()
    except (subprocess.CalledProcessError, OSError, SystemExit) as e:
        return wp.skip('zlib --target win64', f'no zlib source ({e})')
    wp.ensure_vccrt()
    names = names or ['clang', 'vcc', 'vir', 'vccd']
    rc = 0
    if cmd in ('build', 'all'):
        with wp.Vccd(wp.vcc_env('vcc')) as v:
            for x in names:
                secs, bad = wbuild(x, v)
                print(f'{x:6} built in {secs:5.1f} s{": " + "; ".join(bad) if bad else ", nothing foreign"}', flush=True)
                rc |= 1 if bad else 0
        if 'vccd' in names:
            print(f'vccd   served: {v.summary}')
            if not re.search(r'[1-9]\d* compiled by vcc.*[1-9]\d* links', v.summary):
                print('vccd: the server compiled or linked nothing'); rc |= 1
    if cmd in ('test', 'all'):
        res = {x: wtest(x, corpus) for x in names}
        rc |= 1 if any(r[0] for r in res.values()) else 0
        if 'clang' in res:
            _, ref, refgz = res['clang']
            for x in names:
                if x == 'clang':
                    continue
                _, got, gz = res[x]
                diff = [k for k in ref or {} if (got or {}).get(k) != ref[k]]
                same = not diff and got and gz == refgz
                print(f'{x}: ' + ('zbench sizes and CRCs and minigzip -9 bytes equal to clang\'s build' if same else
                                  f'output differs from clang\'s build: {" ".join(diff) or "minigzip -9 bytes"}'))
                rc |= 0 if same else 1
    if cmd in ('speed', 'all'):
        best = {}
        for _ in range(n):
            for x in names:
                r = wzbench(x, corpus, 3)
                if r is None:
                    return 1
                for k, v in r.items():
                    best[x, k] = min(best.get((x, k), 1e9), float(v[0]))
        keys = [k for (c, k) in best if c == names[0]]
        print(f'\nzbench {os.path.basename(corpus)} under Wine, wall ms (QueryPerformanceCounter), best of {n}; for information')
        print(f'  {"":12}' + ''.join(f'{c:>16}' for c in names))
        for k in keys:
            print(f'  {k:12}' + ''.join(f'{best[c, k] * 1000:9.1f} {best[c, k] / max(best[names[0], k], 1e-9):5.2f}x' for c in names))
        tot = {c: sum(best[c, k] for k in keys) for c in names}
        print(f'  {"total":12}' + ''.join(f'{tot[c] * 1000:9.1f} {tot[c] / max(tot[names[0]], 1e-9):5.2f}x' for c in names))
    print('zlib --target win64:', 'ok' if not rc else 'FAIL')
    return rc


def main():
    global wp
    a = sys.argv[1:]
    if '--target' in a:
        i = a.index('--target'); tgt = a[i + 1]; del a[i:i + 2]
        if tgt != 'win64':
            sys.exit(f'--target {tgt}: only win64')
        n, corpus = 3, TAR
        if '-n' in a:
            i = a.index('-n'); n = int(a[i + 1]); del a[i:i + 2]
        if '--corpus' in a:
            i = a.index('--corpus'); corpus = os.path.abspath(a[i + 1]); del a[i:i + 2]
        if not a or a[0] not in ('build', 'test', 'speed', 'all'):
            sys.exit(__doc__)
        sys.path.insert(0, os.path.dirname(HERE))
        import win64proj
        wp = win64proj
        return win64_main(a[0], a[1:], n, corpus)
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
