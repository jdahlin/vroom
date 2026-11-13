#!/usr/bin/env python3
"""
Source quality report for the toolchain's own code (doc/PLAN-quality.md, P0).

Reads every C source of the tree (not tests, not generated tables) once and
prints the numbers the quality work is measured by:

  size           lines, tokens, large files, long functions, per module
  duplication    code that exists twice: runs of at least --min-tokens tokens
                 that match another place token for token once identifiers
                 and string literals are ignored, and whose identifiers map
                 one to one (so a copy under other names counts, two
                 unrelated functions of the same shape do not)
  shared code    private copies of what lib/ should provide: FNV hashes,
                 realloc and mmap call sites, LEB128 encoders, static
                 functions of one name in several files
  threads        thread creation sites, CPU-count probes, locks
  state          thread-locals, file-scope mutable statics, environment knobs
  documents      files that do not open with a comment saying what they do
  dependencies   includes that cross a module boundary

It needs only the checkout (no build, no clang) and takes a few seconds.

  bench/quality.py                    the report
  bench/quality.py --clones 60        ... with the 60 longest duplicates
  bench/quality.py --clones 999 --only ld/ arch/a64/ld/
                                      ... only those with a side under a prefix
  bench/quality.py --json FILE        every number and list as JSON
  bench/quality.py --check [FILE]     exit 1 if a ratchet number is above
                                      the baseline (bench/quality-baseline.json)
  bench/quality.py --bless [FILE]     write the baseline
  bench/quality.py --generated        the files left out as generated
  bench/quality.py --root DIR         another source root (default: src/,
                                      else ccs/vcc/)
"""
import argparse, bisect, json, os, re, subprocess, sys, time
from array import array
from collections import Counter, defaultdict

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASELINE = f'{R}/bench/quality-baseline.json'

# Directories under the root that are not the toolchain's own code: tests,
# pages, vcc's own <arm_neon.h> and friends (include/ before the tree
# reorganisation, headers/ after it), generator scripts.
SKIP = {'ccs/vcc': {'tests', 'docs', 'include'},
        'src': {'test', 'headers', 'tools'}}
LIBDIRS = {'lib', 'common', 'port'}
MIN_DISTINCT = 10       # a duplicate must use this many different tokens
MIN_CONSISTENT = 0.85   # ... and map this share of its identifiers one to one
MIN_REUSE = 0.3         # ... and this share of them must be names used twice
LONG_FN = 300
BIG_FILE = 2000

KEYWORDS = set('''
auto break case char const continue default do double else enum extern float
for goto if inline int long register restrict return short signed sizeof
static struct switch typedef union unsigned void volatile while _Alignas
_Alignof _Atomic _Bool _Complex _Generic _Noreturn _Static_assert
_Thread_local __thread __attribute__ __asm__ asm typeof __typeof__ bool true
false NULL define undef ifdef ifndef endif elif pragma defined
'''.split())

TOK = re.compile(r'''[ \t\r\f\v]*(?:
  (?P<c>/\*.*?\*/|//[^\n]*)
 |(?P<s>(?:u8|[uUL])?"(?:[^"\\\n]|\\.)*")
 |(?P<q>(?:u8|[uUL])?'(?:[^'\\\n]|\\.)*')
 |(?P<n>\.?[0-9](?:[eEpP][+-]|[0-9a-zA-Z_.])*)
 |(?P<i>[A-Za-z_$][A-Za-z0-9_$]*)
 |(?P<e>\\\n)
 |(?P<l>\n)
 |(?P<p><<=|>>=|\.\.\.|->|\+\+|--|<<|>>|<=|>=|==|!=|&&|\|\||[-+*/%&|^]=|\#\#|.)
 )''', re.S | re.X)

FNV = {'16777619', '0x01000193', '0x1000193', '1099511628211', '0x100000001b3',
       '0x00000100000001b3', '2166136261', '0x811c9dc5', '0xcbf29ce484222325',
       '14695981039346656037'}
WATCH = {'realloc', 'mmap', 'getenv', 'pthread_create', 'pthread_mutex_t',
         'pthread_cond_t', '_Thread_local', '__thread', '_SC_NPROCESSORS_ONLN',
         '_SC_NPROCESSORS_CONF', 'sysctlbyname', 'switch'}
INTERNAL_HDR = re.compile(r'(impl|ccint)\.h$')
GENERATED = re.compile(r'/\*[^\n]*\bgenerated (by|from)\b', re.I)
# the comment a file opens with, after an include guard if it has one
HEAD = re.compile(r'(?:\s*#[ \t]*(?:ifndef|define|pragma)\b[^\n]*\n)*\s*'
                  r'(/\*.*?\*/|(?://[^\n]*\n[ \t]*)+)', re.S)

ID, STR = 0, 1          # codes of "an identifier" and "a string literal"


class File:
    __slots__ = ('rel', 'lines', 'code_lines', 'codes', 'spell', 'tline',
                 'funcs', 'includes', 'head', 'hits', 'statics', 'base',
                 'fnv', 'dup')


class Scan:
    """Every file tokenized once; spellings interned to small integers."""

    def __init__(self, root):
        self.root = root
        self.ids = {'<id>': ID, '<str>': STR}
        self.files = []
        self.generated = []

    def intern(self, s):
        ids = self.ids
        v = ids.get(s)
        if v is None:
            v = ids[s] = len(ids)
        return v

    def add(self, rel):
        src = open(f'{self.root}/{rel}', encoding='utf-8', errors='replace').read()
        if GENERATED.match(src):
            self.generated.append(rel)
            return
        f = File()
        f.rel = rel
        f.lines = src.count('\n')
        f.funcs, f.includes, f.statics = [], [], 0
        f.hits = defaultdict(list)
        f.fnv = False
        h = HEAD.match(src)
        f.head = h.group(1).rstrip().count('\n') + 1 if h else None
        codes, spell, tline = array('I'), array('I'), array('I')
        intern, ids = self.intern, self.ids
        line, bol, ncode, last_code_line = 1, True, 0, 0
        directive = 0       # 0 none, 1 after '#', 2 in #include, 3 other
        depth = 0           # brace depth outside directives
        fn = None           # [name, first line, static, line of its '{']
        stmt = []           # tokens of the file-scope declaration so far
        for m in TOK.finditer(src):
            k = m.lastgroup
            if k == 'l':
                line += 1
                bol = True
                if directive:
                    directive = 0
                    stmt = []
                continue
            if k == 'e':
                line += 1
                continue
            s = m.group(k)
            if k == 'c':
                line += s.count('\n')
                continue
            col0 = bol and m.start(k) == m.start()
            first = bol
            bol = False
            if line != last_code_line:
                last_code_line = line
                ncode += 1
            if directive == 2:
                if k == 's':
                    f.includes.append(s[1:-1])
                continue
            if directive == 1:
                directive = 3
                if s == 'include' or s == 'include_next':
                    directive = 2
                    codes.pop(); spell.pop(); tline.pop()
                    continue
            elif first and s == '#' and k == 'p':
                directive = 1
            pos = len(codes)
            if k == 'i':
                sid = ids.get(s)
                if sid is None:
                    sid = intern(s)
                codes.append(sid if s in KEYWORDS else ID)
                if s in WATCH:
                    f.hits[s].append(pos)
            elif k == 's':
                sid = intern(s)
                codes.append(STR)
            else:
                sid = ids.get(s)
                if sid is None:
                    sid = intern(s)
                codes.append(sid)
                if k == 'n' and s.rstrip('uUlL').lower() in FNV:
                    f.fnv = True
            spell.append(sid)
            tline.append(line)
            if directive:
                continue
            if k == 'p':
                if s == '{':
                    if col0 and fn is None and pos and stmt and stmt[-1] == ')':
                        fn = self.function_head(stmt)
                        fn.append(line)
                    depth += 1
                    stmt.append(s)
                    continue
                if s == '}':
                    depth = max(depth - 1, 0)
                    if col0 or (fn and depth == 0 and fn[3] == line):
                        depth = 0
                        if fn:
                            f.funcs.append((fn[0], fn[1], line, fn[2]))
                            fn = None
                        stmt = []
                    elif depth == 0 and fn is None and stmt[:1] != ['=']:
                        # a one-line function, or a struct body: what
                        # follows a function body is a new declaration
                        b = stmt.index('{') if '{' in stmt else 0
                        if b and stmt[b - 1] == ')':
                            stmt = []
                    continue
            if depth == 0 and fn is None:
                if s == ';' or s == '=':
                    if stmt and stmt[0] != '=' and self.mutable_static(stmt):
                        f.statics += 1
                    stmt = [] if s == ';' else ['=']
                elif not stmt:
                    stmt = [s]
                    self.stmt_line = line
                elif stmt[0] != '=':
                    stmt.append(s if k != 's' else '""')
        f.code_lines = ncode
        f.codes, f.spell, f.tline = codes, spell, tline
        self.files.append(f)

    def function_head(self, stmt):
        """[name, first line, is static] from the declarator before a body."""
        d, i = 0, len(stmt) - 1
        while i >= 0:
            if stmt[i] == ')':
                d += 1
            elif stmt[i] == '(':
                d -= 1
                if d == 0:
                    break
            i -= 1
        name = stmt[i - 1] if i > 0 else '?'
        static = 'static' in stmt[:i] or 'VPRIV' in stmt[:i]
        return [name, self.stmt_line, static]

    @staticmethod
    def mutable_static(stmt):
        return ('static' in stmt or 'VPRIV' in stmt) and '(' not in stmt \
            and 'const' not in stmt and stmt[0] not in ('typedef', 'extern')


def source_root(arg):
    if arg:
        return os.path.abspath(arg)
    return f'{R}/src' if os.path.isdir(f'{R}/src') else f'{R}/ccs/vcc'


def list_sources(root):
    key = 'src' if os.path.basename(root) == 'src' else 'ccs/vcc'
    out = []
    for d, dirs, names in os.walk(root):
        if d == root:
            dirs[:] = [x for x in dirs if x not in SKIP[key]]
        dirs.sort()
        for n in sorted(names):
            if n.endswith(('.c', '.h', '.inc')):
                out.append(os.path.relpath(f'{d}/{n}', root))
    return out


def group(rel):
    """The directory a file is counted under in the tables."""
    p = rel.split('/')
    if p[0] == 'include' and len(p) > 2:
        p = p[1:]
    n = 1
    if p[0] == 'arch':
        n = 3
    elif p[0] in ('platform', 'emit', 'codegen', 'frontend', 'bin'):
        n = 2
    return '/'.join(p[:max(1, min(n, len(p) - 1))])


def module(rel):
    """The module a file belongs to: arch/<cpu>/<component>/ is part of
    the component (it is compiled into the component's unity TU)."""
    p = rel.split('/')
    if p[0] == 'include' and len(p) > 2:
        p = p[1:]
    if p[0] == 'arch' and len(p) > 3:
        return {'emit': 'emit', 'codegen': 'codegen'}.get(p[2], p[2])
    if p[0] == 'arch':
        return '/'.join(p[:2])
    if p[0] in ('platform', 'frontend') and len(p) > 2:
        return '/'.join(p[:2])
    return p[0]


# ---------------------------------------------------------------- duplicates

def find_clones(sc, N):
    """Runs of >= N tokens that repeat an earlier place.

    Every window of N normalized tokens is hashed; a window seen before
    (in another file, or in the same file without overlap) points at its
    first occurrence.  Consecutive windows that point at consecutive places
    make a run: the later copy (dst) and the place it repeats (src).  Only
    the later copy is counted as redundant.
    """
    base = 0
    for f in sc.files:
        f.base = base
        base += len(f.codes)
    total = base
    twin = array('q', [-1]) * total
    first = {}
    for f in sc.files:
        b = f.codes.tobytes()
        w = f.codes.itemsize
        g0 = f.base
        step, span = w, w * N
        setdefault = first.setdefault
        for i in range(len(f.codes) - N + 1):
            g = g0 + i
            q = setdefault(hash(b[i * step:i * step + span]), g)
            if q != g and (q < g0 or q + N <= g):
                twin[g] = q
    del first
    starts = [f.base for f in sc.files]

    def at(g):
        f = sc.files[bisect.bisect_right(starts, g) - 1]
        return f, g - f.base

    clones = []
    for f in sc.files:
        f.dup = bytearray(len(f.codes))
        i, n, g0 = 0, len(f.codes), f.base
        while i < n:
            q = twin[g0 + i]
            if q < 0:
                i += 1
                continue
            j = i + 1
            while j < n and twin[g0 + j] == q + (j - i):
                j += 1
            L = j - i + N - 1
            sf, si = at(q)
            c = judge(f, i, sf, si, L)
            if c:
                clones.append(c)
                f.dup[i:i + L] = b'\1' * L
            i = j
    clones.sort(key=lambda c: -c['tokens'])
    return clones


def judge(df, di, sf, si, L):
    """A run is a duplicate if the tokens really match (not a hash
    collision), it is not a table of repeated rows, and it is either an
    exact copy or code whose identifiers map one to one between the two
    places."""
    dc = df.codes[di:di + L]
    if dc != sf.codes[si:si + L] or len(set(dc)) < MIN_DISTINCT:
        return None
    ds, ss = df.spell[di:di + L], sf.spell[si:si + L]
    exact = ds == ss
    score = 1.0
    if not exact:
        fwd, back, n = defaultdict(Counter), defaultdict(Counter), 0
        for c, a, b in zip(dc, ss, ds):
            if c == ID:
                fwd[a][b] += 1
                back[b][a] += 1
                n += 1
        if n:
            score = min(sum(max(c.values()) for c in fwd.values()),
                        sum(max(c.values()) for c in back.values())) / n
        # Code uses its names more than once, and that is what makes a
        # one-to-one mapping evidence.  An enumerator list or a table of
        # designated initializers maps one to one whatever it holds.
        reused = sum(t for t in (sum(c.values()) for c in fwd.values()) if t > 1)
        if score < MIN_CONSISTENT or reused < MIN_REUSE * n:
            return None
    return {'tokens': L, 'exact': exact, 'score': round(score, 2),
            'dst': df.rel, 'dst_lines': [df.tline[di], df.tline[di + L - 1]],
            'src': sf.rel, 'src_lines': [sf.tline[si], sf.tline[si + L - 1]]}


# ------------------------------------------------------------------- metrics

def after(f, pos, *want):
    """Do the tokens after `pos` spell `want` (None matches anything)?"""
    sp = f.spell
    if pos + len(want) >= len(sp):
        return False
    return all(w is None or sp[pos + 1 + i] == w for i, w in enumerate(want))


def measure(sc, N):
    ids = sc.ids
    name = {v: k for k, v in ids.items()}
    lp, arrow = ids.get('('), ids.get('->')
    m, lists = {}, {}
    files = sc.files
    inlib = lambda f: module(f.rel) in LIBDIRS

    # size
    m['files'] = len(files)
    m['lines'] = sum(f.lines for f in files)
    m['code_lines'] = sum(f.code_lines for f in files)
    m['tokens'] = sum(len(f.codes) for f in files)
    big = sorted(((f.lines, f.rel) for f in files if f.lines >= BIG_FILE), reverse=True)
    m['files_over_%d_lines' % BIG_FILE] = len(big)
    lists['big_files'] = [{'lines': n, 'file': r} for n, r in big]
    fns = [(e - s + 1, fn, f.rel, s) for f in files for fn, s, e, st in f.funcs]
    m['functions'] = len(fns)
    m['functions_over_%d_lines' % LONG_FN] = sum(1 for x in fns if x[0] >= LONG_FN)
    m['functions_over_150_lines'] = sum(1 for x in fns if x[0] >= 150)
    lists['long_functions'] = [{'lines': n, 'name': fn, 'file': r, 'line': s}
                               for n, fn, r, s in sorted(fns, reverse=True) if n >= LONG_FN]

    # duplication: how much the threshold matters, then the run that counts
    for n in (30, 100):
        find_clones(sc, n)
        m['redundant_percent_at_%d' % n] = round(
            100.0 * sum(sum(f.dup) for f in files) / max(m['tokens'], 1), 2)
    clones = find_clones(sc, N)
    m['redundant_tokens'] = sum(sum(f.dup) for f in files)
    m['redundant_percent'] = round(100.0 * m['redundant_tokens'] / max(m['tokens'], 1), 2)
    m['duplicate_runs'] = len(clones)
    m['duplicate_runs_exact'] = sum(1 for c in clones if c['exact'])
    m['duplicate_runs_across_files'] = sum(1 for c in clones if c['dst'] != c['src'])
    lists['clones'] = clones
    pairs = Counter()
    for c in clones:
        pairs[tuple(sorted((group(c['dst']), group(c['src']))))] += c['tokens']
    lists['clone_pairs'] = [{'a': a, 'b': b, 'tokens': n} for (a, b), n in pairs.most_common()]

    # shared code reimplemented
    fnv = sorted(f.rel for f in files if f.fnv and not inlib(f))
    m['fnv_hash_files'] = len(fnv)
    lists['fnv_hash_files'] = fnv
    calls = lambda w, pred=lambda f: True: [
        (f.rel, f.tline[p]) for f in files if pred(f) for p in f.hits.get(w, ()) if after(f, p, lp)]
    notlib = lambda f: not inlib(f)
    m['realloc_calls'] = len(calls('realloc', notlib))
    m['mmap_calls'] = len(calls('mmap', notlib))
    # (the library's own, common/leb128.h, is the one the others are counted against)
    leb = sorted({(fn, f.rel) for f in files for fn, *_ in f.funcs if 'leb' in fn.lower() and not f.rel.endswith('include/common/leb128.h')})
    m['leb128_functions'] = len(leb)
    lists['leb128_functions'] = [f'{r}: {fn}' for fn, r in leb]
    where = defaultdict(set)
    for f in files:
        for fn, s, e, st in f.funcs:
            if st and fn != 'main':
                where[fn].add(f.rel)
    rep = sorted(((len(v), k, sorted(v)) for k, v in where.items() if len(v) > 1), reverse=True)
    m['static_names_in_several_files'] = len(rep)
    m['static_name_extra_definitions'] = sum(n - 1 for n, _, _ in rep)
    lists['repeated_static_names'] = [{'name': k, 'files': v} for _, k, v in rep]

    # threads
    pc = calls('pthread_create')
    m['thread_creation_sites'] = len(pc)
    lists['thread_creation_sites'] = [f'{r}:{l}' for r, l in pc]
    probes = [(f.rel, f.tline[p]) for f in files
              for w in ('_SC_NPROCESSORS_ONLN', '_SC_NPROCESSORS_CONF') for p in f.hits.get(w, ())]
    probes += [(f.rel, f.tline[p]) for f in files for p in f.hits.get('sysctlbyname', ())
               if after(f, p, lp) and name[f.spell[p + 2]].startswith('"hw.')]
    m['cpu_count_probes'] = len(probes)
    lists['cpu_count_probes'] = sorted(f'{r}:{l}' for r, l in probes)
    m['mutex_and_condvar_uses'] = sum(len(f.hits.get(w, ())) for f in files
                                      for w in ('pthread_mutex_t', 'pthread_cond_t'))

    # state
    m['thread_locals'] = sum(len(f.hits.get(w, ())) for f in files
                             for w in ('_Thread_local', '__thread'))
    m['file_scope_mutable_statics'] = sum(f.statics for f in files)
    knobs = defaultdict(int)
    for f in files:
        for p in f.hits.get('getenv', ()):
            if after(f, p, lp) and f.codes[p + 2] == STR:
                knobs[name[f.spell[p + 2]].strip('"')] += 1
    m['environment_knobs'] = len(knobs)
    m['getenv_calls'] = sum(knobs.values())
    lists['environment_knobs'] = sorted(knobs)

    # hand-written tree walks: switch (x->op) / switch (x->kind)
    walks = 0
    for f in files:
        sp = f.spell
        for p in f.hits.get('switch', ()):
            if after(f, p, lp, None, arrow) and name[sp[p + 4]] in ('op', 'kind', 'k'):
                walks += 1
    m['switches_on_node_kind'] = walks

    # documents
    none = sorted(f.rel for f in files if f.head is None)
    label = sorted(f.rel for f in files if f.head is not None and f.head <= 2)
    m['files_without_header_comment'] = len(none)
    m['files_with_label_only'] = len(label)
    lists['files_without_header_comment'] = none

    # dependencies
    have = {f.rel for f in files} | set(sc.generated)
    edges, internal = Counter(), []
    for f in files:
        d = os.path.dirname(f.rel)
        for inc in f.includes:
            for cand in (os.path.normpath(os.path.join(d, inc)), os.path.normpath(inc),
                         os.path.normpath('include/' + inc)):
                if cand in have:
                    a, b = module(f.rel), module(cand)
                    if a != b:
                        edges[(a, b)] += 1
                        if INTERNAL_HDR.search(cand):
                            internal.append(f'{f.rel} -> {cand}')
                    break
    m['includes_across_modules'] = sum(edges.values())
    m['includes_of_another_modules_internal_header'] = len(internal)
    mutual = sorted({tuple(sorted(e)) for e in edges if (e[1], e[0]) in edges})
    m['module_pairs_including_each_other'] = len(mutual)
    lists['internal_header_includes'] = sorted(internal)
    lists['mutual_modules'] = [f'{a} <-> {b}' for a, b in mutual]
    lists['module_edges'] = [{'from': a, 'to': b, 'includes': n}
                             for (a, b), n in sorted(edges.items())]

    # per directory
    per = defaultdict(lambda: [0, 0, 0, 0])
    for f in files:
        g = per[group(f.rel)]
        g[0] += 1; g[1] += f.lines; g[2] += len(f.codes); g[3] += sum(f.dup)
    lists['directories'] = [{'dir': k, 'files': v[0], 'lines': v[1], 'tokens': v[2],
                             'redundant_tokens': v[3]} for k, v in sorted(per.items())]
    return m, lists


# Numbers that must not grow (--check).  Sizes are left out: new code is
# allowed; new copies, new private pools and new undocumented files are not.
RATCHET = ['redundant_tokens', 'duplicate_runs_across_files', 'fnv_hash_files',
           'realloc_calls', 'mmap_calls', 'leb128_functions',
           'static_name_extra_definitions', 'thread_creation_sites',
           'cpu_count_probes', 'thread_locals', 'environment_knobs',
           'files_without_header_comment', 'functions_over_%d_lines' % LONG_FN,
           'files_over_%d_lines' % BIG_FILE,
           'includes_of_another_modules_internal_header',
           'module_pairs_including_each_other']


# -------------------------------------------------------------------- output

def revision():
    try:
        return subprocess.run(['git', '-C', R, 'rev-parse', '--short', 'HEAD'],
                              capture_output=True, text=True).stdout.strip()
    except OSError:
        return '?'


def report(root, m, lists, nclones, N, only):
    k = lambda n: f'{n / 1000:.1f} k'
    where = os.path.relpath(root, R) if root.startswith(R + '/') else root
    print(f'{where} at {revision()}: {m["files"]} files, '
          f'{k(m["lines"])} lines ({k(m["code_lines"])} with code), {k(m["tokens"])} tokens')

    print(f'\nSize\n  {"directory":24} {"files":>5} {"lines":>7} {"tokens":>8} {"redundant":>10}')
    for d in lists['directories']:
        pct = 100.0 * d['redundant_tokens'] / max(d['tokens'], 1)
        print(f'  {d["dir"]:24} {d["files"]:5} {d["lines"]:7} {d["tokens"]:8} {pct:9.1f}%')
    print(f'  files of {BIG_FILE}+ lines: {m["files_over_%d_lines" % BIG_FILE]}  '
          + ', '.join(f'{os.path.basename(x["file"])} {x["lines"]}' for x in lists['big_files'][:8]))
    print(f'  functions: {m["functions"]}, of 150+ lines: {m["functions_over_150_lines"]}, '
          f'of {LONG_FN}+ lines: {m["functions_over_%d_lines" % LONG_FN]}')
    for x in lists['long_functions'][:8]:
        print(f'    {x["lines"]:5}  {x["name"]}  {x["file"]}:{x["line"]}')

    print(f'\nDuplication (runs of {N}+ tokens, identifiers and strings ignored)')
    print(f'  redundant tokens: {m["redundant_tokens"]} ({m["redundant_percent"]} %) in '
          f'{m["duplicate_runs"]} runs; {m["duplicate_runs_exact"]} runs are exact copies, '
          f'{m["duplicate_runs_across_files"]} repeat another file')
    print(f'  with runs of 30+ tokens: {m["redundant_percent_at_30"]} %; '
          f'of 100+: {m["redundant_percent_at_100"]} %')
    print('  between directories (tokens):')
    for p in lists['clone_pairs'][:12]:
        print(f'    {p["tokens"]:6}  {p["a"]}' + ('' if p['a'] == p['b'] else f' / {p["b"]}'))
    if nclones:
        cl = [c for c in lists['clones']
              if not only or c['dst'].startswith(only) or c['src'].startswith(only)]
        print(f'  longest {min(nclones, len(cl))}' + (f' under {" ".join(only)}:' if only else ':'))
        for c in cl[:nclones]:
            kind = 'exact' if c['exact'] else f'renamed {c["score"]:.2f}'
            print(f'    {c["tokens"]:5}  {c["dst"]}:{c["dst_lines"][0]}-{c["dst_lines"][1]}'
                  f'  =  {c["src"]}:{c["src_lines"][0]}-{c["src_lines"][1]}  ({kind})')

    print('\nShared code written again (outside lib/)')
    print(f'  FNV hash in {m["fnv_hash_files"]} files; realloc() {m["realloc_calls"]} calls; '
          f'mmap() {m["mmap_calls"]} calls; LEB128 functions {m["leb128_functions"]}')
    print(f'  static functions of one name in several files: {m["static_names_in_several_files"]} '
          f'names, {m["static_name_extra_definitions"]} extra definitions')
    print('    ' + ', '.join(f'{x["name"]} x{len(x["files"])}'
                             for x in lists['repeated_static_names'][:18]))

    print('\nThreads')
    print(f'  pthread_create sites: {m["thread_creation_sites"]}  '
          + ' '.join(lists['thread_creation_sites']))
    print(f'  CPU-count probes: {m["cpu_count_probes"]}  ' + ' '.join(lists['cpu_count_probes']))
    print(f'  mutex and condition variable uses: {m["mutex_and_condvar_uses"]}')

    print('\nState')
    print(f'  thread-locals: {m["thread_locals"]}; file-scope mutable statics (approximate): '
          f'{m["file_scope_mutable_statics"]}')
    print(f'  environment knobs: {m["environment_knobs"]} names in {m["getenv_calls"]} getenv calls')
    print(f'  switches on a node kind (hand-written tree walks, approximate): '
          f'{m["switches_on_node_kind"]}')

    print('\nDocuments')
    print(f'  files without an opening comment: {m["files_without_header_comment"]}; '
          f'with a one- or two-line label only: {m["files_with_label_only"]}')

    print('\nDependencies')
    print(f'  includes across modules: {m["includes_across_modules"]}; of another module\'s '
          f'internal header: {m["includes_of_another_modules_internal_header"]}')
    print(f'  module pairs that include each other: {m["module_pairs_including_each_other"]}  '
          + ', '.join(lists['mutual_modules']))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--root')
    ap.add_argument('--min-tokens', type=int, default=60)
    ap.add_argument('--clones', type=int, default=0, metavar='N')
    ap.add_argument('--only', nargs='+', default=[], metavar='PREFIX')
    ap.add_argument('--json', metavar='FILE')
    ap.add_argument('--check', nargs='?', const=BASELINE, metavar='FILE')
    ap.add_argument('--bless', nargs='?', const=BASELINE, metavar='FILE')
    ap.add_argument('--generated', action='store_true')
    a = ap.parse_args()

    t0 = time.time()
    root = source_root(a.root)
    sc = Scan(root)
    for rel in list_sources(root):
        sc.add(rel)
    if a.generated:
        print('\n'.join(sc.generated))
        return 0
    m, lists = measure(sc, a.min_tokens)
    if a.json:
        with open(a.json, 'w') as o:
            json.dump({'root': os.path.relpath(root, R), 'revision': revision(),
                       'metrics': m, 'lists': lists}, o, indent=1)
    if a.bless:
        with open(a.bless, 'w') as o:
            json.dump({k: m[k] for k in RATCHET}, o, indent=1)
            o.write('\n')
        print(f'wrote {os.path.relpath(a.bless, R)}')
        return 0
    if a.check:
        base = json.load(open(a.check))
        bad = [(k, base[k], m[k]) for k in RATCHET if k in base and m[k] > base[k]]
        better = [(k, base[k], m[k]) for k in RATCHET if k in base and m[k] < base[k]]
        for k, b, c in bad:
            print(f'FAIL {k}: {c}, baseline {b}')
        for k, b, c in better:
            print(f'better {k}: {c}, baseline {b} (bench/quality.py --bless)')
        print(f'quality: {len(RATCHET) - len(bad)}/{len(RATCHET)} numbers at or under the baseline')
        return 1 if bad else 0
    report(root, m, lists, a.clones, a.min_tokens, tuple(a.only))
    print(f'\n({time.time() - t0:.1f} s)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
