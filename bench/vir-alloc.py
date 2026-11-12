#!/usr/bin/env python3
"""
VIR register allocation (PLAN-vir.md V4, SPEC-vir.md 15): build/ccs/viralloc
on the tests and on random functions, every allocation checked by
vir_alloc_check.

  bench/vir-alloc.py [test] [-j N] [-v] [PATTERN]   ccs/vcc/tests/vir-alloc/*.vir: output equals the
                                                    expected part, every register count checked (; SWEEP)
  bench/vir-alloc.py --bless PATTERN                rewrite the expected part of the matching tests
  bench/vir-alloc.py random [-n N] [--seed S] [-j N] [--keep DIR]
                                                    N random units of 8 functions each (arbitrary control
                                                    flow: loops, irreducible cycles, ibr, block arguments,
                                                    calls with one or two results, memory); each allocated
                                                    at every register count from a random maximum down to
                                                    the minimum, with and without x86 constraints, also in
                                                    linear mode; failing units are kept in DIR
  bench/vir-alloc.py mutate [-n K]                  the checker catches corrupted allocations
  bench/vir-alloc.py time                           ns per VIR instruction on large generated functions
  --bin-dir DIR (or VCC_BIN): another build (build/ccs-asan)
"""
import os, random, re, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
D = f'{R}/ccs/vcc/tests/vir-alloc'


def arg(name, default=None, conv=str):
    if name in sys.argv:
        k = sys.argv.index(name); v = sys.argv[k + 1]; del sys.argv[k:k + 2]; return conv(v)
    return default


BIN = os.path.abspath(arg('--bin-dir', os.environ.get('VCC_BIN', f'{R}/build/ccs')))
VA = os.path.join(BIN, 'viralloc')
ENV = dict(os.environ, UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1', ASAN_OPTIONS='detect_leaks=0')


def run(args, timeout=300):
    p = subprocess.run([VA] + args, capture_output=True, text=True, env=ENV, timeout=timeout)
    return p.returncode, p.stdout, p.stderr


# ---------------- the tests ----------------

def tests(pat):
    return sorted(f for f in os.listdir(D) if f.endswith('.vir') and (not pat or pat in f))


def do_test(jobs, verbose, pat, bless):
    names = tests(pat)

    def one(n):
        rc, out, err = run((['--bless'] if bless else []) + [f'{D}/{n}'])
        return n, rc, out, err
    bad = 0
    with ThreadPoolExecutor(jobs) as ex:
        for n, rc, out, err in ex.map(one, names):
            if rc:
                bad += 1
                print(f'FAIL {n}: {err.strip() or "exit " + str(rc)}')
                if verbose:
                    print(out)
    print(f'vir-alloc: {len(names) - bad}/{len(names)} passed')
    return 1 if bad else 0


# ---------------- random functions ----------------

INT_OPS = ['add', 'sub', 'mul', 'and', 'or', 'xor', 'shl', 'lshr', 'ashr', 'sdiv', 'srem', 'udiv']
F_OPS = ['fadd', 'fsub', 'fmul', 'fdiv']
HDR = '''vir 1
target x86_64-linux
extern @g
extern @h
extern @m
sig %g = (int, int) -> int
sig %h = (f64, int) -> f64
sig %m = (int, f64) -> int:f64
sig %v = () -> none
'''


def dominators(n, succ):
    pred = [[] for _ in range(n)]
    for b in range(n):
        for s in succ[b]:
            pred[s].append(b)
    order, seen = [], set()

    def dfs(b):  # iterative postorder
        st = [(b, iter(succ[b]))]
        seen.add(b)
        while st:
            x, it = st[-1]
            for s in it:
                if s not in seen:
                    seen.add(s); st.append((s, iter(succ[s]))); break
            else:
                order.append(x); st.pop()
    dfs(0)
    rpo = order[::-1]
    num = {b: i for i, b in enumerate(rpo)}
    idom = {0: 0}
    ch = True
    while ch:
        ch = False
        for b in rpo[1:]:
            ps = [p for p in pred[b] if p in idom]
            new = ps[0]
            for p in ps[1:]:
                a, c = p, new
                while a != c:
                    while num[a] > num[c]: a = idom[a]
                    while num[c] > num[a]: c = idom[c]
                new = a
            if idom.get(b) != new:
                idom[b] = new; ch = True
    return rpo, idom, pred


def doms_of(b, idom):
    out = [b]
    while b != 0:
        b = idom[b]; out.append(b)
    return out


def gen_fn(rnd, name):
    n = rnd.randint(1, 14)
    # a spanning tree from block 0 (each block's parent is earlier and has at most two
    # children) makes every block reachable; extra edges add loops, irreducible cycles
    # (an edge into the middle of a cycle) and joins
    kids = [[] for _ in range(n)]
    for b in range(1, n):
        p = rnd.choice([x for x in range(b) if len(kids[x]) < 2])
        kids[p].append(b)
    addr = set(b for b in range(1, n) if rnd.random() < 0.12)
    kinds, succ = [], []
    for b in range(n):
        k = kids[b]
        extra = rnd.randrange(1, n) if n > 1 and rnd.random() < 0.4 else (min(n - 1, b + 1 + rnd.randrange(3)) if n > 1 else 0)
        if len(k) == 2:
            kinds.append('brif'); succ.append(k[:])
        elif len(k) == 1:
            if rnd.random() < 0.5:
                kinds.append('br'); succ.append(k[:])
            else:
                kinds.append('brif'); succ.append([k[0], extra] if rnd.random() < 0.5 else [extra, k[0]])
        else:
            r = rnd.random()
            if addr and r < 0.25:
                kinds.append('ibr'); succ.append(sorted(addr))
            elif n > 1 and r < 0.55:
                kinds.append('br'); succ.append([extra])
            elif n > 1 and r < 0.75:
                kinds.append('brif'); succ.append([extra, rnd.randrange(1, n)])
            else:
                kinds.append('ret'); succ.append([])
    rpo, idom, pred = dominators(n, succ)
    if addr and not any(k == 'ibr' for k in kinds):
        addr = set()  # an addr block must be listed by every ibr; none exists, drop the flag
    # br targets with only br predecessors may have parameters
    brpreds_only = {b: b != 0 and b not in addr and all(kinds[p] == 'br' for p in pred[b]) for b in range(n)}
    params = {b: [rnd.choice(['i64', 'i64', 'f64', 'i32']) for _ in range(rnd.randint(0, 3))] if brpreds_only[b] else []
              for b in range(n)}
    vid = [0]
    defs = {b: [] for b in range(n)}   # (name, type) defined in block b, in order
    body = {b: [] for b in range(n)}

    def new(t, b):
        vid[0] += 1
        v = f'v{vid[0]}'
        defs[b].append((v, t))
        return v

    npar = rnd.randint(0, 4)
    ptypes = [rnd.choice(['int', 'int', 'f64']) for _ in range(npar)]
    sig = '%p' + name
    sigline = f'sig {sig} = ({", ".join(ptypes)}) -> int\n'
    pnames = {}
    for b in rpo:
        avail = [x for d in doms_of(b, idom)[1:] for x in defs[d]]
        pn = [new(t, b) for t in params[b]]
        pnames[b] = pn
        lines = body[b]
        if b == 0:
            for i, t in enumerate(ptypes):
                v = new('i64' if t == 'int' else 'f64', b)
                lines.append(f'{v}:{"i64" if t == "int" else "f64"} = param {i}.0')

        def pick(t, allow_const=True):
            c = [v for v, tt in avail + defs[b] if tt == t]
            if not c or (allow_const and rnd.random() < 0.15):
                if t == 'i64': return f'{rnd.randint(-5, 300)}:i64'
                if t == 'i32': return f'{rnd.randint(0, 9)}:i32'
                if t == 'f64': return f'0x1.{rnd.randrange(16):x}p+{rnd.randrange(5)}:f64'
            return rnd.choice(c) if c else None
        for _ in range(rnd.randint(1, 14)):
            r = rnd.random()
            if r < 0.45:
                op = rnd.choice(INT_OPS)
                a, c = pick('i64', False) or '1:i64', pick('i64')
                if a.endswith(':i64') and c.endswith(':i64'):
                    continue
                v = new('i64', b); lines.append(f'{v}:i64 = {op} {a}, {c}')
            elif r < 0.55:
                a, c = pick('i64', False), pick('i64')
                if not a: continue
                v = new('i32', b); lines.append(f'{v}:i32 = icmp.{rnd.choice(["slt", "eq", "ne", "ugt"])} {a}, {c}')
            elif r < 0.65:
                a, c = pick('f64', False), pick('f64')
                if not a: continue
                v = new('f64', b); lines.append(f'{v}:f64 = {rnd.choice(F_OPS)} {a}, {c}')
            elif r < 0.7:
                a = pick('i64', False)
                if not a: continue
                v = new('f64', b); lines.append(f'{v}:f64 = s2f {a}')
            elif r < 0.75:
                cnd, x, y = pick('i32', False), pick('i64', False), pick('i64')
                if not cnd or not x: continue
                v = new('i64', b); lines.append(f'{v}:i64 = select {cnd}, {x}, {y}')
            elif r < 0.8:
                v = new('i64', b); lines.append(f'{v}:i64 = load.8 $0')
            elif r < 0.84:
                x = pick('i64')
                lines.append(f'store.8 $0, {x}')
            elif r < 0.91:
                x, y = pick('i64'), pick('i64')
                v = new('i64', b); lines.append(f'{v}:i64 = call @g %g ({x}, {y})')
            elif r < 0.95:
                x, y = pick('f64'), pick('i64')
                v = new('f64', b); lines.append(f'{v}:f64 = call @h %h ({x}, {y})')
            else:
                x, y = pick('i64'), pick('f64')
                vid[0] += 1; mv = f'v{vid[0]}'
                lines.append(f'{mv}:multi = call @m %m ({x}, {y})')
                p0 = new('i64', b); p1 = new('f64', b)
                lines.append(f'{p0}:i64 = proj {mv}, 0')
                lines.append(f'{p1}:f64 = proj {mv}, 1')
        # the terminator
        k = kinds[b]
        if k == 'ret':
            lines.append(f'ret {pick("i64")}')
        elif k == 'br':
            t = succ[b][0]
            args = []
            for ty in params[t]:
                args.append(pick(ty))
            lines.append(f'br b{t}' + (f'({", ".join(args)})' if args else ''))
        elif k == 'brif':
            cnd = pick('i32', False)
            if not cnd:
                x = pick('i64', False) or None
                if x is None:
                    cnd = new('i32', b); lines.append(f'{cnd}:i32 = icmp.eq 1:i64, 2:i64')
                else:
                    cnd = new('i32', b); lines.append(f'{cnd}:i32 = icmp.ne {x}, 0:i64')
            lines.append(f'brif {cnd}, b{succ[b][0]}, b{succ[b][1]}')
        else:
            targets = sorted(addr)
            cnd = new('i32', b); lines.append(f'{cnd}:i32 = icmp.eq 1:i64, 2:i64')
            p = new('ptr', b)
            lines.append(f'{p}:ptr = select {cnd}, &b{rnd.choice(targets)}, &b{rnd.choice(targets)}')
            lines.append(f'ibr {p} [{", ".join("b" + str(x) for x in targets)}]')
    out = [f'func @{name} {sig} {{', '  slot $0 size 8 align 8']
    for b in range(n):
        ps = ', '.join(f'{v}:{t}' for v, t in zip(pnames[b], params[b]))
        out.append(f'b{b}' + (f'({ps})' if ps else '') + (' addr' if b in addr else '') + ':')
        out += ['  ' + l for l in body[b]]
    out.append('}')
    return sigline, '\n'.join(out) + '\n'


def gen_unit(seed, nfn=8):
    rnd = random.Random(seed)
    sigs, fns = [], []
    for i in range(nfn):
        s, f = gen_fn(rnd, f'f{i}')
        sigs.append(s); fns.append(f)
    return HDR + ''.join(sigs) + '\n'.join(fns)


def do_random(n, seed, jobs, keep):
    os.makedirs(keep, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix='vir-alloc-')

    def one(i):
        s = seed + i
        rnd = random.Random(s * 7919)
        p = f'{tmp}/r{s}.vir'
        open(p, 'w').write(gen_unit(s))
        regs = f'{rnd.randint(3, 10)},{rnd.randint(2, 6)},0,0'
        rc, out, err = run(['--quiet', '--sweep', '--regs', regs, p])
        if rc:
            k = f'{keep}/r{s}.vir'
            os.replace(p, k)
            return s, regs, (out + err).strip().splitlines()[:6], k
        os.unlink(p)
        return None
    bad = 0
    with ThreadPoolExecutor(jobs) as ex:
        for r in ex.map(one, range(n)):
            if r:
                bad += 1
                s, regs, lines, k = r
                print(f'FAIL seed {s} regs {regs}: {k}')
                for l in lines:
                    print('   ', l)
    print(f'vir-alloc random: {n - bad}/{n} units ({n * 8} functions) passed, seeds {seed}..{seed + n - 1}')
    return 1 if bad else 0


def do_mutate(k):
    tot = caught = 0
    for t in tests(None):
        rc, out, err = run(['--regs', '4,2,1,0', '--mutate', str(k), f'{D}/{t}'])
        m = re.search(r'mutate @\S+: (\d+) mutations, (\d+) caught', out)
        if m:
            tot += int(m.group(1)); caught += int(m.group(2))
    print(f'vir-alloc mutate: the checker caught {caught} of {tot} corrupted allocations')
    return 0


# ---------------- timing ----------------

def big_interp(nops, seed, nlive=24):
    """an interpreter loop shaped like sqlite3VdbeExec: a dispatch block, nops
    handlers that each read and update some of nlive loop-carried registers,
    call out now and then, and branch back"""
    rnd = random.Random(seed)
    L = [HDR + 'sig %i = (int, int) -> int\nfunc @interp %i {', '  slot $0 size 8 align 8', 'b0:',
         '  v0:i64 = param 0.0', '  v1:i64 = param 1.0']
    st = [f'{i}:i64' for i in range(nlive)]
    L.append(f'  br b1(0:i64, {", ".join(st)})')
    hdr = ', '.join(f'h{i}:i64' for i in range(nlive))
    L.append(f'b1(pc:i64, {hdr}):')
    L.append('  op:i64 = and pc, 255')
    L.append(f'  t0:i32 = icmp.ult op, {nops}')
    # a binary dispatch tree over the handlers (brif only, like the walker's switch)
    nb = [2]
    tree = []

    def node(lo, hi, cond_src):
        if hi - lo == 1:
            return f'h_{lo}'
        mid = (lo + hi) // 2
        me = f'd{lo}_{hi}'
        tree.append((me, lo, mid, hi))
        return me
    root = node(0, nops, None)
    L.append(f'  brif t0, {"b_" + root if not root.startswith("h_") else root}, bexit')
    blocks = []
    i = 0
    while i < len(tree):
        me, lo, mid, hi = tree[i]
        l, r = node(lo, mid, None), node(mid, hi, None)
        blocks.append((me, [f'  c{me}:i32 = icmp.ult op, {mid}', f'  brif c{me}, b_{l}, b_{r}']))
        i += 1
    vid = 0
    for h in range(nops):
        body = []
        cur = {j: f'h{j}' for j in range(nlive)}
        for _ in range(rnd.randint(3, 12)):
            vid += 1
            a, c = rnd.sample(range(nlive), 2)
            op = rnd.choice(['add', 'sub', 'xor', 'mul', 'and', 'or', 'shl'])
            v = f'x{vid}'
            if rnd.random() < 0.1:
                body.append(f'  {v}:i64 = call @g %g ({cur[a]}, {cur[c]})')
            elif rnd.random() < 0.1:
                body.append(f'  {v}:i64 = load.8 $0')
            else:
                body.append(f'  {v}:i64 = {op} {cur[a]}, {cur[c]}')
            cur[rnd.randrange(nlive)] = v
        vid += 1
        body.append(f'  np{h}:i64 = add pc, {rnd.randint(1, 5)}')
        body.append(f'  br b1(np{h}, {", ".join(cur[j] for j in range(nlive))})')
        blocks.append((f'h_{h}', body))
    for name, body in blocks:
        L.append(f'b_{name}:' if not name.startswith('b_') else f'{name}:')
        L += body
    L.append('bexit:')
    L.append('  ret h0')
    L.append('}')
    text = '\n'.join(L) + '\n'
    # block names: the parser wants bN; number them in order of appearance
    names = re.findall(r'^(b[\w.]+)(?:\(|:)', text, re.M)
    m = {n: f'b{i}' for i, n in enumerate(names)}
    text = re.sub(r'\bb_?[A-Za-z_][\w.]*|\bb\d+\b|\bbexit\b', lambda x: m.get(x.group(0), m.get('b_' + x.group(0), x.group(0))), text)
    # value names: vN
    vals = []
    for mm in re.finditer(r'\b([a-z][\w.]*):(?:i64|i32|f64|ptr|multi)\b', text):
        if mm.group(1) not in vals and mm.group(1) not in ('int', 'none', 'mem', 'sret'):
            vals.append(mm.group(1))
    vm = {n: f'v{i}' for i, n in enumerate(vals)}
    text = re.sub(r'(?<![@%$&\w])([a-z][\w.]*)\b(?![(])', lambda x: vm.get(x.group(1), x.group(1)), text)
    return text


def do_time():
    tmp = tempfile.mkdtemp(prefix='vir-alloc-t-')
    cases = [('interp-256', big_interp(256, 1)), ('interp-2048', big_interp(2048, 2)),
             ('random', gen_unit(4242, 40))]
    for name, text in cases:
        p = f'{tmp}/{name}.vir'
        open(p, 'w').write(text)
        for regs in ['14,14,5,0', '6,4,2,0']:
            rc, out, err = run(['--quiet', '--regs', regs, '--time', '20', p], timeout=600)
            if rc:
                print(f'{name} {regs}: failed\n{out}{err}')
                continue
            for l in err.strip().splitlines()[-1:] if name != 'random' else err.strip().splitlines():
                print(f'{name:12} regs {regs:10} {l}')
    return 0


def main():
    jobs = arg('-j', 6, int)
    n = arg('-n', None, int)
    seed = arg('--seed', 1, int)
    keep = arg('--keep', f'{R}/build/ccs/vir-alloc-fail')
    bless = '--bless' in sys.argv and not sys.argv.remove('--bless')
    verbose = '-v' in sys.argv and not sys.argv.remove('-v')
    rest = sys.argv[1:]
    mode = rest.pop(0) if rest and rest[0] in ('test', 'random', 'mutate', 'time') else 'test'
    if mode == 'random':
        return do_random(n or 200, seed, jobs, keep)
    if mode == 'mutate':
        return do_mutate(n or 300)
    if mode == 'time':
        return do_time()
    return do_test(jobs, verbose, rest[0] if rest else None, bless)


if __name__ == '__main__':
    sys.exit(main())
