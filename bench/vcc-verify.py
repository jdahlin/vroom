#!/usr/bin/env python3
"""
vcc against clang's own -verify tests (clang/test/{C,Sema,Preprocessor,Lexer,
Parser}, fetched by bench/fetch-testsuites.sh into work/testsuites/llvm).

A -verify test carries its expected diagnostics in comments:
    int x = y;   // expected-error {{use of undeclared identifier 'y'}}
Each `%clang_cc1 ... -verify[=P,...]` RUN line of a .c file is one test
(file#N).  Its command line is rewritten for vcc (-triple -> --target, a
target vcc lacks -> aarch64-linux-gnu or arm64-apple-macosx, the resource
headers of clang as -isystem) and the test counts only if
  1. clang (Homebrew's, the same rewritten line) passes its own -verify, and
  2. this harness's reading of the directives accepts clang's diagnostics
     (so a directive grammar the harness does not know cannot blame vcc).
vcc then runs -fsyntax-only (-E for preprocessor-only lines) with
-fdiagnostics-format=json; its diagnostics (notes included) are matched
against the directives as clang's VerifyDiagnosticConsumer does: per level,
line (@+N, @-N, @N, @file:N, @*, @#marker), count (N, N+, N-M, +), text a
substring ({{...}}) or regex (-re, {{...}} parts regex, the rest literal).

  bench/vcc-verify.py [-j N] [-v] [--bin-dir DIR] [--target TRIPLE] [--show CAT] [PAT]
      --target: run all lines on x86_64-linux-gnu or aarch64-linux-gnu
      PAT: substring of the test path (e.g. Sema/, C/C11, drs/dr0xx)
      -v: every failure's missing/unexpected diagnostics
      --show CAT: the tests of one category (fail, crash, skip:REASON, ...)
Clang's side is cached under build/ccs/verify/ (by file content and line);
results.json there is compared with the previous run: tests that changed
from pass to fail (and back) are listed.
"""
import hashlib, json, os, re, shlex, subprocess, sys
from collections import Counter, defaultdict
from multiprocessing import Pool
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hostenv import llvm, BUILD

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
T = f'{R}/work/testsuites/llvm/clang/test'
DIRS = ['C', 'Sema', 'Preprocessor', 'Lexer', 'Parser']
_BIN = os.environ.get('VCC_BIN', BUILD)
if '--bin-dir' in sys.argv:
    _k = sys.argv.index('--bin-dir'); _BIN = os.path.abspath(sys.argv[_k + 1]); del sys.argv[_k:_k + 2]
    os.environ['VCC_BIN'] = _BIN
VPP = f'{_BIN}/vpp'
CL = llvm('clang')
TARGET = os.environ.get('VCC_VERIFY_TARGET')
if '--target' in sys.argv:
    _k = sys.argv.index('--target'); TARGET = sys.argv[_k + 1]; del sys.argv[_k:_k + 2]
    os.environ['VCC_VERIFY_TARGET'] = TARGET  # also seen by spawned pool workers
if TARGET and TARGET not in ('x86_64-linux-gnu', 'aarch64-linux-gnu'):
    sys.exit('--target must be x86_64-linux-gnu or aarch64-linux-gnu')
OUT = f'{BUILD}/verify' + (f'/{TARGET}' if TARGET else '')
TIMEOUT = 20

# cc1 options followed by a separate argument
ARG2 = {'-triple', '-target-cpu', '-target-feature', '-target-abi', '-mllvm', '-main-file-name',
        '-aux-triple', '-x', '-include', '-I', '-D', '-U', '-isystem', '-idirafter', '-iquote',
        '-internal-isystem', '-internal-externc-isystem', '-o', '-MT', '-MF', '-ferror-limit',
        '-fmessage-length', '-mcode-model', '-mrelocation-model', '-pic-level', '-mframe-pointer',
        '-analyzer-checker', '-fvisibility', '-ftabstop', '-isysroot', '-imacros', '-fconstexpr-depth',
        '-fbracket-depth', '-ftemplate-depth', '-target-sdk-version', '-std', '-stack-protector',
        '-fdebug-compilation-dir', '-debug-info-kind', '-dwarf-version', '-object-file-name'}
# cc1 options vcc's driver would not take as they are: dropped (with their argument)
DROP = {'-target-cpu', '-target-feature', '-target-abi', '-mllvm', '-main-file-name', '-aux-triple',
        '-internal-isystem', '-internal-externc-isystem', '-mcode-model', '-mrelocation-model',
        '-pic-level', '-mframe-pointer', '-analyzer-checker', '-target-sdk-version',
        '-stack-protector', '-fdebug-compilation-dir', '-debug-info-kind', '-dwarf-version',
        '-object-file-name', '-fmessage-length', '-ftabstop', '-ferror-limit'}
# languages and modes vcc does not do: the line is skipped
SKIP_RE = re.compile(r'(^|\s)(-x\s+(c\+\+|objective-c|cuda|hip|cl|renderscript|c-header|cpp-output)\S*'
                     r'|-fobjc\S*|-ObjC\S*|-fcuda\S*|-cl-std\S*|-fopenmp\S*|-fsycl\S*|-fcxx-exceptions'
                     r'|-std=(c|gnu)\+\+\S*|-fmodules\S*|-emit-\S+|-S\b|-analyze\b|-ast-\S+|-fblocks'
                     r'|-fms-compatibility\S*|-fhlsl\S*|-finclude-default-header)')


def resdir():
    return subprocess.run([CL, '-print-resource-dir'], capture_output=True, text=True).stdout.strip()


def run_lines(src):
    """The RUN commands of a file, continuations joined."""
    out, cur = [], None
    for ln in src.splitlines():
        m = re.search(r'\bRUN:(.*)$', ln)
        if not m:
            continue
        s = m.group(1).rstrip()
        cur = (cur or '') + ' ' + s
        if cur.endswith('\\'):
            cur = cur[:-1]
            continue
        out.append(cur.strip()); cur = None
    return out


def target_for(tr):
    """vcc's target for a RUN line's triple, and whether it was substituted."""
    if TARGET:
        return TARGET, bool(tr and tr != TARGET)
    if tr is None:
        return 'aarch64-linux-gnu', False
    t = tr.lower()
    if re.match(r'(aarch64|arm64)(_be)?', t):
        if 'apple' in t or 'darwin' in t or 'macos' in t or 'ios' in t:
            return 'arm64-apple-macosx', False
        return 'aarch64-linux-gnu', bool(re.search(r'_be|win|msvc|mingw|android|fuchsia|gnu_ilp32', t))
    if re.match(r'wasm(32|64)', t):
        return t, False
    if 'apple' in t or 'darwin' in t or 'macos' in t:
        return 'arm64-apple-macosx', True
    return 'aarch64-linux-gnu', True


def parse_run(cmd, path):
    """(clang argv tail, vcc argv tail, prefixes, mode, substituted) or a skip reason."""
    if not re.match(r'(not\s+)?%clang_cc1\b', cmd):
        return 'skip:not-cc1'
    if cmd.startswith('not'):
        return 'skip:not'
    c = cmd.split('|')[0]
    if re.search(r'%(t|T)\b|%\{|%[a-z_]{3,}', c.replace('%clang_cc1', '').replace('%s', '').replace('%S', '')):
        return 'skip:lit-subst'
    if '-verify' not in c:
        return 'skip:no-verify'
    if SKIP_RE.search(c):
        return 'skip:lang-or-mode'
    c = c.replace('%clang_cc1', '').replace('%s', path).replace('%S', os.path.dirname(path))
    try:
        a = shlex.split(c)
    except ValueError:
        return 'skip:quoting'
    if any(x in ('&&', '||', ';', '>', '2>', '2>&1') for x in a):
        return 'skip:shell'
    prefixes, triple, mode, ign = [], None, 'sema', None
    clang, vcc, i = [], [], 0
    while i < len(a):
        x = a[i]
        if x == '-verify' or x.startswith('-verify='):
            prefixes += x[8:].split(',') if '=' in x else ['expected']
            i += 1; continue
        if x.startswith('-verify-ignore-unexpected'):
            ign = x; clang.append(x); i += 1; continue
        if x.startswith('-triple='):
            triple = x[8:]; i += 1; continue
        if x == '-triple':
            triple = a[i + 1] if i + 1 < len(a) else None; i += 2; continue
        if x in ('-E', '-Eonly', '-P', '-dM'):
            mode = 'pp'; i += 1; continue
        if x == '-fsyntax-only':
            i += 1; continue
        two = x in ARG2 and i + 1 < len(a)
        if x in DROP:
            clang += a[i:i + 2] if two else [x]; i += 2 if two else 1; continue
        if x == '-std' and two:
            clang += [x, a[i + 1]]; vcc.append('-std=' + a[i + 1]); i += 2; continue
        if x.startswith('-') or not os.path.isabs(x):
            if two:
                clang += a[i:i + 2]; vcc += a[i:i + 2]; i += 2; continue
            if not x.startswith('-'):
                return 'skip:extra-input'
            clang.append(x); vcc.append(x); i += 1; continue
        if os.path.abspath(x) != path:
            return 'skip:other-input'
        i += 1
    if ign:
        return 'skip:ignore-unexpected'
    tgt, sub = target_for(triple)
    prefixes = list(dict.fromkeys(prefixes))
    return clang, vcc, prefixes, mode, tgt, sub


# ---- directives ---------------------------------------------------------------

def comments(src):
    """(offset, text) of every comment, strings and char literals skipped."""
    out, i, n = [], 0, len(src)
    while i < n:
        c = src[i]
        if c == '/' and src.startswith('//', i):
            j = i
            while True:
                e = src.find('\n', j)
                e = n if e < 0 else e
                if e > i and src[e - 1] == '\\':
                    j = e + 1; continue
                break
            out.append((i, src[i:e])); i = e
        elif c == '/' and src.startswith('/*', i):
            e = src.find('*/', i + 2); e = n if e < 0 else e + 2
            out.append((i, src[i:e])); i = e
        elif c == "'" and i and (src[i - 1].isalnum() or src[i - 1] == '_') and \
                re.search(r'(?<![\w.])[.]?\d[\w.\']*$', src[max(0, i - 200):i]):
            i += 1                   # a C23 digit separator in a number
        elif c in '"\'':
            j = i + 1
            while j < n and src[j] != c and src[j] != '\n':
                j += 2 if src[j] == '\\' else 1
            i = j + 1
        else:
            i += 1
    return out


COND = re.compile(r'\s*#\s*(if|ifdef|ifndef|elif|elifdef|elifndef|else|endif)\b')


def live_comments(path, cc1, keep):
    """(file, line, text) of the comments clang's -verify reads: those of the
    main file outside skipped conditional blocks (found by preprocessing a
    copy with a marker line at the top of each branch), and those of the
    files it includes from the test tree.  keep(text) selects."""
    src = open(path, encoding='utf-8', errors='surrogateescape').read()
    cs = comments(src)
    import bisect
    cstart = [o for o, _ in cs]
    cend = [o + len(t) for o, t in cs]

    def in_comment(o):
        k = bisect.bisect_right(cstart, o) - 1
        return k >= 0 and o < cend[k]
    lines = src.split('\n')
    offs, o = [], 0
    for l in lines:
        offs.append(o); o += len(l) + 1
    out_lines, conds, i = [], [], 0      # conds: (line index, kind, marker or None)
    while i < len(lines):
        m = COND.match(lines[i]) if not in_comment(offs[i]) else None
        j = i
        while j < len(lines) - 1 and lines[j].endswith('\\'):
            j += 1
        out_lines += lines[i:j + 1]
        if m:
            k = m.group(1)
            mk = None
            if k != 'endif':
                mk = len(conds)
                out_lines.append(f'__vcc_live_{mk}__')
            conds.append((i, k, mk))
        i = j + 1
    tmp = f'{os.path.dirname(path)}/.vlive-{os.getpid()}-{os.path.basename(path)}'
    open(tmp, 'w', encoding='utf-8', errors='surrogateescape').write('\n'.join(out_lines))
    try:
        r = subprocess.run([CL, '-cc1', '-E'] + cc1 + ['-o', '-', tmp], capture_output=True, text=True,
                           errors='surrogateescape', timeout=TIMEOUT, cwd=os.path.dirname(path))
    finally:
        os.unlink(tmp)
    seen = set(int(x) for x in re.findall(r'__vcc_live_(\d+)__', r.stdout))
    # liveness per line: a stack of the enclosing branches' markers
    live = [True] * (len(lines) + 1)
    stack, ci = [True], 0
    cmap = {c[0]: c for c in conds}
    for n in range(len(lines)):
        c = cmap.get(n)
        if c:
            k, mk = c[1], c[2]
            if k in ('if', 'ifdef', 'ifndef'):
                live[n] = stack[-1]; stack.append(mk in seen)
            elif k == 'endif':
                if len(stack) > 1:
                    stack.pop()
                live[n] = stack[-1]
            else:
                live[n] = stack[-2] if len(stack) > 1 else True
                stack[-1] = mk in seen
            continue
        live[n] = stack[-1]
    rp = os.path.realpath(path)
    # presumed (file, line) of each line: #line directives
    pres, f, d = [], rp, 0
    for n in range(len(lines)):
        pres.append((f, n + 1 + d))
        m = re.match(r'\s*#\s*line\s+(\d+)(?:\s+"([^"]*)")?', lines[n])
        if m and live[n] and not in_comment(offs[n]):
            d = int(m.group(1)) - (n + 2)
            if m.group(2):
                f = os.path.realpath(os.path.join(os.path.dirname(path), m.group(2)))
    out = []
    for o, t in cs:
        n = bisect.bisect_right(offs, o) - 1
        if live[n] and keep(t):
            out.append(pres[n] + (t,))
    # files included from the test tree
    tdir = os.path.realpath(T)
    for f in sorted(set(re.findall(r'^# \d+ "([^"]+)"', r.stdout, re.M))):
        fp = os.path.realpath(os.path.join(os.path.dirname(path), f))
        if fp == rp or not fp.startswith(tdir + '/') or '.vlive-' in fp or not os.path.isfile(fp):
            continue
        hs = open(fp, encoding='utf-8', errors='surrogateescape').read()
        ho = [0] + [m.end() for m in re.finditer('\n', hs)]
        for o, t in comments(hs):
            if keep(t):
                out.append((fp, bisect.bisect_right(ho, o), t))
    return out


def close_brace(t, p, ob, cb):
    depth = 1
    while True:
        a, b = t.find(ob, p), t.find(cb, p)
        if b < 0:
            return -1
        if 0 <= a < b:
            depth += 1; p = a + len(ob)
        else:
            depth -= 1
            if depth == 0:
                return b
            p = b + len(cb)


def directives(cs, prefixes):
    """[(level, file|None, line|None, min, max, text)] or raise ValueError;
    file is the comment's own (realpath) or an @file name."""
    markers = {}
    for f, l, t in cs:
        for m in re.finditer(r'(?<![\w#@])#([A-Za-z_]\w*)', t):
            markers.setdefault(m.group(1), (f, l + t.count('\n', 0, m.start())))
    pre = '|'.join(re.escape(p) for p in sorted(prefixes, key=len, reverse=True))
    rx = re.compile(r'(?<![\w-])(' + pre + r')-(error|warning|note|remark|no-diagnostics)(-re)?(?![\w-])')
    out, nodiag = [], False
    for cf, cl, t in cs:
        for m in rx.finditer(t):
            lvl = m.group(2)
            if lvl == 'no-diagnostics':
                nodiag = True; continue
            p = m.end()
            line, file = cl, cf
            if p < len(t) and t[p] == '@':
                mm = re.match(r'@(?:([^\s:]+):)?([+-]?\d+|\*|#\w+)', t[p:])
                if not mm:
                    raise ValueError('bad @')
                f, l = mm.group(1), mm.group(2)
                if f:
                    file = f
                if l == '*':
                    line = None
                elif l.startswith('#'):
                    if l[1:] not in markers:
                        raise ValueError('marker')
                    file, line = markers[l[1:]]
                elif l[0] in '+-':
                    line += int(l)
                else:
                    line = int(l)
                p += mm.end()
            mm = re.match(r'\s*(?:(\d+)(?:(\+)|-(\d+))?|(\+))?\s*', t[p:])
            lo = hi = 1
            if mm.group(1):
                lo = int(mm.group(1)); hi = 1 << 30 if mm.group(2) else int(mm.group(3)) if mm.group(3) else lo
            elif mm.group(4):
                lo, hi = 1, 1 << 30
            p += mm.end()
            mm = re.match(r'(\{{2,})', t[p:])
            if not mm:
                raise ValueError('no {{')
            ob = mm.group(1); cb = '}' * len(ob)
            e = close_brace(t, p + len(ob), ob, cb)
            if e < 0:
                raise ValueError('no }}')
            text = t[p + len(ob):e]
            if m.group(3):
                parts, q = [], 0
                for z in re.finditer(r'\{\{(.*?)\}\}', text, re.S):
                    parts.append(re.escape(text[q:z.start()])); parts.append('(' + z.group(1) + ')'); q = z.end()
                parts.append(re.escape(text[q:]))
                try:
                    text = re.compile(''.join(parts), re.S)
                except re.error:
                    raise ValueError('regex')
            else:
                text = text.replace('\\n', '\n')
            out.append((lvl, file, line, lo, hi, text))
    if not out and not nodiag:
        raise ValueError('no directives')
    return out


MACRO_NOTE = re.compile(r'expanded from (here|macro )|in instantiation of|while substituting')


def check(dirs, diags, path):
    """(missing, unexpected) lists of diagnostics [(level, file, line, msg)];
    macro backtrace notes are part of their diagnostic's text, not checked."""
    left = [d for d in diags if not (d[0] == 'note' and MACRO_NOTE.match(d[3]))]
    missing = []
    for lvl, file, line, lo, hi, text in dirs:
        got = 0
        for d in list(left):
            if got >= hi:
                break
            dl, df, dn, dm = d
            if dl != lvl:
                continue
            if not df or not (df == file or df.endswith('/' + file)) or (line is not None and dn != line):
                continue
            if (text.search(dm) if hasattr(text, 'search') else text in dm):
                left.remove(d); got += 1
        if got < lo:
            missing.append((lvl, os.path.basename(file), line,
                            text.pattern if hasattr(text, 'pattern') else text))
    return missing, left


FLAG = re.compile(r' \[-W[^\]]*\]$')


def clang_diags(text, cwd):
    out = []
    for ln in text.splitlines():
        m = re.match(r'(.*?):(\d+):(\d+): (fatal error|error|warning|note|remark): (.*)$', ln)
        if m:
            out.append(('error' if m.group(4) == 'fatal error' else m.group(4), os.path.realpath(os.path.join(cwd, m.group(1))),
                        int(m.group(2)), FLAG.sub('', m.group(5))))
            continue
        m = re.match(r'(fatal error|error|warning|note|remark): (.*)$', ln)
        if m:
            out.append(('error' if m.group(1) == 'fatal error' else m.group(1), None, None, FLAG.sub('', m.group(2))))
    return out


def vcc_diags(text, cwd):
    out = []

    def add(r):
        lv = r.get('level', '')
        lv = 'error' if lv.startswith('fatal') else lv
        loc = r.get('location') or {}
        f = loc.get('path') or loc.get('file')
        out.append((lv, os.path.realpath(os.path.join(cwd, f)) if f and not f.startswith('<') else f, loc.get('line'),
                    FLAG.sub('', r.get('message', ''))))
        for c in r.get('children') or []:
            add(c)
    for ln in text.splitlines():
        ln = ln.strip()
        if ln.startswith('{'):
            try:
                add(json.loads(ln))
            except ValueError:
                pass
    return out


# ---- one test -------------------------------------------------------------------

def job(t):
    path, idx, cmd, RES = t
    name = f'{os.path.relpath(path, T)}#{idx}'
    p = parse_run(cmd, path)
    if isinstance(p, str):
        return name, p, None
    clang, vcc, prefixes, mode, tgt, sub = p
    src = open(path, encoding='utf-8', errors='surrogateescape').read()
    key = hashlib.sha1((src + '\0' + cmd + '\0' + tgt + '\0' + os.path.realpath(CL)).encode('utf-8', 'surrogateescape')).hexdigest()
    cf = f'{OUT}/clang/{key}.json'
    try:
        ref = json.load(open(cf))
    except (OSError, ValueError):
        base = [CL, '-cc1', '-internal-isystem', f'{RES}/include', '-nostdsysteminc', '-triple', tgt,
                '-fsyntax-only' if mode == 'sema' else '-E', '-o', '/dev/null', '-fno-caret-diagnostics'] + clang + [path]
        vfy = ['-verify' if prefixes == ['expected'] else '-verify=' + ','.join(prefixes)]
        try:
            r1 = subprocess.run(base + vfy, capture_output=True, text=True, errors='replace', timeout=TIMEOUT,
                                cwd=os.path.dirname(path))
            r2 = subprocess.run(base, capture_output=True, text=True, errors='replace', timeout=TIMEOUT,
                                cwd=os.path.dirname(path))
            pre = [a for a in base[2:-1] if a not in ('-fsyntax-only', '-E', '/dev/null', '-o')]
            cm = live_comments(path, pre, lambda t: '#' in t or any(q + '-' in t for q in prefixes))
            ref = {'verify': r1.returncode, 'vout': r1.stderr[-2000:], 'diags': clang_diags(r2.stderr, os.path.dirname(path)), 'cmts': cm}
        except subprocess.TimeoutExpired:
            ref = {'verify': -1, 'vout': 'timeout', 'diags': []}
        os.makedirs(os.path.dirname(cf), exist_ok=True)
        json.dump(ref, open(cf, 'w'))
    if ref['verify'] != 0:
        return name, 'clang-fails', None
    rp = os.path.realpath(path)
    try:
        dirs = directives(ref['cmts'], prefixes)
    except ValueError as e:
        return name, 'harness:' + str(e), None
    cd = [tuple(d) for d in ref['diags']]
    if check(dirs, cd, rp) != ([], []):
        return name, 'harness:clang-mismatch', None
    argv = [VPP, f'--target={tgt}', '-nostdinc', '-isystem', f'{RES}/include', '-fdiagnostics-format=json',
            '-fdiagnostics-style=clang', '-fno-color-diagnostics', '-ferror-limit=0']
    argv += ['-fsyntax-only'] if mode == 'sema' else ['-E', '-o', '/dev/null']
    argv += vcc + [path]
    try:
        r = subprocess.run(argv, capture_output=True, text=True, errors='replace', timeout=TIMEOUT,
                           cwd=os.path.dirname(path))
    except subprocess.TimeoutExpired:
        return name, 'timeout', {'sub': sub}
    if r.returncode < 0 or r.returncode >= 128:
        return name, 'crash', {'sub': sub, 'rc': r.returncode, 'err': r.stderr[-1500:], 'argv': argv}
    missing, unexp = check(dirs, vcc_diags(r.stdout + '\n' + r.stderr, os.path.dirname(path)), rp)
    if not missing and not unexp:
        return name, 'pass', {'sub': sub}
    return name, 'fail', {'sub': sub, 'missing': missing, 'unexpected': unexp, 'argv': argv}


def main():
    j, verbose, show, pats = 6, False, None, []
    a = sys.argv[1:]
    while a:
        x = a.pop(0)
        if x == '-j': j = int(a.pop(0))
        elif x.startswith('-j'): j = int(x[2:])
        elif x == '-v': verbose = True
        elif x == '--show': show = a.pop(0)
        else: pats.append(x)
    if not os.path.isdir(T):
        sys.exit(f'{T}: missing; run bench/fetch-testsuites.sh llvm')
    RES = resdir()
    jobs = []
    for d in DIRS:
        for root, _, fs in os.walk(f'{T}/{d}'):
            for f in sorted(fs):
                if not f.endswith('.c'):
                    continue
                p = os.path.join(root, f)
                rel = os.path.relpath(p, T)
                if pats and not any(q in rel for q in pats):
                    continue
                src = open(p, encoding='utf-8', errors='surrogateescape').read()
                if re.search(r'^\s*//\s*(XFAIL|UNSUPPORTED):\s*\*', src, re.M):
                    continue
                for i, c in enumerate(run_lines(src)):
                    if '-verify' in c:
                        jobs.append((p, i, c, RES))
    with Pool(j) as pool:
        res = pool.map(job, jobs, chunksize=4)
    res.sort()
    cat = Counter(c for _, c, _ in res)
    bydir = defaultdict(Counter)
    for n, c, _ in res:
        d = n.split('/')[0] + ('/' + n.split('/')[1] if n.startswith('C/') else '')
        bydir[d][c] += 1
    print(f'{"dir":<16} {"tests":>6} {"pass":>6} {"fail":>6} {"crash":>6} {"valid":>6} {"% pass":>7}')
    for d in sorted(bydir):
        c = bydir[d]
        v = c['pass'] + c['fail'] + c['crash'] + c['timeout']
        print(f'{d:<16} {sum(c.values()):>6} {c["pass"]:>6} {c["fail"]:>6} {c["crash"] + c["timeout"]:>6} {v:>6} '
              f'{100 * c["pass"] / v if v else 0:>6.1f}%')
    v = cat['pass'] + cat['fail'] + cat['crash'] + cat['timeout']
    print(f'{"total":<16} {len(res):>6} {cat["pass"]:>6} {cat["fail"]:>6} {cat["crash"] + cat["timeout"]:>6} {v:>6} '
          f'{100 * cat["pass"] / v if v else 0:>6.1f}%')
    nsub = Counter(c for _, c, x in res if x and x.get('sub'))
    print(f'  of the valid tests {sum(nsub.values())} run on a substituted target (pass {nsub["pass"]})')
    print('  not counted: ' + ', '.join(f'{k} {n}' for k, n in sorted(cat.items())
                                        if k not in ('pass', 'fail', 'crash', 'timeout')))
    os.makedirs(OUT, exist_ok=True)
    rf = f'{OUT}/results.json'
    now = {n: c for n, c, _ in res}
    if not pats and os.path.exists(rf):
        old = json.load(open(rf))
        worse = sorted(n for n, c in now.items() if old.get(n) == 'pass' and c != 'pass')
        better = sorted(n for n, c in now.items() if old.get(n) in ('fail', 'crash', 'timeout') and c == 'pass')
        if better:
            print(f'  newly passing: {len(better)}')
        if worse:
            print(f'  REGRESSED ({len(worse)}): ' + ' '.join(worse[:30]))
    if not pats:
        json.dump(now, open(rf, 'w'), indent=0, sort_keys=True)
    json.dump({n: x for n, c, x in res if c in ('fail', 'crash')}, open(f'{OUT}/failures.json', 'w'))
    for n, c, x in res:
        if c == 'crash' and (verbose or show in (None, 'crash')):
            print(f'CRASH {n} rc={x["rc"]}: ' + ' '.join(shlex.quote(q) for q in x['argv']))
        if show and (c == show or (show == 'fail' and c == 'fail')) or (verbose and c == 'fail'):
            print(f'{c.upper()} {n}')
            if x and c == 'fail':
                for m in x['missing'][:12]:
                    print(f'   missing     {m[0]} {m[1]}:{m[2]} {{{{{m[3]}}}}}')
                for d in x['unexpected'][:12]:
                    print(f'   unexpected  {d[0]} {os.path.basename(d[1] or "")}:{d[2]} {d[3]}')
                if verbose:
                    print('   ' + ' '.join(shlex.quote(q) for q in x['argv']))
    sys.exit(1 if cat['crash'] else 0)


if __name__ == '__main__':
    main()
