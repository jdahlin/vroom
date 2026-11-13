#!/usr/bin/env python3
"""
Mutation corpus for diagnostics (PLAN-diag.md D0.2, test layer 3).

Takes valid C TUs that both clang and vcc accept, injects one error into
the main file (a deleted, duplicated, inserted or replaced token: see
MUTATORS), runs `-fsyntax-only` with both compilers and measures:

  - vcc crashes (signal) or times out;
  - missed: clang rejects the mutant, vcc accepts it;
  - spurious: clang accepts it (the mutation happened to be valid C), vcc rejects it;
  - first error: at clang's place (same line and column), same line, or elsewhere;
  - follow-on errors (errors after the first): vcc fewer / the same / more than clang.

Sources: `torture` (tests/gcc-torture/execute, tests/tcc-tests2, tests/vcc,
with vcc-ctests.py's flags) and `kernel` (work/build-o0 TUs, as vcc-sema.py).
The seed is fixed, so a run is deterministic; clang's results are cached in
build/src/mutate/clang/ (keyed by the mutant and the flags), so a rerun only
runs vcc.  Details of every mutant go to build/src/mutate/last.jsonl.

  bench/vcc-mutate.py [-j N] [-n N] [--kernel-share F] [--seed S] [--src torture|kernel|all] [--formats]
      --formats: also check every mutant's JSON records rebuild its text, its SARIF log and its HTML page (layer 5a)
      --fix: also run `vpp --fix` (up to 3 passes) on each rejected mutant: how often a machine-applicable
             fix restores the original, and that none leaves more errors than before (layer 5b, D6)
  bench/vcc-mutate.py show CATEGORY [KIND] [-n N]   mutants of a category from the last run, with both outputs
      CATEGORY: crash timeout missed spurious firstloc firstline more fewer

-n: mutants (default 2000), --kernel-share: the part of them from kernel TUs (0.25).
"""
import hashlib, json, os, random, re, subprocess, sys, tempfile, time
from collections import Counter, defaultdict
from multiprocessing import Pool
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hostenv import llvm, BUILD
import importlib.util

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
T = f'{R}/src/tests'
CL = llvm('clang')
VPP = os.path.join(os.environ.get('VCC_BIN') or BUILD, 'vpp')
OUT = f'{BUILD}/mutate'
CACHE = f'{OUT}/clang'
RES = subprocess.run([CL, '-print-resource-dir'], capture_output=True, text=True).stdout.strip() + '/include'
TORTURE_FL = ['--target=aarch64-linux-gnu', '-nostdinc', '-I', f'{T}/rt/include', '-isystem', RES, '-w',
              '-Wno-error=implicit-function-declaration', '-Wno-error=implicit-int', '-Wno-error=int-conversion',
              '-Wno-error=incompatible-pointer-types', '-Wno-error=return-type',
              '-Wno-error=incompatible-function-pointer-types']
COMMON = ['-fsyntax-only', '-fno-color-diagnostics', '-ferror-limit=0']
TIMEOUT = 20

# ---------------- tokens of the main file ----------------

TOK = re.compile(r'''
    (?P<ws>\s+) | (?P<com>//[^\n]*|/\*.*?\*/) |
    (?P<str>[LuU]?8?"(?:\\.|[^"\\\n])*"|[LuU]?'(?:\\.|[^'\\\n])*') |
    (?P<num>\.?\d(?:[eEpP][+-]|[\w.])*) | (?P<id>[A-Za-z_]\w*) |
    (?P<punct>\.\.\.|<<=|>>=|->|\+\+|--|<<|>>|<=|>=|==|!=|&&|\|\||[-+*/%&|^!=<>]=|\#\#|[][(){}.,;:?~+\-*/%&|^!=<>\#])
''', re.S | re.X)
KEYWORDS = set('auto break case char const continue default do double else enum extern float for goto if inline int '
               'long register restrict return short signed sizeof static struct switch typedef union unsigned void '
               'volatile while _Bool _Complex _Atomic _Alignas _Alignof _Generic _Noreturn _Static_assert '
               '_Thread_local typeof __typeof__ __attribute__ __asm__ asm __extension__ __inline__ __restrict__ '
               '__volatile__ __signed__ __const__ __inline __restrict __volatile __alignof__ __label__'.split())
TYPES = ['int', 'char', 'long', 'short', 'unsigned', 'double', 'float', 'void']


def tokens(src):
    """[(start, end, kind, text)] outside preprocessor lines and comments"""
    out, i, n = [], 0, len(src)
    bol = True
    while i < n:
        if bol:
            j = i
            while j < n and src[j] in ' \t':
                j += 1
            if j < n and src[j] == '#':		# a directive: skip to its end (with continuations)
                while j < n and src[j] != '\n':
                    j += 2 if src[j] == '\\' and j + 1 < n and src[j + 1] == '\n' else 1
                i = j
                continue
        m = TOK.match(src, i)
        if not m:
            i += 1
            bol = False
            continue
        k = m.lastgroup
        if k == 'ws' or k == 'com':
            bol = bol or '\n' in m.group()
        else:
            out.append((m.start(), m.end(), k, m.group()))
            bol = False
        i = m.end()
    return out

# ---------------- mutators ----------------
# each: (rng, src, toks) -> new src or None (not applicable)


def _pick(rng, toks, pred):
    c = [t for t in toks if pred(t)]
    return rng.choice(c) if c else None


def _repl(src, t, s):
    return src[:t[0]] + s + src[t[1]:]


def m_del_semi(rng, src, toks):
    t = _pick(rng, toks, lambda t: t[3] == ';')
    return t and _repl(src, t, '')


def m_del_close(rng, src, toks):
    t = _pick(rng, toks, lambda t: t[3] in ')]}')
    return t and _repl(src, t, '')


def m_del_open(rng, src, toks):
    t = _pick(rng, toks, lambda t: t[3] in '([{')
    return t and _repl(src, t, '')


def m_del_token(rng, src, toks):
    t = _pick(rng, toks, lambda t: True)
    return t and _repl(src, t, '')


def m_dup_token(rng, src, toks):
    t = _pick(rng, toks, lambda t: True)
    return t and src[:t[1]] + ' ' + t[3] + src[t[1]:]


def m_semi_comma(rng, src, toks):
    t = _pick(rng, toks, lambda t: t[3] in ';,')
    return t and _repl(src, t, ',' if t[3] == ';' else ';')


def m_typo_ident(rng, src, toks):
    """an identifier misspelled: undeclared, or a typo clang may correct"""
    t = _pick(rng, toks, lambda t: t[2] == 'id' and t[3] not in KEYWORDS and len(t[3]) > 2)
    if not t:
        return None
    s = t[3]
    k = rng.randrange(len(s) - 1)
    return _repl(src, t, s[:k] + s[k + 1] + s[k] + s[k + 2:] if s[k] != s[k + 1] else s + 'x')


def m_typo_keyword(rng, src, toks):
    t = _pick(rng, toks, lambda t: t[3] in KEYWORDS and len(t[3]) > 2 and not t[3].startswith('_'))
    return t and _repl(src, t, t[3][:-1])


def m_change_type(rng, src, toks):
    t = _pick(rng, toks, lambda t: t[3] in TYPES)
    return t and _repl(src, t, rng.choice(['struct zz', 'void', 'double', 'int *', 'struct { int q; }']))


def m_insert(rng, src, toks):
    t = _pick(rng, toks, lambda t: True)
    return t and src[:t[0]] + rng.choice([')', '(', ']', '}', 'int ', '; ', ', ', '= ', '* ', '0 ', 'x ']) + src[t[0]:]


def m_operator(rng, src, toks):
    t = _pick(rng, toks, lambda t: t[2] == 'punct' and t[3] in ('+', '-', '*', '/', '==', '<', '>', '&&', '||', '=',
                                                                 '.', '->'))
    return t and _repl(src, t, rng.choice(['.', '->', '=', '+=', '?', ':', '##', '@']))


MUTATORS = {
    'del-semi': m_del_semi, 'del-close': m_del_close, 'del-open': m_del_open, 'del-token': m_del_token,
    'dup-token': m_dup_token, 'semi-comma': m_semi_comma, 'typo-ident': m_typo_ident,
    'typo-keyword': m_typo_keyword, 'change-type': m_change_type, 'insert': m_insert, 'operator': m_operator,
}

# ---------------- sources ----------------


def torture_tus():
    out = []
    for d in ('gcc-torture/execute', 'tcc-tests2', 'vcc'):
        for f in sorted(os.listdir(f'{T}/{d}')):
            if f.endswith('.c'):
                out.append(('torture', f'{d}/{f}', f'{T}/{d}/{f}', TORTURE_FL, f'{T}/{d}'))
    return out


def kernel_tus():
    spec = importlib.util.spec_from_file_location('vd', f'{R}/bench/vcc-diff.py')
    vd = importlib.util.module_from_spec(spec)
    saved = sys.argv
    sys.argv = [sys.argv[0]]
    spec.loader.exec_module(vd)
    sys.argv = saved
    if not os.path.isdir(vd.B):
        return []
    out = []
    for obj, a in vd.tus():
        if obj.startswith('scripts/') or not a[-1].endswith('.c') or '-E' in a:
            continue
        a = [x for x in a if not x.startswith('-Wp,')]
        path = a[-1] if os.path.isabs(a[-1]) else os.path.normpath(f'{vd.B}/{a[-1]}')
        out.append(('kernel', obj, path, a[:-1], vd.B))
    return out

# ---------------- running ----------------

ELINE = re.compile(r'^(.*?):(\d+):(\d+): (error|fatal error): (.*)$', re.M)


IDRX = re.compile(r' \[([a-z][a-z0-9_]*)\]$')


def compile1(cc, args, path, cwd, ids=False):
    """ids: vcc with -fdiagnostics-show-id: each error's id is taken off its message (PLAN-diag layer 2)"""
    t0 = time.time()
    try:
        p = subprocess.run([cc] + COMMON + (['-fdiagnostics-show-id'] if ids else []) + args + [path], cwd=cwd, capture_output=True, text=True, errors='replace',
                           timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        return {'rc': 'timeout', 'errs': [], 'out': '', 't': TIMEOUT}
    errs = [[os.path.basename(m.group(1)), int(m.group(2)), int(m.group(3)), m.group(5)] for m in ELINE.finditer(p.stderr)]	# lists: as from the JSON cache
    errs = [e for e in errs if not e[3].startswith('too many errors emitted')]
    r = {'rc': p.returncode, 'errs': errs, 'out': p.stderr[-6000:], 't': time.time() - t0}
    if ids:
        r['ids'] = []
        for e in errs:
            m = IDRX.search(e[3])
            r['ids'].append(m.group(1) if m and m.group(1) != 'vcc_unknown' else None)
            if m:
                e[3] = e[3][:m.start()]
        r['out'] = IDRX.sub('', r['out'])
    return r


def clang_cached(args, path, src, cwd):
    key = hashlib.sha256(json.dumps([CL, args, src, os.path.basename(path)]).encode()).hexdigest()
    f = f'{CACHE}/{key[:2]}/{key}.json'
    try:
        return json.load(open(f))
    except (OSError, ValueError):
        pass
    r = compile1(CL, args, path, cwd)
    if r['rc'] != 'timeout':
        os.makedirs(os.path.dirname(f), exist_ok=True)
        json.dump(r, open(f, 'w'))
    return r


def formats_check(a, mp, cwd):
    """layer 5a and D3.2 on a mutant: the JSON records rebuild the text, the SARIF log is well formed"""
    import importlib.util
    mods = {}
    for name, file in (('diagrender', 'diag-render.py'), ('sarifcheck', 'sarif-check.py')):
        spec = importlib.util.spec_from_file_location(name, os.path.join(os.path.dirname(os.path.abspath(__file__)), file))
        mods[name] = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mods[name])
    out = {}
    hp = mp + '.html'
    for k, extra in (('text', ['-fdiagnostics-add-output=html:file=' + hp]), ('json', ['-fdiagnostics-format=json']),
                     ('sarif', ['-fdiagnostics-format=sarif'])):
        p = subprocess.run([VPP] + COMMON + extra + a + [mp], cwd=cwd, capture_output=True, timeout=TIMEOUT)
        out[k] = p.stderr
    try:
        if mods['diagrender'].render(out['json'].decode(errors='replace').splitlines(), cwd or '.') != out['text']:
            return 'JSON does not rebuild the text'
        sp = mods['sarifcheck'].check(json.loads(out['sarif']))
        if sp:
            return f'SARIF: {sp[0]}'
        return html_check(hp, out['text'], out['json'])
    except (ValueError, KeyError) as e:
        return f'bad JSON: {e}'
    finally:
        if os.path.exists(hp):
            os.unlink(hp)


def html_check(hp, text, js):
    """D8.4: the HTML page is well formed, has a section per record, and its text is the text
    output's (without colour; the summary line aside)"""
    import html, html.parser
    try:
        page = open(hp, encoding='utf-8').read()
    except (OSError, UnicodeDecodeError) as e:
        return f'HTML: {e}'

    class P(html.parser.HTMLParser):
        def __init__(self):
            super().__init__()
            self.st, self.bad, self.sec, self.pre, self.cur = [], None, 0, [], None

        def handle_starttag(self, t, at):
            if t == 'meta':
                return
            self.st.append(t)
            if t == 'section':
                self.sec += 1
            if t == 'pre':
                self.cur = []

        def handle_endtag(self, t):
            if not self.st or self.st[-1] != t:
                self.bad = self.bad or f'</{t}> closes {self.st[-1:] or "nothing"}'
            else:
                self.st.pop()
            if t == 'pre' and self.cur is not None:
                self.pre.append(''.join(self.cur))
                self.cur = None

        def handle_data(self, d):
            if self.cur is not None:
                self.cur.append(d)
    p = P()
    p.feed(page)
    if p.bad or p.st:
        return f'HTML: {p.bad or "unclosed " + str(p.st)}'
    nrec = sum(1 for ln in js.decode(errors='replace').splitlines() if ln.startswith('{'))
    if p.sec != nrec:
        return f'HTML: {p.sec} sections, {nrec} records'
    t = text.decode(errors='replace')
    t = re.sub(r'\n?[^\n]*generated\.\n$', '\n', t) if t.endswith('generated.\n') else t
    if ''.join(p.pre).strip('\n') != t.strip('\n'):
        if os.environ.get('MUTATE_HTML_DUMP'):
            d = os.environ['MUTATE_HTML_DUMP']
            open(d + '.html.txt', 'w').write(''.join(p.pre))
            open(d + '.text.txt', 'w').write(t)
        return 'HTML text differs from the text output'
    return None


def fix_check(a, mp, cwd, src, nerr):
    """layer 5b on a mutant: --fix until nothing changes; restored (the original again), errors after"""
    applied = False
    for _ in range(3):
        p = subprocess.run([VPP] + COMMON + ['--fix'] + a + [mp], cwd=cwd, capture_output=True, timeout=TIMEOUT)
        if p.returncode < 0:
            return {'crash': p.returncode}
        if b'note: applied' not in p.stderr:
            break
        applied = True
    if not applied:
        return {'applied': False}
    after = compile1(VPP, a, mp, cwd)
    return {'applied': True, 'restored': open(mp, errors='replace').read() == src, 'before': nerr,
            'after': len(after['errs']) if after['rc'] != 'timeout' else -1}


def job(arg):
    """one mutant: (source tu, kind, seed) -> record"""
    tu, kind, seed, tmp, formats, fix = arg
    src_kind, name, path, args, cwd = tu
    try:
        src = open(path, errors='replace').read()
    except OSError:
        return None
    rng = random.Random(seed)
    mut = MUTATORS[kind](rng, src, tokens(src))
    if mut is None or mut == src:
        return None
    d = f'{tmp}/{seed}'
    os.makedirs(d, exist_ok=True)
    mp = f'{d}/{os.path.basename(path)}'
    open(mp, 'w').write(mut)
    a = args + ['-iquote', os.path.dirname(path)]		# "x.h" next to the original
    c = clang_cached(a, mp, mut, cwd)
    v = compile1(VPP, a, mp, cwd, ids=True)
    fmt = formats_check(a, mp, cwd) if formats else None
    fx = fix_check(a, mp, cwd, src, len(v['errs'])) if fix and v['errs'] else None
    os.unlink(mp)
    os.rmdir(d)
    rec = {'src': src_kind, 'tu': name, 'kind': kind, 'seed': seed, 'path': path, 'clang': c, 'vcc': v}
    if formats:
        rec['formats'] = fmt
    if fx:
        rec['fix'] = fx
    rec['cat'] = classify(c, v)
    # the line that was changed, for show
    ol, nl = src.splitlines(), mut.splitlines()
    i = next((i for i in range(min(len(ol), len(nl))) if ol[i] != nl[i]), min(len(ol), len(nl)))
    rec['line'] = i + 1
    rec['before'] = ol[i] if i < len(ol) else ''
    rec['after'] = nl[i] if i < len(nl) else ''
    return rec


def classify(c, v):
    cat = []
    if isinstance(v['rc'], int) and v['rc'] < 0:
        return ['crash']
    if v['rc'] == 'timeout':
        return ['timeout']
    if c['rc'] == 'timeout':
        return ['clang-timeout']
    crej, vrej = c['rc'] != 0, v['rc'] != 0
    if crej and not vrej:
        return ['missed']
    if vrej and not crej:
        return ['spurious']
    if not crej:
        return ['valid']
    if not c['errs'] or not v['errs']:
        return ['noerrline']
    ce, ve = c['errs'][0], v['errs'][0]
    if ce[:3] == ve[:3]:
        cat.append('firstok')
    elif ce[:2] == ve[:2]:
        cat.append('firstline')
    else:
        cat.append('firstloc')
    nc, nv = len(c['errs']) - 1, len(v['errs']) - 1
    cat.append('fewer' if nv < nc else 'same' if nv == nc else 'more')
    return cat


def baseline_ok(tu):
    """both compilers accept the unmodified TU"""
    src_kind, name, path, args, cwd = tu
    try:
        src = open(path, errors='replace').read()
    except OSError:
        return None
    a = args + ['-iquote', os.path.dirname(path)]
    c = clang_cached(a, path, src, cwd)
    if c['rc'] != 0:
        return None
    v = compile1(VPP, a, path, cwd)
    return tu if v['rc'] == 0 else None


def run(argv):
    j, n, share, seed, which = 6, 2000, 0.25, 1, 'all'
    formats = '--formats' in argv
    fix = '--fix' in argv
    if '-j' in argv:
        j = int(argv[argv.index('-j') + 1])
    if '-n' in argv:
        n = int(argv[argv.index('-n') + 1])
    if '--kernel-share' in argv:
        share = float(argv[argv.index('--kernel-share') + 1])
    if '--seed' in argv:
        seed = int(argv[argv.index('--seed') + 1])
    if '--src' in argv:
        which = argv[argv.index('--src') + 1]
    t0 = time.time()
    rng = random.Random(seed)
    tor = torture_tus() if which in ('all', 'torture') else []
    ker = kernel_tus() if which in ('all', 'kernel') else []
    nk = int(n * share) if tor and ker else (n if ker else 0)
    nt = n - nk
    # sample more TUs than needed: some fail the baseline
    pick_t = rng.sample(tor, min(len(tor), nt + nt // 2 + 10)) if nt else []
    pick_k = rng.sample(ker, min(len(ker), nk // 4 + 20)) if nk else []
    with Pool(j) as pool:
        good_t = [t for t in pool.map(baseline_ok, pick_t, chunksize=4) if t]
        good_k = [t for t in pool.map(baseline_ok, pick_k) if t]
    kinds = sorted(MUTATORS)
    jobs = []
    tmp = tempfile.mkdtemp(prefix='vcc-mutate-')
    for i in range(nt):
        if good_t:
            jobs.append((good_t[i % len(good_t)], kinds[i % len(kinds)], seed * 1000003 + i, tmp, formats, fix))
    for i in range(nk):
        if good_k:
            # several mutants per kernel TU: they are expensive to pick
            jobs.append((good_k[i % len(good_k)], kinds[(i * 7) % len(kinds)], seed * 1000003 + nt + i, tmp, formats, fix))
    with Pool(j) as pool:
        recs = [r for r in pool.imap_unordered(job, jobs, chunksize=2) if r]
    os.rmdir(tmp)
    recs.sort(key=lambda r: r['seed'])
    os.makedirs(OUT, exist_ok=True)
    with open(f'{OUT}/last.jsonl', 'w') as f:
        for r in recs:
            f.write(json.dumps(r) + '\n')
    report(recs, len(good_t), len(good_k), time.time() - t0)
    bad = [r for r in recs if r.get('formats')]
    if formats:
        print(f'  formats (JSON rebuilds the text, SARIF well formed, HTML has the text): {len(recs) - len(bad)}/{len(recs)} ok')
        for r in bad[:10]:
            print(f'    {r["tu"]} seed {r["seed"]}: {r["formats"]}')
    worse = []
    if fix:
        fx = [r for r in recs if r.get('fix')]
        ap = [r for r in fx if r['fix'].get('applied')]
        worse = [r for r in fx if r['fix'].get('crash') or (r['fix'].get('applied') and
                 (r['fix']['after'] < 0 or r['fix']['after'] > r['fix']['before']))]
        print(f'  --fix (layer 5b): applied to {len(ap)} of {len(fx)} rejected mutants, restored the original '
              f'{sum(1 for r in ap if r["fix"]["restored"])}, no error left '
              f'{sum(1 for r in ap if r["fix"]["after"] == 0)}, more errors than before {len(worse)}')
        per = Counter(r['kind'] for r in ap if r['fix']['restored'])
        print('    restored per kind: ' + ', '.join(f'{k} {per[k]}/{sum(1 for r in ap if r["kind"] == k)}'
                                                   for k in sorted({r['kind'] for r in ap})))
        for r in worse[:10]:
            print(f'    worse: {r["tu"]} seed {r["seed"]} {r["kind"]}: {r["fix"]}')
    return 1 if any(r['cat'][0] in ('crash', 'timeout') for r in recs) or bad or worse else 0


def report(recs, ntu, nktu, dt):
    print(f'{len(recs)} mutants from {ntu} torture + {nktu} kernel TUs in {dt:.1f} s')
    tot = Counter()
    by = defaultdict(Counter)
    for r in recs:
        for c in r['cat']:
            tot[c] += 1
            by[r['kind']][c] += 1
        by[r['kind']]['n'] += 1
    rej = sum(tot[c] for c in ('firstok', 'firstline', 'firstloc'))
    print(f'\n  crash {tot["crash"]}  timeout {tot["timeout"]}  missed {tot["missed"]}  spurious {tot["spurious"]}  '
          f'valid (both accept) {tot["valid"]}  both reject {rej}')
    if rej:
        print(f'  first error: at clang\'s place {tot["firstok"]} ({100 * tot["firstok"] / rej:.1f} %), '
              f'same line {tot["firstline"]} ({100 * tot["firstline"] / rej:.1f} %), '
              f'elsewhere {tot["firstloc"]} ({100 * tot["firstloc"] / rej:.1f} %)')
        print(f'  follow-on errors vs clang: fewer {tot["fewer"]} ({100 * tot["fewer"] / rej:.1f} %), '
              f'same {tot["same"]} ({100 * tot["same"] / rej:.1f} %), more {tot["more"]} ({100 * tot["more"] / rej:.1f} %)')
        nv = sum(len(r['vcc']['errs']) for r in recs if r['cat'][0].startswith('first'))
        nc = sum(len(r['clang']['errs']) for r in recs if r['cat'][0].startswith('first'))
        print(f'  errors in all: vcc {nv}, clang {nc}')
    ids = [i for r in recs for i in r['vcc'].get('ids', [])]
    if ids:
        miss = sum(1 for i in ids if i is None)
        print(f'  ids (layer 2): {len(set(i for i in ids if i))} distinct over {len(ids)} errors, {miss} without an id')
    cols = ['n', 'crash', 'missed', 'spurious', 'valid', 'firstok', 'firstline', 'firstloc', 'fewer', 'same', 'more']
    print('\n  ' + f'{"kind":14}' + ''.join(f'{c:>10}' for c in cols))
    for k in sorted(by):
        print('  ' + f'{k:14}' + ''.join(f'{by[k][c]:>10}' for c in cols))


def show(argv):
    cat = argv[1]
    kind = argv[2] if len(argv) > 2 and not argv[2].startswith('-') else None
    lim = int(argv[argv.index('-n') + 1]) if '-n' in argv else 5
    k = 0
    for l in open(f'{OUT}/last.jsonl'):
        r = json.loads(l)
        if cat not in r['cat'] or (kind and r['kind'] != kind):
            continue
        print(f'=== {r["kind"]} {r["src"]} {r["tu"]}:{r["line"]}  seed {r["seed"]}  {r["cat"]}')
        print(f'  - {r["before"].strip()[:150]}\n  + {r["after"].strip()[:150]}')
        for who in ('clang', 'vcc'):
            e = r[who]['errs']
            print(f'  {who} rc={r[who]["rc"]} errors={len(e)}')
            for x in e[:4]:
                print(f'     {x[1]}:{x[2]}: {x[3][:140]}')
        k += 1
        if k >= lim:
            break


if __name__ == '__main__':
    a = sys.argv[1:]
    if a and a[0] in ('-h', '--help'):
        print(__doc__)
        sys.exit(0)
    if a and a[0] == 'show':
        sys.exit(show(a) or 0)
    sys.exit(run(a))
