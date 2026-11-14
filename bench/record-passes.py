#!/usr/bin/env python3
"""What each arm64 record clean-up pass changes (doc/quality/VIR-MIGRATION.md, "Order" step 1).

vpp counts per pass, with VCC_REC_STATS=1, the records it changes, removes or
adds, separately for functions that came through VIR's lowering and through
the direct emitter (stderr: "vcc rec-pass: path=vir pass=copies_1 runs=N
changed=N removed=N added=N", src/arch/a64/codegen/opt.c).  This harness runs
vpp -c over TU sets twice, at -O1 (the direct emitter) and at -O2 (VIR),
and prints per pass the records changed (changed + removed + added) in each
mode, most active in VIR's mode first: the top of the table moves into VIR
first, a pass at the bottom only needs deleting.

  bench/record-passes.py [-j 3] [-n 300] [--set suites,kernel,sqlite] [--bin-dir DIR] [-v]

Sets: `suites` (the vendored C suites, ccs/vcc tests: vcc, inline, gcc-torture,
tcc-tests2; always), `kernel` (the TUs of the configured kernel tree, KTREE of
bench/hostenv.py, a seeded random sample of -n, default 300), `sqlite` (the
amalgamation of work/sqlite, bench/sqlite/run.py).  A set that is not there is
reported and left out.  A TU that vpp rejects is counted and left out.

Columns: `direct` the -O1 run (the direct emitter); `vir` and `fallback`
the -O2 run, functions lowered by VIR and functions VIR gave up on that
the direct emitter then did.  `-v` adds the changed/removed/added split.
"""
import importlib.util, os, random, re, subprocess, sys
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
args = sys.argv[1:]


def opt(name, default, conv=str):
    if name in args:
        k = args.index(name)
        v = conv(args[k + 1])
        del args[k:k + 2]
        return v
    return default


bin_dir = opt('--bin-dir', os.environ.get('VCC_BIN'))
if bin_dir:
    os.environ['VCC_BIN'] = os.path.abspath(bin_dir)
jobs = opt('-j', 3, int)
nkern = opt('-n', 300, int)
want = opt('--set', 'suites,kernel,sqlite').split(',')
verbose = '-v' in args
if verbose:
    args.remove('-v')
if args:
    sys.exit(__doc__)

from hostenv import llvm, BUILD, KTREE, WORK
R = os.path.dirname(HERE)
BIN = os.environ.get('VCC_BIN', BUILD)
VPP = os.environ.get('VPP', f'{BIN}/vpp')
T = f'{R}/src/test'
LINE = re.compile(rb'^vcc rec-pass: path=(\w+) pass=(\w+) runs=(\d+) changed=(\d+) removed=(\d+) added=(\d+)$')
MODES = [('direct', ['-O1']), ('O2', ['-O2'])]	# (the level after the TU's own -O)
COLS = [('direct', 'direct'), ('O2', 'vir'), ('O2', 'direct')]	# (mode, path)
HEAD = ['direct', 'vir', 'fallback']
LENIENT = ['-w', '-Wno-error=implicit-function-declaration', '-Wno-error=implicit-int',
           '-Wno-error=int-conversion', '-Wno-error=incompatible-pointer-types', '-Wno-error=return-type',
           '-Wno-error=incompatible-function-pointer-types', '-fno-strict-aliasing', '-ffp-contract=off']


def suites():
    try:
        res = subprocess.run([llvm('clang'), '-print-resource-dir'], capture_output=True, text=True).stdout.strip() + '/include'
    except OSError:
        return None
    base = ['--target=aarch64-linux-gnu', '-nostdinc', '-I', f'{T}/rt/include', '-isystem', res,
            '-fno-PIE', '-fno-asynchronous-unwind-tables', '-fno-stack-protector', '-O2'] + LENIENT
    out = []
    for d in ('vcc', 'inline/gcc', 'inline/llvm', 'gcc-torture/execute', 'tcc-tests2'):
        p = f'{T}/{d}'
        if os.path.isdir(p):
            out += [(f'{d}/{f}', base + [f'{p}/{f}'], p) for f in sorted(os.listdir(p))
                    if f.endswith('.c') and (d != 'tcc-tests2' or re.match(r'\d+_', f))]
    return out


def kernel():
    if not os.path.isdir(KTREE):
        return None
    spec = importlib.util.spec_from_file_location('vccdiff', f'{HERE}/vcc-diff.py')
    m = importlib.util.module_from_spec(spec)
    sys.argv = sys.argv[:1]
    spec.loader.exec_module(m)
    tus = [(o, a) for o, a in m.tus() if re.search(r'\.c$', a[-1])]
    if not tus:
        return None
    random.Random(1).shuffle(tus)
    return [(o, a + ['-O2'], KTREE) for o, a in sorted(tus[:nkern])]


def sqlite():
    for d in ('st', 'b-vcc', 'b-clang', 'b-gcc'):
        p = f'{WORK}/sqlite/{d}'
        if os.path.exists(f'{p}/sqlite3.c'):
            return [('sqlite3.c', ['-O2', '-DSQLITE_OMIT_LOAD_EXTENSION', '-DSQLITE_THREADSAFE=0', '-I.', 'sqlite3.c'], p)]
    return None


def run1(job):
    """one TU in both modes: [{(path, pass): [runs, changed, removed, added]}, ...] or None per mode"""
    name, a, cwd = job
    res = []
    for mode, extra in MODES:
        env = dict(os.environ, VCC_REC_STATS='1')
        p = subprocess.run([VPP, '-c'] + a + extra + ['-o', os.devnull], cwd=cwd, env=env, capture_output=True)
        if p.returncode:
            res.append(None)
            continue
        d = {}
        for l in p.stderr.splitlines():
            m = LINE.match(l)
            if m:
                k = (m.group(1).decode(), m.group(2).decode())
                v = d.setdefault(k, [0, 0, 0, 0])
                for i in range(4):
                    v[i] += int(m.group(3 + i))
        res.append(d)
    return name, res


def run_set(name, tus):
    tot = {m: defaultdict(lambda: [0, 0, 0, 0]) for m, _ in MODES}
    ok = {m: 0 for m, _ in MODES}
    bad = {m: 0 for m, _ in MODES}
    with ThreadPoolExecutor(jobs) as ex:
        for _, res in ex.map(run1, tus):
            for (m, _), d in zip(MODES, res):
                if d is None:
                    bad[m] += 1
                    continue
                ok[m] += 1
                for k, v in d.items():
                    for i in range(4):
                        tot[m][k][i] += v[i]
    return tot, ok, bad


def table(name, tot, ok, bad):
    print(f'\n== {name}: {ok["direct"]} TUs compiled (-O1), {ok["O2"]} (-O2); '
          f'rejected: {bad["direct"]} / {bad["O2"]}')
    cols = [tot[m] for m, _ in COLS]
    keys = [(m, pth) for m, pth in COLS]

    def get(i, pas):
        return tot[keys[i][0]].get((keys[i][1], pas), [0, 0, 0, 0])

    passes = sorted({p for m in tot.values() for (_, p) in m})
    fn = passes and 'functions' in passes
    passes = [p for p in passes if p != 'functions']
    if fn:
        print('functions through the clean-ups: ' + ', '.join(f'{HEAD[i]} {get(i, "functions")[0]}' for i in range(3)))

    def chg(v):
        return v[1] + v[2] + v[3]
    passes.sort(key=lambda p: (-chg(get(1, p)), -chg(get(0, p)), p))
    print(f'{"pass":<15}' + ''.join(f'{h + " runs":>14}{h + " chg":>14}' for h in HEAD))
    for p in passes:
        row = f'{p:<15}'
        for i in range(3):
            v = get(i, p)
            row += f'{v[0]:>14}{chg(v):>14}'
        print(row)
    if verbose:
        print('  split changed/removed/added:')
        for p in passes:
            print(f'  {p:<15}' + ''.join('{:>26}'.format('/'.join(map(str, get(i, p)[1:]))) for i in range(3)))


def main():
    if not os.path.exists(VPP):
        sys.exit(f'{VPP} not found (make -C src vcc, or --bin-dir)')
    gens = {'suites': suites, 'kernel': kernel, 'sqlite': sqlite}
    missing = []
    for name in want:
        if name not in gens:
            sys.exit(f'unknown set {name}')
        tus = gens[name]()
        if not tus:
            missing.append(name)
            continue
        sys.stdout.flush()
        table(name, *run_set(name, tus))
    if missing:
        print('\nsets not found: ' + ', '.join(missing))


main()
