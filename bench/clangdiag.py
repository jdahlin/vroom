#!/usr/bin/env python3
"""
clang's diagnostic table as data (PLAN-diag.md D0.1).

Parses clang's include/clang/Basic/Diagnostic*.td (fetched once into
work/clang-td/VERSION/, git-ignored) into one record per diagnostic:
name, class (error/warning/extension/note/remark), default severity,
-W group, component, category, message template and a language bucket
(c, cxx, objc, omp, gpu, target, tool).  The bucket is a heuristic over
name, message, category and group; `stats` prints the counts.

The template converts to a regex (`to_regex`) that matches a rendered
message, and to a few concrete samples (`samples`), for matching vcc's
messages in bench/vcc-diagcov.py.

  bench/clangdiag.py fetch [VERSION]   download the .td files (default: Homebrew clang's version)
  bench/clangdiag.py stats             counts per bucket and class
  bench/clangdiag.py show REGEX        records whose name or message matches
"""
import itertools, os, re, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hostenv import llvm

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FILES = ['Diagnostic.td', 'DiagnosticASTKinds.td', 'DiagnosticCategories.td', 'DiagnosticCommentKinds.td',
         'DiagnosticCommonKinds.td', 'DiagnosticCrossTUKinds.td', 'DiagnosticDocs.td', 'DiagnosticDriverKinds.td',
         'DiagnosticFrontendKinds.td', 'DiagnosticGroups.td', 'DiagnosticInstallAPIKinds.td', 'DiagnosticLexKinds.td',
         'DiagnosticParseKinds.td', 'DiagnosticRefactoringKinds.td', 'DiagnosticSemaKinds.td',
         'DiagnosticSerializationKinds.td', 'DiagnosticTrapKinds.td']
CLASSES = {'Error': 'error', 'Warning': 'warning', 'Extension': 'extension', 'ExtWarn': 'extension',
           'Note': 'note', 'Remark': 'remark', 'Trap': 'trap'}


def clang_version():
    out = subprocess.run([llvm('clang'), '--version'], capture_output=True, text=True).stdout
    return re.search(r'version (\d+\.\d+\.\d+)', out).group(1)


def td_dir(ver=None):
    ver = ver or os.environ.get('CLANG_TD_VERSION') or clang_version()
    return f'{R}/work/clang-td/{ver}'


def fetch(ver=None):
    ver = ver or clang_version()
    d = td_dir(ver)
    os.makedirs(d, exist_ok=True)
    base = f'https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-{ver}/clang/include/clang/Basic'
    for f in FILES:
        subprocess.run(['curl', '-fsSL', '-o', f'{d}/{f}', f'{base}/{f}'], check=True)
    print(f'{len(FILES)} files in {d}')


# ---------------- .td parsing ----------------

def _strip_comments(s):
    out, i, n = [], 0, len(s)
    while i < n:
        c = s[i]
        if c == '"':
            j = i + 1
            while j < n and s[j] != '"':
                j += 2 if s[j] == '\\' else 1
            out.append(s[i:j + 1])
            i = j + 1
        elif s.startswith('//', i):
            i = s.find('\n', i)
            i = n if i < 0 else i
        elif s.startswith('/*', i):
            i = s.find('*/', i) + 2
        elif s.startswith('[{', i):		# code block (docs)
            j = s.find('}]', i)
            out.append('""')
            i = j + 2
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def _strings(s):
    """the concatenation of the string literals in s"""
    return ''.join(m.group(1).encode().decode('unicode_escape') for m in re.finditer(r'"((?:[^"\\]|\\.)*)"', s))


def _split_top(s, sep=','):
    parts, depth, cur, q = [], 0, [], False
    for c in s:
        if q:
            cur.append(c)
            q = c != '"'
            continue
        if c == '"':
            q = True
        elif c in '<([':
            depth += 1
        elif c in '>)]':
            depth -= 1
        elif c == sep and depth == 0:
            parts.append(''.join(cur).strip())
            cur = []
            continue
        cur.append(c)
    if ''.join(cur).strip():
        parts.append(''.join(cur).strip())
    return parts


def _statements(text):
    """yield ('let', {k: v}) / ('end', None) / ('def', name, body) / ('defm', ...) at the top level"""
    i, n = 0, len(text)
    while i < n:
        m = re.compile(r'\s*').match(text, i)
        i = m.end()
        if i >= n:
            break
        if text[i] == '}':
            yield ('end', None)
            i += 1
            continue
        m = re.compile(r'let\s+(.*?)\s+in\s*\{', re.S).match(text, i)
        if m:
            kv = {}
            for a in _split_top(m.group(1)):
                k, _, v = a.partition('=')
                kv[k.strip()] = v.strip()
            yield ('let', kv)
            i = m.end()
            continue
        m = re.compile(r'(def|defm|class|multiclass|include)\b').match(text, i)
        if not m:
            i = text.find('\n', i) + 1 or n
            continue
        kw = m.group(1)
        if kw == 'include':
            i = text.find('\n', i) + 1 or n
            continue
        # a statement ends at ';' at depth 0 or at its '{...}' body
        j, depth, q = m.end(), 0, False
        while j < n:
            c = text[j]
            if q:
                if c == '\\':
                    j += 1
                elif c == '"':
                    q = False
            elif c == '"':
                q = True
            elif c in '<([':
                depth += 1
            elif c in '>)]':
                depth -= 1
            elif depth == 0 and c == ';':
                break
            elif depth == 0 and c == '{':
                d2 = 1
                j += 1
                while j < n and d2:
                    if text[j] == '"':
                        j = text.index('"', j + 1)
                    elif text[j] == '{':
                        d2 += 1
                    elif text[j] == '}':
                        d2 -= 1
                    j += 1
                j -= 1
                break
            j += 1
        yield (kw, text[m.end():j + 1])
        i = j + 1


def load(d=None):
    """{name: record}, {group def name: (flag, [subgroup def names])}, {substitution: text}"""
    d = d or td_dir()
    diags, groups, subst = {}, {}, {}
    inline_groups = {}
    for f in FILES:
        if f in ('Diagnostic.td', 'DiagnosticDocs.td', 'DiagnosticCategories.td'):
            continue
        text = _strip_comments(open(f'{d}/{f}', encoding='utf-8').read())
        lets = []
        for st in _statements(text):
            kind = st[0]
            if kind == 'let':
                lets.append(st[1])
                continue
            if kind == 'end':
                if lets:
                    lets.pop()
                continue
            if kind not in ('def',):
                continue
            body = st[1].strip().rstrip(';')
            m = re.match(r'(\w+)\s*:\s*(.*)', body, re.S)
            if not m:
                continue
            name, rest = m.group(1), m.group(2)
            parents = _split_top(rest)
            head = parents[0]
            hm = re.match(r'(\w+)\s*<(.*)>\s*$', head, re.S)
            cls = hm.group(1) if hm else head
            if cls == 'DiagGroup':
                a = _split_top(hm.group(2))
                subs = re.findall(r'\w+', a[1]) if len(a) > 1 else []
                groups[name] = (_strings(a[0]), subs)
                continue
            if cls == 'TextSubstitution':
                subst[name] = _strings(hm.group(2))
                continue
            if cls not in CLASSES and not (hm and cls in ('NonportablePath', 'NonportablePathTrailing',
                                                            'NonportablePrivateAPINotesPath')):
                continue
            ctx = {}
            for kv in lets:
                ctx.update(kv)
            r = {'name': name, 'class': CLASSES.get(cls, 'warning'), 'text': _strings(hm.group(2)) if hm else '',
                 'file': f, 'component': _strings(ctx.get('Component', '')),
                 'category': _strings(ctx.get('CategoryName', '')), 'group': None, 'default': None}
            for p in parents[1:]:
                gm = re.match(r'InGroup\s*<\s*(.*)>\s*$', p, re.S)
                if gm:
                    g = gm.group(1).strip()
                    im = re.match(r'DiagGroup\s*<\s*"([^"]*)"', g)
                    if im:
                        inline_groups[im.group(1)] = 1
                        r['group'] = im.group(1)
                    else:
                        r['group'] = g		# a def name, resolved below
                elif p in ('DefaultIgnore', 'DefaultWarn', 'DefaultError', 'DefaultFatal'):
                    r['default'] = p[7:].lower()
                elif p.startswith('CatBackend') or p == 'BackendInfo':
                    r['category'] = r['category'] or 'Backend Issue'
            diags[name] = r
    for r in diags.values():
        g = r['group']
        if g and g in groups:
            r['group'] = groups[g][0]
        if r['default'] is None:
            r['default'] = {'error': 'error', 'warning': 'warning', 'extension': 'ignore', 'note': '-',
                            'remark': 'ignore', 'trap': '-'}[r['class']]
        smp = samples(parse(r['text'], subst), 1)
        r['first'] = smp[0] if smp else r['text']
        r['bucket'] = bucket(r)
    return diags, groups, subst


# ---------------- language bucket (heuristic) ----------------

_NONC_CAT = [('objc', r'ARC|Cocoa|Objective-C|Generics Issue|Nullability'), ('cxx', r'Lambda|Coroutines|Concepts|Modules|'
             r'Related Result Type|Template'), ('omp', r'OpenMP|OpenACC'), ('gpu', r'CUDA|HLSL|SYCL|OpenCL')]
_NAME = [	# matched against the name's '_'-separated tokens
    ('omp', r'omp|openmp|acc|openacc'),
    ('objc', r'objc|arc|property|selector|ivar|ivars|protocol|nsobject|cf|nsconsumed|swift|ns|nullability|'
             r'synthesize|nsstring|kindof'),
    ('gpu', r'opencl|cuda|hip|hlsl|sycl|spirv|amdgpu|nvptx|ocl'),
    ('target', r'riscv|sve|neon|arm|x86|wasm|mips|ppc|ve|hexagon|loongarch|bpf|sme|mve|aarch64|avx|amx|systemz|'
               r'tme|rvv|zve|vsx|altivec|ptrauth'),
    ('cxx', r'cxx\w*|cpp\w*|template\w*|templ|lambda|class|ctor|dtor|constexpr|consteval|constinit|virtual|'
            r'namespace|concept|coroutine|await|friend|override|using|module|exception|throw|catch|typeid|'
            r'rvalue|nullptr|deduction|deduced|explicit|mutable|capture|overload|overloaded|'
            r'constraint|decltype|pure|structured|decomp|fold|'
            r'typename|dependent|inherit|inherited|destructor|constructor|ms|microsoft|'
            r'dllimport|dllexport|declspec|uuid|mangling|vtable|odr|noexcept|'
            r'instantiation|instantiate|specialization|'
            r'scoped|abstract|polymorphic|vbase|ovl|memptr|deleted|defaulted|covariant|overriding|import|lifetime|'
               r'allocation|conv'),
]
_MSG = [
    ('omp', r'OpenMP|OpenACC|\'#pragma omp'),
    ('objc', r'Objective-C|@property|@interface|@implementation|@selector|\bARC\b|__weak|__strong|NSObject|'
             r'instance method|class method|ivar|@synthesize|nullability'),
    ('gpu', r'OpenCL|CUDA|HIP|HLSL|SYCL|__device__|__global__|__host__|kernel function'),
    ('cxx', r'C\+\+|template|lambda|constexpr|consteval|constinit|\bclass\b|namespace|\bvirtual\b|constructor|'
            r'destructor|\boverload|\bfriend\b|base class|\bnullptr\b|rvalue|lvalue reference|'
            r'\bconcept\b|coroutine|co_await|\bmodule\b|exception|noexcept|typename|operator\w*\(|\'this\'|'
            r'\bdecltype\b|member function|structured binding|deduction|\binherit|\bmutable\b|'
            r'\bexplicit\b|parameter pack|fold expression|\bvtable|\bstd::|defaulted|deleted function|constraints? not '
            r'satisfied|member pointer|conversion function|overrid'),
]
_TOOL = {'Driver', 'Serialization', 'CrossTU', 'InstallAPI', 'Refactoring', 'Comment'}
_GROUP = [('cxx', r'^c\+\+|cxx|^pre-c\+\+|c\+\+\d|^deprecated-(copy|dynamic|this|literal-operator)'), ('objc', r'^objc|arc|'
          r'nullab|^swift'), ('omp', r'openmp|openacc|^source-uses-openmp'), ('gpu', r'cuda|opencl|hlsl|sycl')]


def bucket(r):
    if r['component'] in _TOOL:
        return 'tool'
    for b, pat in _NONC_CAT:
        if r['category'] and re.search(pat, r['category']):
            return b
    for b, pat in _GROUP:
        if r['group'] and re.search(pat, r['group']):
            return b
    toks = r['name'].split('_')[1:]
    for b, pat in _NAME:
        if any(re.fullmatch(pat, t) for t in toks):
            return b
    first = r.get('first', r['text'])		# each %select as its first alternative
    for b, pat in _MSG:
        if re.search(pat, first):
            return b
    return 'c'


# ---------------- message templates ----------------

def _brace(s, i):
    """s[i] == '{': index of the matching '}'"""
    d = 0
    for j in range(i, len(s)):
        if s[j] == '{':
            d += 1
        elif s[j] == '}':
            d -= 1
            if not d:
                return j
    return len(s) - 1


def _alts(s):
    out, d, cur = [], 0, []
    for c in s:
        if c == '{':
            d += 1
        elif c == '}':
            d -= 1
        if c == '|' and d == 0:
            out.append(''.join(cur))
            cur = []
        else:
            cur.append(c)
    out.append(''.join(cur))
    return out


ARG = 'ARG'


def parse(t, subst):
    """template -> list of nodes: str | ('arg',) | ('alt', [nodes...]) | ('s',)"""
    out, i, n = [], 0, len(t)
    lit = []

    def flush():
        if lit:
            out.append(''.join(lit))
            lit.clear()

    while i < n:
        c = t[i]
        if c != '%':
            lit.append(c)
            i += 1
            continue
        if t.startswith('%%', i):
            lit.append('%')
            i += 2
            continue
        m = re.compile(r'%([a-z_]*)').match(t, i)
        mod = m.group(1)
        j = m.end()
        flush()
        if mod == 'enum_select':
            j = t.index('>', j) + 1
            mod = 'select'
        if mod in ('select', 'plural', 'diff', 'sub') and j < n and t[j] == '{':
            k = _brace(t, j)
            inner = t[j + 1:k]
            j = k + 1
            am = re.compile(r'[\d,]*').match(t, j)
            j = am.end()
            if mod == 'sub':
                out.extend(parse(subst.get(inner, ''), subst))
            elif mod == 'diff':
                a = _alts(inner)
                out.append(('alt', [parse(a[0].replace('$', '%0'), subst), parse(a[-1], subst)]))
            elif mod == 'plural':
                out.append(('alt', [parse(x.split(':', 1)[1] if ':' in x else x, subst) for x in _alts(inner)]))
            else:
                alts = [re.sub(r'^%\w+\{(.*)\}$', r'\1', x, flags=re.S) for x in _alts(inner)]
                out.append(('alt', [parse(x, subst) for x in alts]))
            i = j
            continue
        am = re.compile(r'\d+').match(t, j)
        j = am.end() if am else j
        out.append(('s',) if mod == 's' else ('arg',))
        i = j
    flush()
    return out


def to_regex(nodes, arg=r'.+?'):
    rx = []
    for x in nodes:
        if isinstance(x, str):
            rx.append(re.escape(x))
        elif x[0] == 'arg':
            rx.append(arg)
        elif x[0] == 's':
            rx.append('s?')
        else:
            rx.append('(?:' + '|'.join(to_regex(a, arg) for a in x[1]) + f'|{arg})')
    return ''.join(rx)


def samples(nodes, cap=64):
    """up to cap concrete messages, ARG for each argument"""
    parts = []
    for x in nodes:
        if isinstance(x, str):
            parts.append([x])
        elif x[0] == 'arg':
            parts.append([ARG])
        elif x[0] == 's':
            parts.append(['', 's'])
        else:
            alts = []
            for a in x[1]:
                alts.extend(samples(a, cap))
            parts.append(alts[:cap] or [''])
    out = []
    for p in itertools.product(*parts):
        out.append(''.join(p))
        if len(out) >= cap:
            break
    return out


def main():
    a = sys.argv[1:]
    if not a or a[0] in ('-h', '--help'):
        print(__doc__)
        return 0
    if a[0] == 'fetch':
        fetch(a[1] if len(a) > 1 else None)
        return 0
    diags, groups, subst = load()
    if a[0] == 'stats':
        from collections import Counter
        cnt = Counter((r['bucket'], r['class']) for r in diags.values())
        bs = ['c', 'cxx', 'objc', 'omp', 'gpu', 'target', 'tool']
        cl = ['error', 'warning', 'extension', 'note', 'remark', 'trap']
        print(f'{"":8}' + ''.join(f'{c:>10}' for c in cl) + f'{"total":>10}')
        for b in bs:
            print(f'{b:8}' + ''.join(f'{cnt[b, c]:>10}' for c in cl) + f'{sum(cnt[b, c] for c in cl):>10}')
        print(f'{"total":8}' + ''.join(f'{sum(cnt[b, c] for b in bs):>10}' for c in cl) + f'{len(diags):>10}')
        print(f'{len(groups)} -W groups, {len(subst)} substitutions')
        return 0
    if a[0] == 'show':
        for r in diags.values():
            if re.search(a[1], r['name']) or re.search(a[1], r['text']):
                print(f'{r["name"]:50} {r["class"]:9} {r["bucket"]:6} {r["group"] or "":28} {r["text"]}')
        return 0
    print(__doc__)
    return 1


if __name__ == '__main__':
    sys.exit(main())
