#!/usr/bin/env python3
"""vbench: small, load-independent benchmarks of the vcc tools, one per module.

The metric is *instructions retired* by the measured threads (src/lib/vperf.h,
no root needed): it hardly moves with machine load (~0.5 % noise), so many
agents can measure at the same time on a shared machine.  Cycles and wall time
are printed too but are only trustworthy in an A/B run (`ab`, alternating).

  vbench.py run [MODE...] [--quick] [--reps N] [--bin DIR]
  vbench.py ab  [MODE...] --base DIR [--quick] [--reps N] [--bin DIR] [--per-tu]
  vbench.py check [--quick] [--bin DIR] [--base DIR]
  vbench.py gate [MODE...] [--base DIR] [--budget PCT] [--quick] [--reps N] [--bin DIR]
                                  A/B against --base (default: mkbase of the merge-base
                                  with main); exit 1 if any mode's instructions grew by
                                  more than PCT (default 1) or twice the noise (TODO.md T0)
  vbench.py mkbase [REV]          build REV's src/ (default: merge-base with main)
                                  into work/vbench/base/<sha>/; prints the bin dir
  vbench.py pick TSV              choose the corpus from a `vpp --batch --stats`
                                  profile of all TUs (-c, VPP_NOHC=1 VCC_NODC=1)

Modes (default: all; one thread each):
  lex   lextest core: lexing + interning of the 3 225 build files, pre-mapped
  pp    corpus, tokens only (pp_next), token cache warm, no header cache
  pphc  corpus, tokens only, header cache on, cold (record + replay)
  fe    corpus, -fsyntax-only (pp + parse + sema), warm, no header/decl cache
  cc    corpus, -c, warm, no caches; also reports the emit and obj phases
  emit  as cc, but the metric is emit_tu alone (codegen + encoding)
  real  corpus, -c, all caches on, cold (closest to vccd)
  as    vasm --bench over the 30 preprocessed kernel .S files
  ld    vlink --threads=1: the final vmlinux link of work/build-o0

The corpus (bench/vbench-corpus.txt) is 24 kernel TUs chosen to match the
instruction profile of all 840 (quantiles + emit- and parse-heavy ones);
--quick takes the 9 marked `quick`.  pp - fe - cc differences give the parse
and codegen shares.  The header-cache modes (pphc, real) move whenever the
cache changes; pp/fe/cc are the stable ones.

`check` hashes every output -- -E text, the token stream (--vcc-tokhash),
layout checks, objects with and without caches, lexer tokens, .S objects,
vmlinux -- and compares with the same hashes from --base (cached by the base
binaries' content in work/vbench/golden/).  A pure optimisation must keep them
all identical; a change that alters code generation on purpose needs
bench/vcc-obj.py cmp and the qemu tests instead.
"""
import hashlib, json, os, re, shutil, statistics, subprocess, sys, tempfile, time
import importlib.util

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
spec = importlib.util.spec_from_file_location('vd', f'{R}/bench/vcc-diff.py')
vd = importlib.util.module_from_spec(spec); spec.loader.exec_module(vd)
sys.path.insert(0, f"{R}/bench"); from hostenv import WORK as W; B = vd.B
VB = f'{W}/vbench'
CORPUS = f'{R}/bench/vbench-corpus.txt'
MODES = ['lex', 'pp', 'pphc', 'fe', 'cc', 'emit', 'real', 'as', 'ld']
LD_ARGS = ('--vroom-stubs -EL -maarch64elf -z norelro -z noexecstack --no-undefined -X --pic-veneer -shared '
           '-Bsymbolic -z notext --no-apply-dynamic-relocs --build-id=sha1 --orphan-handling=warn '
           '--script=arch/arm64/kernel/vmlinux.lds --whole-archive vmlinux.a .vmlinux.export.o '
           'init/version-timestamp.o --no-whole-archive').split()

def die(msg): sys.exit(f'vbench: {msg}')

# ---------------- corpus ----------------

def corpus(quick):
    names = []
    for l in open(CORPUS):
        l = l.split('#')[0].split()
        if l and (not quick or 'quick' in l[1:]): names.append(l[0])
    by = {o: a for o, a in vd.tus(None)}
    miss = [n for n in names if n not in by]
    if miss: die(f'corpus TUs not in {B}: {miss}')
    return [(n, by[n]) for n in names]

def jobfile(tus, extra, d, out=None):
    """a vpp --batch job file; out(obj) -> output path or None (discarded)"""
    p = f'{d}/jobs-{abs(hash((tuple(extra), out is None)))}'
    with open(p, 'w') as f:
        for o, a in tus:
            f.write('\x1f'.join(['-'] + a + extra + (['-o', out(o)] if out else [])) + '\n')
    return p

# ---------------- running ----------------

def vpp_batch(bin, jobs, env, warm=0, j=1, stats=None):
    e = dict(os.environ, **env)
    cmd = [f'{bin}/vpp', '--batch', jobs, '-j', str(j), '-C', B, '--warm', str(warm)]
    if stats: cmd += ['--stats', stats]
    t0 = time.time()
    p = subprocess.run(cmd, env=e, capture_output=True, text=True)
    wall = time.time() - t0
    m = re.search(r'perf: (\d+) instructions, (\d+) cycles \(jobs\); cc_run (\d+), emit (\d+), obj (\d+) instructions; emit (\d+) cycles', p.stdout)
    ok = re.search(r'(\d+)/(\d+) ok', p.stdout)
    if p.returncode or not m or not ok or ok.group(1) != ok.group(2):
        die(f'vpp --batch failed ({p.returncode}):\n{p.stdout[-2000:]}{p.stderr[-2000:]}')
    ins, cyc, cc, em, ob, emc = map(int, m.groups())
    return dict(ins=ins, cyc=cyc, cc=cc, emit=em, obj=ob, emit_cyc=emc, wall=wall)

def per_tu(stats):
    d = {}
    for l in open(stats):
        f = l.rstrip('\n').split('\t')
        d[f[7]] = int(f[2])
    return d

def timed(cmd, cwd=None, env=None):
    """a whole process through /usr/bin/time -l: instructions + cycles of all its threads"""
    t0 = time.time()
    p = subprocess.run(['/usr/bin/time', '-l'] + cmd, cwd=cwd, capture_output=True, text=True,
                       env=dict(os.environ, **(env or {})))
    wall = time.time() - t0
    if p.returncode: die(f'{cmd[0]} failed:\n{p.stdout[-1500:]}{p.stderr[-1500:]}')
    ins = int(re.search(r'(\d+)\s+instructions retired', p.stderr).group(1))
    cyc = int(re.search(r'(\d+)\s+cycles elapsed', p.stderr).group(1))
    return p, dict(ins=ins, cyc=cyc, wall=wall)

def inproc(out):
    m = re.search(r'perf: (\d+) instructions, (\d+) cycles', out)
    h = re.search(r'hash: (\S+)', out)
    return dict(ins=int(m.group(1)), cyc=int(m.group(2))), h.group(1) if h else None

NOC = {'VPP_NOHC': '1', 'VCC_NODC': '1'}

def run_mode(mode, bin, quick, d, stats=None):
    """one measurement: dict(ins, cyc, wall, ...); ins is the metric"""
    if mode == 'lex':
        t0 = time.time()
        p = subprocess.run([f'{bin}/lextest', 'core', f'{W}/lex-files.txt', '3'], capture_output=True, text=True)
        r, _ = inproc(p.stdout); r['wall'] = time.time() - t0; return r
    if mode == 'as':
        t0 = time.time()
        p = subprocess.run([f'{bin}/vasm', '--bench', '6'] + as_inputs(), cwd=B, capture_output=True, text=True)
        if p.returncode: die(f'vasm failed: {p.stderr[-1000:]}')
        r, _ = inproc(p.stdout); r['wall'] = time.time() - t0; return r
    if mode == 'ld':
        # the first link into a fresh directory costs ~15 % more: one untimed run first
        for warm in (1, 0):
            if os.path.exists(f'{d}/vmlinux'): os.unlink(f'{d}/vmlinux')
            _, r = timed([f'{bin}/vlink', '--threads=1'] + LD_ARGS + ['-o', f'{d}/vmlinux'], cwd=B)
        return r
    tus = corpus(quick)
    if mode == 'pp':
        return vpp_batch(bin, jobfile(tus, ['--vcc-tokhash'], d), dict(NOC, VPP_TOKCOUNT='1'), warm=1, stats=stats)
    if mode == 'pphc':
        return vpp_batch(bin, jobfile(tus, ['--vcc-tokhash'], d), {'VPP_TOKCOUNT': '1'}, stats=stats)
    if mode == 'fe':
        return vpp_batch(bin, jobfile(tus, ['-fsyntax-only'], d), NOC, warm=1, stats=stats)
    if mode == 'cc':
        return vpp_batch(bin, jobfile(tus, ['-c'], d), NOC, warm=1, stats=stats)
    if mode == 'emit':
        r = vpp_batch(bin, jobfile(tus, ['-c'], d), NOC, warm=1, stats=stats)
        r['ins'], r['cyc'] = r['emit'], r['emit_cyc']
        return r
    if mode == 'real':
        return vpp_batch(bin, jobfile(tus, ['-c'], d), {}, stats=stats)
    die(f'unknown mode {mode}')

def as_inputs():
    fs = sorted(f'{W}/asref/{f}' for f in os.listdir(f'{W}/asref') if f.endswith('.vpp.s'))
    if not fs: die(f'no {W}/asref/*.vpp.s (bench/vas-diff.py ref --vpp)')
    return fs

def fmt(r):
    s = f"{r['ins'] / 1e6:10.1f} M ins  {r['cyc'] / 1e6:9.1f} M cyc  IPC {r['ins'] / max(r['cyc'], 1):4.2f}  {r['wall']:6.2f} s"
    if r.get('cc'):
        s += f"   [cc_run {r['cc'] / 1e6:.0f} M, emit {r['emit'] / 1e6:.0f} M, obj {r['obj'] / 1e6:.0f} M]"
    return s

def best(rs):
    """min instructions and min cycles (noise only adds), median wall"""
    b = dict(rs[0])
    b['ins'] = min(r['ins'] for r in rs); b['cyc'] = min(r['cyc'] for r in rs)
    b['wall'] = statistics.median(r['wall'] for r in rs)
    b['spread'] = (max(r['ins'] for r in rs) - b['ins']) / b['ins'] if b['ins'] else 0
    return b

def cmd_run(modes, bin, quick, reps):
    with tempfile.TemporaryDirectory(prefix='vbench-') as d:
        for m in modes:
            b = best([run_mode(m, bin, quick, d) for _ in range(reps)])
            print(f'{m:5} {fmt(b)}  (spread {b["spread"] * 100:.2f} %)', flush=True)

def cmd_ab(modes, bin, base, quick, reps, show_tu):
    """prints the comparison; returns [(mode, instructions %, noise %)]"""
    res = []
    with tempfile.TemporaryDirectory(prefix='vbench-') as d:
        for m in modes:
            A, Bn, ta, tb = [], [], {}, {}
            for i in range(reps):	# alternate, so drifting load hits both alike
                for side, bdir, acc, tu in ((0, base, A, ta), (1, bin, Bn, tb)) if i % 2 == 0 else \
                                           ((1, bin, Bn, tb), (0, base, A, ta)):
                    st = f'{d}/st-{side}' if show_tu and m not in ('lex', 'as', 'ld') else None
                    acc.append(run_mode(m, bdir, quick, d, stats=st))
                    if st:
                        for k, v in per_tu(st).items(): tu[k] = min(tu.get(k, v), v)
            a, b = best(A), best(Bn)
            di = (b['ins'] - a['ins']) / a['ins'] * 100 if a['ins'] else 0
            dc = (b['cyc'] - a['cyc']) / a['cyc'] * 100 if a['cyc'] else 0
            noise = max(a['spread'], b['spread']) * 100
            verdict = 'BETTER' if di < -max(0.5, 2 * noise) else 'WORSE' if di > max(0.5, 2 * noise) else 'same'
            print(f'{m:5} base {fmt(a)}\n      new  {fmt(b)}\n      instructions {di:+.2f} %  cycles {dc:+.2f} %  '
                  f'(noise {noise:.2f} %)  {verdict}', flush=True)
            res.append((m, di, noise))
            if show_tu and ta:
                ds = sorted(((tb[k] - ta[k]) / ta[k] * 100, os.path.relpath(k, f'{W}/linux')) for k in ta if k in tb)
                for x, k in ds[:5] + ds[-5:]: print(f'        {x:+6.2f} %  {k}')
    return res

# the modes that only move when the code does (pphc/real move with the header cache)
GATE_MODES = ['lex', 'pp', 'fe', 'cc', 'emit', 'as', 'ld']

def cmd_gate(modes, bin, base, quick, reps, budget):
    """TODO.md T0: the hot path may not get slower than the budget"""
    if not base: base = cmd_mkbase(None, quiet=True)
    print(f'gate: {bin} against {base}, budget {budget:.2f} %', flush=True)
    res = cmd_ab(modes, bin, base, quick, reps, False)
    bad = [(m, di, n) for m, di, n in res if di > max(budget, 2 * n)]
    print('gate: ' + '  '.join(f'{m} {di:+.2f}%' for m, di, _ in res))
    for m, di, n in bad: print(f'gate: FAIL {m}: instructions {di:+.2f} % > budget {budget:.2f} % (noise {n:.2f} %)')
    print('gate: ' + ('FAIL' if bad else 'ok'))
    return 1 if bad else 0

# ---------------- correctness ----------------

def sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for c in iter(lambda: f.read(1 << 20), b''): h.update(c)
    return h.hexdigest()[:16]

def bin_key(bin):
    h = hashlib.sha256()
    for t in ('vpp', 'lextest', 'vasm', 'vlink'): h.update(open(f'{bin}/{t}', 'rb').read())
    return h.hexdigest()[:16]

def hashes(bin, quick):
    """every output of every tool on the corpus -> {name: hash}"""
    out = {}
    tus = corpus(quick)
    with tempfile.TemporaryDirectory(prefix='vbench-chk-') as d:
        name = lambda o, k: f'{d}/{k}/{o.replace("/", "__")}'
        kinds = [('E', [], NOC), ('Ehc', [], {}), ('tok', ['--vcc-tokhash'], NOC), ('tokhc', ['--vcc-tokhash'], {}),
                 ('layout', ['--vcc-layout-checks'], NOC), ('obj', ['-c'], NOC), ('objhc', ['-c'], {})]
        for k, extra, env in kinds:
            os.makedirs(f'{d}/{k}')
            vpp_batch(bin, jobfile(tus, extra, d, out=lambda o, k=k: name(o, k)), env, j=4)
            for o, _ in tus: out[f'{k}:{o}'] = sha(name(o, k))
        p = subprocess.run([f'{bin}/lextest', 'core', f'{W}/lex-files.txt', '1'], capture_output=True, text=True)
        out['lex'] = inproc(p.stdout)[1]
        p = subprocess.run([f'{bin}/vasm', '--bench', '1'] + as_inputs(), cwd=B, capture_output=True, text=True)
        out['as'] = inproc(p.stdout)[1]
        subprocess.run([f'{bin}/vlink'] + LD_ARGS + ['-o', f'{d}/vmlinux'], cwd=B, check=True, capture_output=True)
        out['ld'] = sha(f'{d}/vmlinux')
    # the caches must not change outputs either
    for o, _ in tus:
        for a, b in (('E', 'Ehc'), ('tok', 'tokhc'), ('obj', 'objhc')):
            if out[f'{a}:{o}'] != out[f'{b}:{o}']: out[f'cachediff-{a}:{o}'] = 'differs'
    return out

def golden(base, quick):
    os.makedirs(f'{VB}/golden', exist_ok=True)
    g = f'{VB}/golden/{bin_key(base)}{"-q" if quick else ""}.json'
    if os.path.exists(g): return json.load(open(g))
    h = hashes(base, quick)
    tmp = f'{g}.{os.getpid()}'
    json.dump(h, open(tmp, 'w'), indent=0, sort_keys=True); os.rename(tmp, g)
    return h

def cmd_check(bin, base, quick):
    t0 = time.time()
    ref = golden(base, quick) if base else None
    new = hashes(bin, quick)
    bad = sorted(k for k in set(new) | set(ref or {}) if k.startswith('cachediff') or (ref and ref.get(k) != new.get(k)))
    for k in bad[:30]: print(f'DIFF {k}: base {ref.get(k) if ref else "-"} new {new.get(k)}')
    print(f'check: {len(new) - len(bad)}/{len(new)} identical{"" if ref else " (no --base: only cache consistency)"}'
          f' ({time.time() - t0:.0f} s)')
    return 1 if bad else 0

# ---------------- base build ----------------

def tree_dir(sha_):
    """the compiler's directory at a revision: src/, or ccs/ before doc/PLAN-tree.md T1"""
    p = subprocess.run(['git', '-C', R, 'cat-file', '-e', f'{sha_}:src/Makefile'], capture_output=True)
    return 'src' if p.returncode == 0 else 'ccs'

def cmd_mkbase(rev, quiet=False):
    if not rev:
        rev = subprocess.run(['git', '-C', R, 'merge-base', 'HEAD', 'main'], capture_output=True, text=True).stdout.strip()
    sha_ = subprocess.run(['git', '-C', R, 'rev-parse', rev], capture_output=True, text=True, check=True).stdout.strip()
    d = f'{VB}/base/{sha_[:12]}'
    os.makedirs(f'{VB}/base', exist_ok=True)
    # built in place (the binaries hold src/headers' absolute path), under a
    # lock directory so agents asking for the same base at once build it once
    while not os.path.exists(f'{d}/ok'):
        try: os.mkdir(f'{d}.lock')
        except FileExistsError: time.sleep(1); continue
        try:
            shutil.rmtree(d, ignore_errors=True); os.makedirs(d)
            top = tree_dir(sha_)
            a = subprocess.run(['git', '-C', R, 'archive', sha_, top], capture_output=True, check=True).stdout
            subprocess.run(['tar', '-x', '-C', d], input=a, check=True)
            p = subprocess.run(['make', '-C', f'{d}/{top}', '-j', '6', 'vcc'], capture_output=True, text=True)
            if p.returncode: die(f'base build failed:\n{p.stdout[-1500:]}{p.stderr[-1500:]}')
            open(f'{d}/ok', 'w').close()
        finally: os.rmdir(f'{d}.lock')
    top = tree_dir(sha_)	# the Makefile's OUT is build/<top>
    out = f'{d}/build/linux/{top}' if sys.platform.startswith('linux') else f'{d}/build/{top}'
    if not quiet: print(out)
    return out

# ---------------- corpus selection ----------------

def cmd_pick(tsv):
    src = {a[-1]: o for o, a in vd.tus(None) if not o.startswith('scripts/')}
    rows = []
    for l in open(tsv):
        f = l.rstrip('\n').split('\t')
        if f[7] in src: rows.append(dict(o=src[f[7]], ins=int(f[2]), cc=int(f[4]), em=int(f[5])))
    rows.sort(key=lambda r: r['ins'])
    n = len(rows)
    pick, quick = [], set()
    for i in range(16):	# quantile midpoints of the instruction distribution
        r = rows[min(n - 1, int((i + 0.5) * n / 16))]
        pick.append((r['o'], 'q%02d' % i))
        if i % 2: quick.add(r['o'])
    for key, tag, k in (('em', 'emit-heavy', 4), ('cc', 'parse-heavy', 4)):
        val = (lambda r: r['em']) if key == 'em' else (lambda r: r['cc'] - r['em'])
        for r in sorted(rows, key=lambda r: -val(r)):
            if k and r['o'] not in dict(pick):
                pick.append((r['o'], tag)); k -= 1
                if tag == 'emit-heavy' and k == 3: quick.add(r['o'])
    tot = sum(r['ins'] for r in rows) / n
    sel = [r for r in rows if r['o'] in dict(pick)]
    print(f'# vbench corpus: {len(pick)} kernel TUs from {os.path.basename(tsv)}; mean {tot / 1e6:.0f} M instructions/TU '
          f'over all {n}, {sum(r["ins"] for r in sel) / len(sel) / 1e6:.0f} M here')
    print('# OBJ  [quick]  # why')
    for o, why in pick: print(f'{o}{"  quick" if o in quick else ""}  # {why}')

if __name__ == '__main__':
    av = sys.argv[1:]
    if not av: print(__doc__); sys.exit(0)
    def opt(name, default=None, flag=False):
        if name not in av: return default
        k = av.index(name)
        if flag: del av[k]; return True
        v = av[k + 1]; del av[k:k + 2]; return v
    quick = opt('--quick', False, True); show_tu = opt('--per-tu', False, True)
    reps = int(opt('--reps', '3')); bin = os.path.abspath(opt('--bin', f'{R}/build/src'))
    base = opt('--base'); base = os.path.abspath(base) if base else None
    budget = float(opt('--budget', '1'))
    cmd, rest = av[0], av[1:]
    modes = rest or (GATE_MODES if cmd == 'gate' else MODES)
    if cmd in ('run', 'ab', 'gate'):
        bad = [m for m in modes if m not in MODES]
        if bad: die(f'unknown modes {bad}; have {MODES}')
    if cmd == 'run': cmd_run(modes, bin, quick, reps)
    elif cmd == 'ab':
        if not base: die('ab needs --base DIR (vbench.py mkbase)')
        cmd_ab(modes, bin, base, quick, max(reps, 2), show_tu)
    elif cmd == 'gate': sys.exit(cmd_gate(modes, bin, base, quick, max(reps, 2), budget))
    elif cmd == 'check': sys.exit(cmd_check(bin, base, quick))
    elif cmd == 'mkbase': cmd_mkbase(rest[0] if rest else None)
    elif cmd == 'pick': cmd_pick(rest[0])
    else: die(f'unknown command {cmd}')
