"""bench/sqlite/run.py --target win64 (doc/PLAN-win64.md WIN8): SQLite cross-built
for x86_64-w64-mingw32 by clang (the reference) and by vcc, run under Wine.

  bench/sqlite/run.py --target win64 [build|test|speed|all] [clang vcc vir] [--full] [-n N]

Sources: the amalgamation (sqlite3.c, sqlite3.h, shell.c) and the source tree's
test programs, both pinned zips of 3.50.4 in work/sqlite.  Each build in
build/linux/win64-projects/sqlite/b-NAME is all one toolchain's (bench/win64proj.py:
clang + lld + compiler-rt, or vcc's own with PATH emptied of foreign tools and
every link map checked): sqlite3.c with the default options (threadsafe, the
Windows VFS: critical sections, Interlocked*, the file APIs, TLS through
_beginthreadex threads) plus FTS5, R-Tree, math functions and the bytecode/stmt
vtabs; the shell sqlite3.exe; speedtest1.exe; mptester.exe; threadtest3.exe; and
fuzzcheck.exe from its own build of sqlite3.c with fuzzcheck's options.

`test`, each program under Wine, vcc's results against clang's:
  shell     bench/sqlite/win64-corpus.sql through sqlite3.exe on a file database
            (WAL) and on :memory:: stdout and stderr byte-identical to clang's;
  speedtest1  --testset mix1 --size 10 --verify on a file database: the
            verification hash equal to clang's;
  fuzzcheck the eight test/fuzzdata*.db: no error, the same summary line;
  mptester  crash01 and multiwrite01 in two journal modes, --repeat 2
            (--full: main.mk's eight runs, --repeat 20): several processes on one
            database, exit 0 and no errors, or exactly clang's build's result
            (in WAL mode both report 'winUnlockReadLock ... Lock violation' at
            the end under Wine 11: Wine's, printed and not counted);
  threadtest3  walthread5 cgt_pager_1 stress1 vacuum1 (--full: all 15, ~4 min):
            "0 errors".
SQLite's TCL test suite needs a tclsh for Windows (and testfixture.exe built
against its Tcl), which this host does not have: it is not run.
`speed`: speedtest1 --size 100 --testset main --memdb under Wine, best of -n
(wall time; for information) and the time to compile sqlite3.c.
"""
import hashlib, os, re, shutil, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import hostenv
import win64proj as wp

VER, YEAR = '3500400', '2025'
W = os.path.join(hostenv.WORK, 'sqlite')
SRC = os.path.join(W, f'sqlite-src-{VER}')
AMAL = os.path.join(W, f'sqlite-amalgamation-{VER}')
ZIPS = {f'sqlite-src-{VER}.zip': 'b7b4dc060f36053902fb65b344bbbed592e64b2291a26ac06fe77eec097850e9',
        f'sqlite-amalgamation-{VER}.zip': '1d3049dd0f830a025a53105fc79fd2ab9431aea99e137809d064d8ee8356b032'}
CORPUS = os.path.join(HERE, 'win64-corpus.sql')
OPTS = ['-O2', '-DSQLITE_ENABLE_FTS5', '-DSQLITE_ENABLE_RTREE', '-DSQLITE_ENABLE_MATH_FUNCTIONS',
        '-DSQLITE_ENABLE_BYTECODE_VTAB', '-DSQLITE_ENABLE_STMTVTAB', '-DSQLITE_ENABLE_DBSTAT_VTAB',
        '-DSQLITE_ENABLE_EXPLAIN_COMMENTS']
FUZZ_OPTS = ['-O2', '-DSQLITE_OSS_FUZZ', '-DSQLITE_ENABLE_BYTECODE_VTAB', '-DSQLITE_ENABLE_DBPAGE_VTAB',
             '-DSQLITE_ENABLE_DBSTAT_VTAB', '-DSQLITE_ENABLE_DESERIALIZE', '-DSQLITE_ENABLE_EXPLAIN_COMMENTS',
             '-DSQLITE_ENABLE_FTS3_PARENTHESIS', '-DSQLITE_ENABLE_FTS4', '-DSQLITE_ENABLE_FTS5', '-DSQLITE_ENABLE_GEOPOLY',
             '-DSQLITE_ENABLE_MATH_FUNCTIONS', '-DSQLITE_ENABLE_MEMSYS5', '-DSQLITE_ENABLE_NORMALIZE',
             '-DSQLITE_ENABLE_OFFSET_SQL_FUNC', '-DSQLITE_ENABLE_PREUPDATE_HOOK', '-DSQLITE_ENABLE_RTREE',
             '-DSQLITE_ENABLE_SESSION', '-DSQLITE_ENABLE_STMTVTAB', '-DSQLITE_ENABLE_UNKNOWN_SQL_FUNCTION',
             '-DSQLITE_ENABLE_STAT4', '-DSQLITE_ENABLE_STMT_SCANSTATUS', '-DSQLITE_JSON_MAX_DEPTH=500',
             '-DSQLITE_MAX_MEMORY=50000000', '-DSQLITE_MAX_MMAP_SIZE=0', '-DSQLITE_OMIT_LOAD_EXTENSION',
             '-DSQLITE_PRINTF_PRECISION_LIMIT=1000', '-DSQLITE_PRIVATE=', '-DSQLITE_STRICT_SUBTYPE=1',
             '-DSQLITE_STATIC_RANDOMJSON']
FUZZ_SRC = ['test/fuzzcheck.c', 'test/ossfuzz.c', 'test/fuzzinvariants.c', 'ext/recover/dbdata.c',
            'ext/recover/sqlite3recover.c', 'test/vt02.c', 'ext/misc/percentile.c', 'ext/misc/randomjson.c']
FULL = False


def fetch():
    """the two zips (SHA-256 checked) unpacked into work/sqlite"""
    if os.path.isdir(SRC) and os.path.isdir(AMAL):
        return
    os.makedirs(W, exist_ok=True)
    for z, sha in ZIPS.items():
        p = os.path.join(W, z)
        if not os.path.exists(p):
            subprocess.run(['curl', '-sSfL', '-o', p + '.part', f'https://sqlite.org/{YEAR}/{z}'], check=True)
            os.replace(p + '.part', p)
        if hashlib.sha256(open(p, 'rb').read()).hexdigest() != sha:
            raise OSError(f'{p}: SHA-256 is not {sha}')
        subprocess.run(['unzip', '-q', '-o', p, '-d', W], check=True)


def bdir(name):
    return os.path.join(wp.OUT, 'sqlite', 'b-' + name)


def build(name):
    """every program of one build; (seconds, sqlite3.c compile seconds, problems)"""
    b = bdir(name)
    shutil.rmtree(b, ignore_errors=True)
    os.makedirs(b)
    vcc = name != 'clang'
    cc, env = (wp.vcc_cc(name), wp.vcc_env(name)) if vcc else (wp.CLANG_CC, wp.clang_env())
    link = wp.vcc_link(name) if vcc else wp.CLANG_LINK
    bad = []
    t0 = time.time()

    def run(cmd, what):
        p = subprocess.run(cmd, cwd=b, env=env, capture_output=True, text=True)
        open(os.path.join(b, 'build.log'), 'a').write(' '.join(cmd) + '\n' + p.stderr)
        if p.returncode:
            sys.exit(f'{name}: {what} failed:\n{p.stderr[-3000:]}')
        return p

    if vcc:
        rc, n, ch = wp.no_clang_headers(cc + OPTS + [os.path.join(AMAL, 'sqlite3.c')], b, env)
        if rc or not n or ch:
            bad.append(f'headers: {ch or "-H failed"}')
    t = time.time()
    run(cc + OPTS + ['-c', os.path.join(AMAL, 'sqlite3.c'), '-o', 'sqlite3.o'], 'sqlite3.c')
    ct = time.time() - t
    run(cc + FUZZ_OPTS + ['-I' + AMAL, '-c', os.path.join(AMAL, 'sqlite3.c'), '-o', 'sqlite3-fuzz.o'], 'sqlite3.c (fuzzcheck)')
    progs = {
        'sqlite3': [os.path.join(AMAL, 'shell.c'), 'sqlite3.o'],
        'speedtest1': [os.path.join(SRC, 'test', 'speedtest1.c'), 'sqlite3.o'],
        'mptester': [os.path.join(SRC, 'mptest', 'mptest.c'), 'sqlite3.o'],
        'threadtest3': [os.path.join(SRC, 'test', 'threadtest3.c'), os.path.join(SRC, 'src', 'test_multiplex.c'), 'sqlite3.o'],
        'fuzzcheck': FUZZ_OPTS + ['-I' + os.path.join(SRC, 'test'), '-I' + os.path.join(SRC, 'ext', 'recover')] +
                     [os.path.join(SRC, f) for f in FUZZ_SRC] + ['sqlite3-fuzz.o'],
    }
    for prog, args in progs.items():
        cmd = link + (OPTS if prog != 'fuzzcheck' else []) + ['-w', '-I' + AMAL, '-I' + os.path.join(SRC, 'src')] + args + ['-o', prog + '.exe']
        if vcc:
            cmd += ['-v', f'-Wl,-Map={prog}.exe.map']
        p = run(cmd, prog)
        if vcc:
            f = wp.link_line_foreign(p.stderr) + wp.map_foreign(os.path.join(b, prog + '.exe.map'))
            if f:
                bad.append(f'{prog}: foreign input {" ".join(f)}')
    return time.time() - t0, ct, bad


def fresh(name, sub):
    d = os.path.join(bdir(name), sub)
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    return d


def t_shell(name):
    """the corpus on a file database and on :memory:: (stdout, stderr)"""
    sql = open(CORPUS, 'rb').read()
    out = []
    for db in ('test.db', ':memory:'):
        d = fresh(name, 'shell')
        rc, o, e = wp.wine(os.path.join(bdir(name), 'sqlite3.exe'), [db], cwd=d, input=sql, timeout=300)
        out.append((rc, wp.crlf(o), wp.crlf(e)))
    return out


def t_speedtest(name):
    d = fresh(name, 'speedtest')
    rc, o, e = wp.wine(os.path.join(bdir(name), 'speedtest1.exe'),
                       ['--testset', 'mix1', '--size', '10', '--verify', 'speed.db'], cwd=d, timeout=600)
    h = re.findall(rb'Verification Hash: (\S+)', o)
    return rc, h[-1].decode() if h else None, wp.crlf(e).decode(errors='replace')[-500:]


def t_fuzzcheck(name):
    d = fresh(name, 'fuzz')
    dbs = [os.path.join(SRC, 'test', f'fuzzdata{k}.db') for k in range(1, 9)]
    rc, o, e = wp.wine(os.path.join(bdir(name), 'fuzzcheck.exe'), dbs, cwd=d, timeout=900)
    s = re.findall(rb'fuzzcheck: (\d+) errors out of (\d+) tests[^\r\n]*', o + e)
    return rc, (s[-1][0].decode() + '/' + s[-1][1].decode()) if s else None, wp.crlf(o + e).decode(errors='replace')[-500:]


def t_mptest(name):
    d = fresh(name, 'mptest')
    exe = os.path.join(bdir(name), 'mptester.exe')
    rep = '20' if FULL else '2'
    runs = [('crash01', m) for m in ('DELETE', 'WAL', 'PERSIST', 'TRUNCATE')] + \
           [('multiwrite01', m) for m in ('WAL', 'PERSIST', 'TRUNCATE', 'DELETE')]
    if not FULL:
        runs = [('crash01', 'DELETE'), ('multiwrite01', 'WAL'), ('crash01', 'WAL'), ('multiwrite01', 'DELETE')]
    res = []
    for test, mode in runs:
        for f in os.listdir(d):
            os.remove(os.path.join(d, f))
        rc, o, e = wp.wine(exe, ['mptest.db', os.path.join(SRC, 'mptest', test + '.test'), '--repeat', rep,
                                 '--journalmode', mode], cwd=d, timeout=900)
        m = re.findall(rb'Summary: (\d+) errors out of (\d+) tests', o + e)
        res.append((test, mode, rc, m[-1][0].decode() if m else '?'))
    return res


def t_threadtest(name):
    d = fresh(name, 'threadtest')
    tests = [] if FULL else ['walthread5', 'cgt_pager_1', 'stress1', 'vacuum1']
    rc, o, e = wp.wine(os.path.join(bdir(name), 'threadtest3.exe'), tests, cwd=d, timeout=1200)
    s = re.findall(rb'(\d+) errors out of (\d+) tests', o)
    return rc, (s[-1][0].decode() + '/' + s[-1][1].decode()) if s else None, wp.crlf(o + e).decode(errors='replace')[-500:]


def test(names):
    rc = 0
    with ThreadPoolExecutor(len(names)) as ex:	# the builds' slow tests side by side
        thr = {x: ex.submit(t_threadtest, x) for x in names}
        res = {}
        for x in names:
            res[x] = dict(shell=t_shell(x), speed=t_speedtest(x), fuzz=t_fuzzcheck(x), mp=t_mptest(x))
        for x in names:
            res[x]['thread'] = thr[x].result()
    ref = res.get('clang')
    envnote = set()	# failures of clang's build too: Wine's, not vcc's
    for x in names:
        r, bad = res[x], []
        for (src, (sr, so, se)) in zip(('file', 'memory'), r['shell']):
            if ref and x != 'clang' and (sr, so, se) != ref['shell'][('file', 'memory').index(src)]:
                bad.append(f'shell({src}) differs from clang\'s')
        if r['speed'][0] or not r['speed'][1] or (ref and r['speed'][1] != ref['speed'][1]):
            bad.append(f'speedtest1 rc {r["speed"][0]} hash {r["speed"][1]} {r["speed"][2]}')
        if r['fuzz'][0] or not r['fuzz'][1] or not r['fuzz'][1].startswith('0/') or (ref and r['fuzz'][1] != ref['fuzz'][1]):
            bad.append(f'fuzzcheck rc {r["fuzz"][0]} {r["fuzz"][1]} {r["fuzz"][2]}')
        for k, (test_, mode, mrc, errs) in enumerate(r['mp']):
            if not mrc and errs == '0':
                continue
            if x == 'clang' or (ref and ref['mp'][k] == (test_, mode, mrc, errs)):
                envnote.add(f'mptester {test_} {mode}: rc {mrc}, {errs} error(s) with clang\'s build too')
            else:
                bad.append(f'mptester {test_} {mode}: rc {mrc} errors {errs}')
        if r['thread'][0] or not r['thread'][1] or not r['thread'][1].startswith('0/'):
            bad.append(f'threadtest3 rc {r["thread"][0]} {r["thread"][1]} {r["thread"][2]}')
        sh = r['shell'][0]
        print(f'{x:6} {"FAIL: " + "; ".join(bad) if bad else "ok"}: shell corpus {len(sh[1].splitlines())} lines '
              f'(file and :memory:{", = clang" if ref and x != "clang" and not any("shell" in b for b in bad) else ""}), '
              f'speedtest1 hash {r["speed"][1]}, fuzzcheck {r["fuzz"][1]} errors/tests, '
              f'mptester {len(r["mp"])} runs, threadtest3 {r["thread"][1]} errors/tests', flush=True)
        rc |= 1 if bad else 0
    for e in sorted(envnote):
        print(f'  under Wine, not counted: {e}')
    return rc


def speed(names, n):
    best = {x: 1e9 for x in names}
    for _ in range(n):
        for x in names:
            d = fresh(x, 'speed')
            t = time.time()
            rc, o, e = wp.wine(os.path.join(bdir(x), 'speedtest1.exe'), ['--size', '100', '--testset', 'main', '--memdb'],
                               cwd=d, timeout=900)
            if rc:
                print(f'{x}: speedtest1 failed ({rc})')
                return 1
            best[x] = min(best[x], time.time() - t)
    base = best.get('clang') or min(best.values())
    print(f'\nspeedtest1 --size 100 --testset main --memdb under Wine, best of {n} (wall; for information)')
    for x in names:
        print(f'  {x:6} {best[x]:6.2f} s ({best[x] / base:.2f}x)')
    return 0


def main(cmd, names, n, full):
    global FULL
    FULL = full
    why = wp.missing()
    if why:
        return wp.skip('sqlite --target win64', why)
    try:
        fetch()
    except (subprocess.CalledProcessError, OSError) as e:
        return wp.skip('sqlite --target win64', f'no SQLite source ({e})')
    wp.ensure_vccrt()
    names = names or ['clang', 'vcc', 'vir']
    rc = 0
    if cmd in ('build', 'all'):
        for x in names:
            secs, ct, bad = build(x)
            print(f'{x:6} built in {secs:5.1f} s (sqlite3.c -O2: {ct:5.2f} s){": " + "; ".join(bad) if bad else ", nothing foreign"}',
                  flush=True)
            rc |= 1 if bad else 0
    if cmd in ('test', 'all'):
        rc |= test(names)
        print('TCL test suite: not run (no tclsh for Windows on this host; testfixture.exe needs one)')
    if cmd in ('speed', 'all'):
        rc |= speed(names, n)
    print('sqlite --target win64:', 'ok' if not rc else 'FAIL')
    return rc
