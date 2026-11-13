#!/usr/bin/env python3
"""vir-failures.py: why VIR gives up on a function (VIR-MIGRATION.md, gate G1).

  bench/vir-failures.py [-j N] [-n N] [--sets LIST] [--bin-dir DIR] [--level -O2|-O3] [--top K]

Compiles TUs with `vpp -S -O2 -fvir-stats` (doc/OPTIMIZATION.md): every
function the VIR builder or lowering cannot handle is compiled by the direct
emitter, and vcc prints per TU how many and why (src/codegen/vir/failure.h,
try.c).  This sums that over a set of TUs and prints the reasons, most
frequent first, with one example function and what the reason leaves out.

Sets (--sets, comma separated; default all that exist on this machine):
  suites    the vendored C suites (src/test/{vcc,gcc-torture,tcc-tests2,inline}),
            aarch64 Linux, flags as bench/vcc-ctests.py
  kernel    the kernel TUs of work/build-o0 (hostenv.KTREE), -n N evenly spaced
  postgres  work/build-pg-*/compile_commands.json (a macOS build), -n N spaced
  sqlite    the amalgamation sqlite3.c under work/sqlite, -O2 as run.py speed
A set whose files are not on this machine is named as missing, not an error.

-n N   TUs per sample for kernel and postgres (default 150; 0: all)
-j N   parallel vpp processes (default 3)
--keep-g       keep -g in postgres' flags (it builds with -g, and VIR lowers nothing under
               -g: every function would be one debug_info failure; default: -g removed)
--bin-dir DIR  tools from DIR (default build/src, or $VCC_BIN)
--level L      the level, -O2 (default) or -O3 (with the vectorizer); x86-64 lowers the same way
Counts are functions, not TUs.  A TU vpp rejects (exit status != 0) is listed
as failed and counted only as far as vpp reported.
"""
import glob, importlib.util, json, os, re, shlex, subprocess, sys, time
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
R = os.path.dirname(HERE)


def option(name, default, cast=str):
    """--name VALUE removed from argv"""
    if name in sys.argv:
        k = sys.argv.index(name)
        v = sys.argv[k + 1]
        del sys.argv[k:k + 2]
        return cast(v)
    return default


JOBS = option('-j', 3, int)
SAMPLE = option('-n', 150, int)
SETS = option('--sets', '', str)
LEVEL = option('--level', '-O2', str)
TOP = option('--top', 0, int)
KEEPG = '--keep-g' in sys.argv
if KEEPG:
    sys.argv.remove('--keep-g')
BINDIR = option('--bin-dir', '', str)
if BINDIR:
    os.environ['VCC_BIN'] = os.path.abspath(BINDIR)

sys.path.insert(0, HERE)
from hostenv import BUILD, WORK, KTREE

VPP = os.path.join(os.environ.get('VCC_BIN') or BUILD, 'vpp')
TMO = 300


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def sample(items, n):
    """n items evenly spaced over the sorted list (all when n is 0 or enough)"""
    if not n or len(items) <= n:
        return items
    return [items[i * len(items) // n] for i in range(n)]


# ---- the sets: each returns (list of (label, argv, cwd), None) or (None, why it is missing)

def set_suites():
    try:
        ct = load('vcc_ctests', f'{HERE}/vcc-ctests.py')
    except Exception as e:	# (needs clang's resource directory)
        return None, f'bench/vcc-ctests.py would not load: {e}'
    out = []
    for suite in ('vcc', 'gcc', 'tcc', 'inline'):
        for s, name, path in ct.tests(suite, None):
            src = open(path, 'rb').read().decode('latin-1')
            if s == 'gcc':
                fl = ct.dg_flags(src)
            elif s == 'inline':
                fl = [f for f in ct.dg_flags(src) if ct.INLINE_FLAGS.match(f)]
            elif s == 'vcc':
                fl = ct.exec_marks(src)[0]
            else:
                fl = ['-fdollars-in-identifiers'] if name.startswith('76_dollars') else []
            out.append((f'{s}/{name}', ct.BASE + ct.CLFL + fl + [path], os.path.dirname(path)))
    return (out, None) if out else (None, 'src/test has no C files')


def set_kernel():
    if not os.path.isdir(KTREE):
        return None, f'no kernel build tree at {KTREE} (hostenv.KTREE; set $VCC_KTREE)'
    vd = load('vcc_diff', f'{HERE}/vcc-diff.py')
    ts = [t for t in vd.tus() if t[1][-1].endswith('.c') and '-E' not in t[1] and not t[0].startswith('scripts/')]
    if not ts:
        return None, f'no .cmd files under {KTREE}'
    return [(o, a, KTREE) for o, a in sample(ts, SAMPLE)], None


def set_postgres():
    for d in ('build-pg-clang', 'build-pg-vcc', 'build-pg-vccd'):
        cc = os.path.join(WORK, d, 'compile_commands.json')
        if os.path.exists(cc):
            break
    else:
        return None, f'no {WORK}/build-pg-*/compile_commands.json (bench/pg/build.sh clang setup)'
    ents = [e for e in json.load(open(cc)) if e['file'].endswith('.c')]
    ents.sort(key=lambda e: e['file'])
    out = []
    for e in sample(ents, SAMPLE):
        a = shlex.split(e['command'])[1:]
        b, k = [], 0
        while k < len(a):
            if a[k] in ('-MQ', '-MF', '-o'):
                k += 2
                continue
            if a[k] not in ('-MD', '-MMD', '-c') and not a[k].startswith('-fdiagnostics-color') and (KEEPG or a[k] != '-g'):
                b.append(a[k])
            k += 1
        out.append((os.path.relpath(os.path.join(e['directory'], e['file']), WORK + '/postgres'), b, e['directory']))
    return out, None


def set_sqlite():
    cands = glob.glob(f'{WORK}/sqlite/st/sqlite3.c') + sorted(glob.glob(f'{WORK}/sqlite/b-*/sqlite3.c'))
    if not cands:
        return None, f'no sqlite3.c under {WORK}/sqlite (bench/sqlite/run.py fetch, then build)'
    c = cands[0]
    return [('sqlite3.c', ['-O2', '-DSQLITE_OMIT_LOAD_EXTENSION', '-DSQLITE_THREADSAFE=0', '-I.', 'sqlite3.c'],
             os.path.dirname(c))], None


ALL = {'suites': set_suites, 'kernel': set_kernel, 'postgres': set_postgres, 'sqlite': set_sqlite}


# ---- running

TOTAL = re.compile(r'^vcc vir: attempted=(\d+) lowered=(\d+) built=(\d+) fallback=(\d+)$')
REASON = re.compile(r'^vcc vir-failure: reason=(\S+) count=(\d+) example=(\S*) detail=(.*)$')


def run(job):
    label, argv, cwd = job
    t = time.time()
    try:   # (the level after the TU's own -O)
        p = subprocess.run([VPP, '-S', '-o', '/dev/null'] + argv + [LEVEL, '-fvir-stats'], cwd=cwd, capture_output=True,
                           text=True, errors='replace', timeout=TMO)
        rc, err = p.returncode, p.stderr
    except subprocess.TimeoutExpired:
        rc, err = -1, ''
    # the last block of lines is the final round of the TU (earlier rounds print too when inlining repeats a TU)
    tot, reasons = None, []
    for line in err.splitlines():
        m = TOTAL.match(line)
        if m:
            tot, reasons = tuple(int(x) for x in m.groups()), []
            continue
        m = REASON.match(line)
        if m:
            reasons.append((m.group(1), int(m.group(2)), m.group(3), m.group(4)))
    return label, rc, tot, reasons, time.time() - t, err[-300:]


def table(name, jobs, res):
    ok = [r for r in res if r[2]]
    bad = [r for r in res if not r[2]]
    att = sum(r[2][0] for r in ok)
    low = sum(r[2][1] for r in ok)
    blt = sum(r[2][2] for r in ok)
    fb = sum(r[2][3] for r in ok)
    cnt, ex, nt = Counter(), {}, Counter()
    for label, rc, tot, reasons, _, _ in ok:
        for reason, n, fn, detail in reasons:
            cnt[reason] += n
            nt[reason] += 1
            ex.setdefault(reason, (f'{label}:{fn}', detail))
    print(f'\n== {name}{" (-g removed from the flags)" if name == "postgres" and not KEEPG else ""}: {len(jobs)} TUs, {len(bad)} without a statistic '
          f'(vpp failed or timed out), {sum(1 for r in ok if r[1])} more exited nonzero after printing one')
    print(f'   functions: {att} attempted, {low} through VIR ({100 * low / max(att, 1):.1f} %), '
          f'{fb} fell back to the direct emitter, {blt} built only')
    if not cnt:
        print('   no fallbacks')
    else:
        w = max(len(k) for k in cnt)
        print(f'   {"reason":<{w}} {"functions":>9} {"TUs":>5}  example (TU:function)  [detail]')
        for k, n in cnt.most_common(TOP or None):
            print(f'   {k:<{w}} {n:>9} {nt[k]:>5}  {ex[k][0]}' + (f'  [{ex[k][1]}]' if ex[k][1] else ''))
    for r in bad[:5]:
        print(f'   no statistic: {r[0]} (exit {r[1]}) {r[5].strip()[-160:]!r}')
    return cnt, ex, (att, low, fb, blt)


def main():
    if not os.path.exists(VPP):
        sys.exit(f'{VPP}: not built (make -C src -j 3 vcc OUT=DIR, then --bin-dir DIR)')
    want = [s for s in SETS.split(',') if s] or list(ALL)
    unknown = [s for s in want if s not in ALL]
    if unknown:
        sys.exit(f'unknown set {unknown[0]}; sets: {", ".join(ALL)}')
    missing, grand, gex, gt = [], Counter(), {}, [0, 0, 0, 0]
    for name in want:
        jobs, why = ALL[name]()
        if not jobs:
            missing.append((name, why))
            continue
        t = time.time()
        with ThreadPoolExecutor(JOBS) as pool:
            res = list(pool.map(run, jobs))
        cnt, ex, t4 = table(name, jobs, res)
        print(f'   ({time.time() - t:.0f} s, -j {JOBS}, {LEVEL})')
        grand.update(cnt)
        for k, v in ex.items():
            gex.setdefault(k, (f'{name}:{v[0]}', v[1]))
        gt = [a + b for a, b in zip(gt, t4)]
    if missing:
        print()
        for name, why in missing:
            print(f'set {name}: NOT RUN, not found on this machine: {why}')
    if grand and len(want) - len(missing) > 1:
        att, low, fb, blt = gt
        print(f'\n== all sets: {att} functions attempted, {low} through VIR ({100 * low / max(att, 1):.1f} %), {fb} fell back')
        w = max(len(k) for k in grand)
        for k, n in grand.most_common(TOP or None):
            print(f'   {k:<{w}} {n:>9}  {gex[k][0]}' + (f'  [{gex[k][1]}]' if gex[k][1] else ''))


if __name__ == '__main__':
    main()
