#!/usr/bin/env python3
"""SQLite as a test corpus (TODO.md T13/T15): build, TCL tests, speedtest1.

  bench/sqlite/run.py fetch                    # sqlite-src zip -> work/sqlite (once)
  bench/sqlite/run.py build  [vcc clang gcc]   # configure + make testfixture sqlite3
  bench/sqlite/run.py test   [vcc clang]       # testrunner.tcl --jobs 6, then compare
  bench/sqlite/run.py speed  [-n 3] [--size 100] [vcc clang gcc]
  bench/sqlite/run.py all                      # build, test, speed for vcc and clang (+ gcc if found)

Builds go to work/sqlite/b-NAME (configure with CFLAGS=-O2).  vcc's build uses
bench/pg/cc as CC (vpp compiles C, clang links); its failures are logged to
b-vcc/fail.jsonl (configure probes that clang rejects too are expected).
`test` passes when vcc fails no test that clang passes; known environmental
failures (interrupt2.test: timing) show up on both sides.
`speed`: speedtest1 from the amalgamation, -O2 -DSQLITE_THREADSAFE=0, --memdb,
best of N, interleaved; the verification hash must agree.  It also times
compiling sqlite3.c with each compiler.

Needs Tcl >= 8.6 (brew install tcl-tk; Apple's tclsh is 8.5) and, for gcc,
a real GCC (brew install gcc; /usr/bin/gcc is clang).  Override with
SQLITE_TCL (prefix), GCC, CLANG, VPP.
"""
import glob, os, re, shutil, sqlite3, subprocess, sys, time

VER, YEAR = '3500400', '2025'
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
W = os.path.join(os.environ.get('VROOM_WORK') or os.path.join(ROOT, 'work'), 'sqlite')
SRC = os.path.join(W, f'sqlite-src-{VER}')
TCL = os.environ.get('SQLITE_TCL') or '/opt/homebrew/opt/tcl-tk'
CLANG = os.environ.get('CLANG') or ('/opt/homebrew/opt/llvm/bin/clang' if os.path.exists('/opt/homebrew/opt/llvm/bin/clang') else 'clang')
VPP = os.environ.get('VPP') or os.path.join(ROOT, 'build', 'src', 'vpp')


def find_gcc():
    if os.environ.get('GCC'):
        return os.environ['GCC']
    c = sorted(glob.glob('/opt/homebrew/bin/gcc-[0-9]*'), key=lambda p: int(p.rsplit('-', 1)[1]))
    return c[-1] if c else shutil.which('gcc') if sys.platform != 'darwin' else None


GCC = find_gcc()
CC = {'clang': CLANG, 'gcc': GCC, 'vcc': os.path.join(ROOT, 'bench', 'pg', 'cc')}
DIRECT = {'clang': CLANG, 'gcc': GCC, 'vcc': VPP}   # for the amalgamation


def sh(cmd, **kw):
    print('+', ' '.join(cmd) if isinstance(cmd, list) else cmd, flush=True)
    return subprocess.run(cmd, check=True, **kw)


def fetch():
    if os.path.isdir(SRC):
        return
    os.makedirs(W, exist_ok=True)
    z = os.path.join(W, f'sqlite-src-{VER}.zip')
    if not os.path.exists(z):
        sh(['curl', '-sSfL', '-o', z, f'https://sqlite.org/{YEAR}/sqlite-src-{VER}.zip'])
    sh(['unzip', '-q', z, '-d', W])


def build(name):
    fetch()
    if not CC[name]:
        sys.exit(f'{name}: no compiler (set GCC)')
    b = os.path.join(W, 'b-' + name)
    shutil.rmtree(b, ignore_errors=True)
    os.makedirs(b)
    env = dict(os.environ, CC=CC[name], CFLAGS='-O2')
    if name == 'vcc':
        env['VCC_PG_LOG'] = os.path.join(b, 'fail.jsonl')
    with open(os.path.join(b, 'cfg.log'), 'w') as log:
        sh([os.path.join(SRC, 'configure'), f'--with-tclsh={TCL}/bin/tclsh', f'--with-tcl={TCL}/lib'],
           cwd=b, env=env, stdout=log, stderr=subprocess.STDOUT)
    t = time.time()
    with open(os.path.join(b, 'build.log'), 'w') as log:
        sh(['make', '-j6', 'testfixture', 'sqlite3'], cwd=b, env=env, stdout=log, stderr=subprocess.STDOUT)
    print(f'{name}: built in {time.time() - t:.1f} s')


def failed(name):
    db = os.path.join(W, 'b-' + name, 'testrunner.db')
    with sqlite3.connect(db) as c:
        return {r[0] for r in c.execute("select displayname from jobs where state='failed'")}


def test(name):
    b = os.path.join(W, 'b-' + name)
    for d in glob.glob(os.path.join(b, 'testdir*')) + glob.glob(os.path.join(b, 'testrunner.*')):
        shutil.rmtree(d) if os.path.isdir(d) else os.remove(d)
    with open(os.path.join(b, 'testrunner.out'), 'w') as log:
        subprocess.run(['./testfixture', os.path.join(SRC, 'test', 'testrunner.tcl'), '--jobs', '6'],
                       cwd=b, stdout=log, stderr=subprocess.STDOUT)
    out = open(os.path.join(b, 'testrunner.out'), errors='replace').read()
    m = re.findall(r'(\d+) errors out of (\d+) tests', out)
    f = failed(name)
    print(f'{name}: {m[-1][0] if m else "?"} errors out of {m[-1][1] if m else "?"} tests; failed: {" ".join(sorted(f)) or "-"}')
    return f


def speed(names, n, size):
    fetch()
    st = os.path.join(W, 'st')
    os.makedirs(st, exist_ok=True)
    if not os.path.exists(os.path.join(st, 'sqlite3.c')):
        # the amalgamation from any configured tree, else make one
        b = next((os.path.join(W, 'b-' + x) for x in ('clang', 'vcc', 'gcc')
                  if os.path.exists(os.path.join(W, 'b-' + x, 'sqlite3.c'))), None)
        if not b:
            sys.exit('speed: build one tree first (run.py build clang)')
        for f in ('sqlite3.c', 'sqlite3.h'):
            shutil.copy(os.path.join(b, f), st)
    shutil.copy(os.path.join(SRC, 'test', 'speedtest1.c'), st)
    flags = ['-O2', '-DSQLITE_OMIT_LOAD_EXTENSION', '-DSQLITE_THREADSAFE=0', '-I.']
    ct = {}
    for c in names:
        if not DIRECT[c]:
            sys.exit(f'{c}: no compiler')
        t = time.time()
        subprocess.run([DIRECT[c]] + flags + ['-c', 'sqlite3.c', '-o', f's-{c}.o'], cwd=st, check=True)
        ct[c] = time.time() - t
        subprocess.run([DIRECT[c]] + flags + ['-c', 'speedtest1.c', '-o', f't-{c}.o'], cwd=st, check=True)
        subprocess.run([CLANG, f's-{c}.o', f't-{c}.o', '-o', f'speedtest1-{c}'], cwd=st, check=True)
    hashes = {}
    for c in names:
        p = subprocess.run([f'./speedtest1-{c}', '--size', '30', '--memdb', '--verify', '--testset', 'main'],
                           cwd=st, capture_output=True, text=True)
        hashes[c] = re.findall(r'Verification Hash: (.*)', p.stdout)
    best = {c: 1e9 for c in names}
    for _ in range(n):
        for c in names:
            t = time.time()
            subprocess.run([f'./speedtest1-{c}', '--size', str(size), '--memdb', '--testset', 'main'], cwd=st,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
            best[c] = min(best[c], time.time() - t)
    base = min(best.values())
    print(f'\nspeedtest1 --size {size} --memdb, best of {n}; sqlite3.c -O2 compile time')
    for c in names:
        print(f'  {c:6} run {best[c]:6.2f} s ({best[c] / base:.2f}x)   compile {ct[c]:6.2f} s')
    ok = len({tuple(h) for h in hashes.values()}) == 1 and all(hashes.values())
    print('verification hash:', 'same' if ok else f'DIFFERS {hashes}')
    return ok


def main():
    a = sys.argv[1:]
    if not a:
        sys.exit(__doc__)
    cmd, a = a[0], a[1:]
    n, size = 3, 100
    if '-n' in a:
        i = a.index('-n'); n = int(a[i + 1]); del a[i:i + 2]
    if '--size' in a:
        i = a.index('--size'); size = int(a[i + 1]); del a[i:i + 2]
    if cmd == 'fetch':
        fetch()
    elif cmd == 'build':
        for x in a or ['vcc', 'clang']:
            build(x)
    elif cmd == 'test':
        return main_test(a or ['vcc', 'clang'])
    elif cmd == 'speed':
        return 0 if speed(a or ['clang', 'vcc'] + (['gcc'] if GCC else []), n, size) else 1
    elif cmd == 'all':
        names = ['vcc', 'clang'] + (['gcc'] if GCC else [])
        for x in names:
            build(x)
        r = main_test(['vcc', 'clang'])
        return r | (0 if speed(['clang'] + (['gcc'] if GCC else []) + ['vcc'], n, size) else 1)
    else:
        sys.exit(__doc__)
    return 0


def main_test(names):
    f = {x: test(x) for x in names}
    if not ('vcc' in f and 'clang' in f):
        return 1 if any(f.values()) else 0
    extra = f['vcc'] - f['clang']
    print('vcc-only failures:', ' '.join(sorted(extra)) or 'none')
    return 1 if extra else 0


if __name__ == '__main__':
    sys.exit(main())
