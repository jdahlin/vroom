#!/usr/bin/env python3
"""vir-passes.py: where the VIR builder's time goes, pass by pass (doc/quality/WORK-vir.md step 2b).

  bench/vir-passes.py [-j N] [-n N] [--sets LIST] [--bin-dir DIR]
  bench/vir-passes.py --bisect [--suite vcc|gcc|tcc|inline] PAT [--bin-dir DIR]
  bench/vir-passes.py --bisect --cmd 'SHELL COMMAND' [--bin-dir DIR]

The builder's passes run from one table (src/codegen/vir/pass-table.c).  With
VCC_VIR=bl VCC_VIR_STATS=1 VCC_VIR_PASSES=time, vpp prints per TU and pass
"vcc vir-pass: pass=NAME runs=N changed=N instructions=N" (changed is `-` for
a pass that does not say), plus two totals: @build (the walk and every pass,
to-vir and canon included) and @lower (the target's lowering).  This runs
`vpp -O2 -c` over the TU sets of bench/vir-failures.py (suites, kernel sample,
SQLite; PostgreSQL with --sets postgres) and prints per pass: runs, runs that
changed something, instructions (millions) and its share of @build; `walk` is
@build less every pass (the AST walk through the builder's LO() interface).
Instructions are vperf's (macOS); elsewhere they are 0 and only the counts mean
something.  The timing's own cost (two counter reads per pass, measured once
per process) is taken off each pass and off @build.

--bisect: for a TU whose VIR build misbehaves, rerun it with each optional pass
left out in turn (VCC_VIR_PASSES=-NAME) and say which leaving-out makes it
pass.  PAT is a test of bench/vcc-ctests.py (run with VCC_VIR=bl); --cmd runs
any command instead (exit status 0: it passes), with VCC_VIR_PASSES set.

-n N   TUs per sample for kernel and postgres (default 200; 0: all)
-j N   parallel vpp processes (default 3)
--sets suites,kernel,sqlite,postgres   (default: suites, kernel, sqlite)
--bin-dir DIR  tools from DIR (default build/src, or $VCC_BIN)
"""
import importlib.util, os, re, subprocess, sys, time
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
R = os.path.dirname(HERE)


def option(name, default, cast=str):
    if name in sys.argv:
        k = sys.argv.index(name)
        v = sys.argv[k + 1]
        del sys.argv[k:k + 2]
        return cast(v)
    return default


def flag(name):
    if name in sys.argv:
        sys.argv.remove(name)
        return True
    return False


JOBS = option('-j', 3, int)
SAMPLE = option('-n', 200, int)
SETS = option('--sets', 'suites,kernel,sqlite', str)
BINDIR = option('--bin-dir', '', str)
SUITE = option('--suite', 'vcc', str)
CMD = option('--cmd', '', str)
BISECT = flag('--bisect')
if BINDIR:
    os.environ['VCC_BIN'] = os.path.abspath(BINDIR)
ARGS = sys.argv[1:]

sys.path.insert(0, HERE)
from hostenv import BUILD

BIN = os.environ.get('VCC_BIN') or BUILD
VPP = os.path.join(BIN, 'vpp')
TMO = 300
LINE = re.compile(r'^vcc vir-pass: pass=(\S+) runs=(\d+) changed=(\d+|-) instructions=(\d+)')
TOTAL = re.compile(r'^vcc vir: attempted=')
PASSLIST = re.compile(r'\(passes: (.*); \* required\)')


def vir_failures():
    """bench/vir-failures.py as a module, for its TU sets (it reads sys.argv when loaded)"""
    argv, sys.argv = sys.argv, sys.argv[:1]
    try:
        spec = importlib.util.spec_from_file_location('vir_failures', f'{HERE}/vir-failures.py')
        m = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(m)
    finally:
        sys.argv = argv
    m.SAMPLE = SAMPLE
    return m


def run(job):
    """one TU: {pass: [runs, changed or None, instructions]} of its final round, or None"""
    label, argv, cwd = job
    env = dict(os.environ, VCC_VIR='bl', VCC_VIR_STATS='1', VCC_VIR_PASSES='time')
    try:
        p = subprocess.run([VPP, '-c', '-o', os.devnull] + argv, cwd=cwd, capture_output=True, text=True,
                           errors='replace', env=env, timeout=TMO)
        err = p.stderr
    except subprocess.TimeoutExpired:
        return label, None, []
    d, order = None, []
    for line in err.splitlines():
        if TOTAL.match(line):
            d, order = {}, []
            continue
        m = LINE.match(line)
        if m and d is not None:
            name = m.group(1)
            if name not in d:
                order.append(name)
                d[name] = [0, 0, 0]
            v = d[name]
            v[0] += int(m.group(2))
            v[1] = None if m.group(3) == '-' else (v[1] or 0) + int(m.group(3))
            v[2] += int(m.group(4))
    return label, d, order


def table(name, jobs, res):
    tot = defaultdict(lambda: [0, 0, 0])
    order, ok, bad = [], 0, []
    for label, d, o in res:
        if not d:
            bad.append(label)
            continue
        ok += 1
        for k in o:
            if k not in order:
                order.append(k)
        for k, v in d.items():
            t = tot[k]
            t[0] += v[0]
            t[1] = None if v[1] is None else t[1] + v[1]
            t[2] += v[2]
    print(f'\n== {name}: {len(jobs)} TUs, {ok} with a statistic' + (f', {len(bad)} without (vpp failed or timed out)' if bad else ''))
    if not ok:
        return tot
    build = tot['@build'][2]
    passes = [k for k in order if not k.startswith('@')]
    walk = build - sum(tot[k][2] for k in passes)
    print(f'   functions built: {tot["@build"][0]}, lowered: {tot["@lower"][0]}; '
          f'@build {build / 1e6:.1f} M instructions, @lower {tot["@lower"][2] / 1e6:.1f} M')
    print(f'   {"pass":<18} {"runs":>8} {"changed":>8} {"M instr":>9} {"% build":>8}')
    for k in passes + ['walk']:
        runs, ch, ins = tot[k] if k != 'walk' else ('', '', walk)
        ch = '-' if ch is None else ch
        print(f'   {k:<18} {runs:>8} {ch:>8} {ins / 1e6:>9.2f} {100 * ins / max(build, 1):>7.1f}%')
    for b in bad[:5]:
        print(f'   no statistic: {b}')
    return tot


def stats():
    if ARGS:
        sys.exit(__doc__)
    vf = vir_failures()
    want = [s for s in SETS.split(',') if s]
    missing = []
    for name in want:
        if name not in vf.ALL:
            sys.exit(f'unknown set {name}; sets: {", ".join(vf.ALL)}')
        jobs, why = vf.ALL[name]()
        if not jobs:
            missing.append((name, why))
            continue
        t = time.time()
        with ThreadPoolExecutor(JOBS) as pool:
            res = list(pool.map(run, jobs))
        table(name, jobs, res)
        print(f'   ({time.time() - t:.0f} s, -j {JOBS})')
    for name, why in missing:
        print(f'set {name}: NOT RUN, not found on this machine: {why}')


def optional_passes():
    """the table's passes that may be left out, from vpp's own list (an unknown pass prints it)"""
    p = subprocess.run([VPP, '-O2', '-x', 'c', '-c', '-o', os.devnull, '-'], input='int f(void) { return 0; }\n',
                       capture_output=True, text=True, env=dict(os.environ, VCC_VIR='bl', VCC_VIR_PASSES='-?'))
    m = PASSLIST.search(p.stderr)
    if not m:
        sys.exit(f'vpp did not list its passes: {p.stderr.strip()[:300]}')
    return [n for n in m.group(1).split() if not n.endswith('*')]


def bisect():
    if CMD:
        what = CMD

        def passes(extra):
            env = dict(os.environ, VCC_VIR=os.environ.get('VCC_VIR', 'bl'), **extra)
            return subprocess.run(CMD, shell=True, env=env, capture_output=True).returncode == 0
    else:
        if len(ARGS) != 1:
            sys.exit(__doc__)
        what = f'vcc-ctests.py --suite {SUITE} {ARGS[0]}'

        def passes(extra):
            env = dict(os.environ, VCC_VIR='bl', VCC_BIN=BIN, **extra)
            p = subprocess.run([sys.executable, f'{HERE}/vcc-ctests.py', '-j', '1', '--suite', SUITE, ARGS[0]],
                               env=env, capture_output=True, text=True)
            return p.returncode == 0
    print(f'bisect: {what}')
    if passes({}):
        print('bisect: it passes with every pass: nothing to bisect')
        return
    names = optional_passes()
    fixed = []
    for n in names:
        ok = passes({'VCC_VIR_PASSES': f'-{n}'})
        print(f'  -{n:<18} {"passes" if ok else "fails"}', flush=True)
        if ok:
            fixed.append(n)
    if fixed:
        print('bisect: leaving out ' + ', '.join(fixed) + ' makes it pass')
    else:
        print('bisect: it fails whichever optional pass is left out (a required pass, the walk or the lowering)')


def main():
    if not os.path.exists(VPP):
        sys.exit(f'{VPP}: not built (make -C src -j 3 vcc OUT=DIR, then --bin-dir DIR)')
    bisect() if BISECT else stats()


if __name__ == '__main__':
    main()
