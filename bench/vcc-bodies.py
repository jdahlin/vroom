#!/usr/bin/env python3
"""How much of what the parser sees is header function bodies a TU never uses.

    bench/vcc-bodies.py [-j N] [PAT]     default: the TUs in bench/vbench-corpus.txt

Runs `clang -E` (line markers on) per TU, splits the token stream into
top-level function bodies by brace matching, and marks a body used when it is
reachable from the main file: main-file bodies are roots, as are function
names that appear outside any body other than as a call (initializers such
as `.fn = foo`); a used body makes every function it names used.  Prints per
TU: tokens, share from headers, share in header bodies, share in header
bodies never reached, share in main-file bodies.  An estimate (lexing by
regex, no macro-level view), good to a few percent.
"""
import collections, importlib.util, os, re, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
spec = importlib.util.spec_from_file_location('vo', R + '/bench/vcc-obj.py')
vo = importlib.util.module_from_spec(spec); spec.loader.exec_module(vo)

TOK = re.compile(r'[A-Za-z_]\w*|\d[\w.]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|->|\+\+|--|<<=|>>=|\.\.\.'
                 r'|[<>=!&|^+\-*/%]=|&&|\|\||<<|>>|##|\S')
IDENT = re.compile(r'[A-Za-z_]\w*$')
KW = set('if while for switch return sizeof typeof __typeof__ __attribute__ asm __asm__ do else case'.split())


def analyse(obj, args):
    p = subprocess.run([vo.CL] + [a for a in args if a != '-P'] + ['-E', '-o', '-', '-w'],
                       cwd=vo.B, capture_output=True, text=True)
    toks, hdr = [], False
    for line in p.stdout.split('\n'):
        if line.startswith('# '):
            m = re.match(r'# \d+ "([^"]*)"', line)
            if m:
                hdr = not m[1].endswith('.c')
            continue
        toks += [(t, hdr) for t in TOK.findall(line)]
    n = len(toks)
    funcs = []			# (name, first, last, from a header)
    depth = par = 0
    name, eq, i = None, False, 0
    while i < n:
        t = toks[i][0]
        if t == '(':
            if depth == 0 and par == 0 and name is None and i and IDENT.match(toks[i - 1][0]) and toks[i - 1][0] not in KW:
                name = toks[i - 1][0]
            par += 1
        elif t == ')':
            par -= 1
        elif depth == 0 and par == 0 and t == '=':
            eq = True
        elif depth == 0 and par == 0 and t == ';':
            name, eq = None, False
        elif t == '{':
            prev = toks[i - 1][0]
            if depth == 0 and par == 0 and name and not eq and (prev == ')' or not IDENT.match(prev)):
                d, j = 0, i
                while j < n:
                    d += toks[j][0] == '{'
                    d -= toks[j][0] == '}'
                    if d == 0:
                        break
                    j += 1
                funcs.append((name, i, j, toks[i][1]))
                i, name, eq = j + 1, None, False
                continue
            depth += 1
        elif t == '}':
            depth -= 1
        i += 1
    inbody = bytearray(n)
    byname = collections.defaultdict(list)
    for f in funcs:
        byname[f[0]].append(f)
        inbody[f[1]:f[2] + 1] = b'\1' * (f[2] - f[1] + 1)
    used, work = set(), []

    def use(nm):
        if nm in byname and nm not in used:
            used.add(nm)
            work.append(nm)
    for f in funcs:
        if not f[3]:
            use(f[0])
    for k, (t, _) in enumerate(toks):
        if not inbody[k] and t in byname and k + 1 < n and toks[k + 1][0] != '(':
            use(t)
    while work:
        for _, s, e, _ in byname[work.pop()]:
            for k in range(s + 1, e):
                use(toks[k][0])
    size = lambda f: f[2] - f[1] + 1
    return dict(obj=obj, n=n, hdr=sum(h for _, h in toks),
                hb=sum(size(f) for f in funcs if f[3]),
                hbu=sum(size(f) for f in funcs if f[3] and f[0] not in used),
                mb=sum(size(f) for f in funcs if not f[3]),
                nh=sum(1 for f in funcs if f[3]), nhu=sum(1 for f in funcs if f[3] and f[0] not in used))


def main():
    av, j = sys.argv[1:], 6
    if '-j' in av:
        k = av.index('-j'); j = int(av[k + 1]); del av[k:k + 2]
    tus = vo.c_tus(av[0] if av else None)
    if not av:
        want = [l.split()[0] for l in open(R + '/bench/vbench-corpus.txt') if l.strip() and not l.startswith('#')]
        d = dict(tus)
        tus = [(o, d[o]) for o in want if o in d]
    with ThreadPoolExecutor(j) as ex:
        res = list(ex.map(lambda t: analyse(*t), tus))
    pc = lambda a, b: 100 * a / b if b else 0
    print('%-40s %8s %6s %7s %7s %6s  unused/header fns' % ('TU', 'tokens', 'hdr', 'hbody', 'unused', 'main'))
    T = collections.Counter()
    for r in res:
        print('%-40s %8d %5.1f%% %6.1f%% %6.1f%% %5.1f%%  %5d/%5d' % (r['obj'], r['n'], pc(r['hdr'], r['n']), pc(r['hb'], r['n']),
              pc(r['hbu'], r['n']), pc(r['mb'], r['n']), r['nhu'], r['nh']))
        T.update({k: v for k, v in r.items() if k != 'obj'})
    n = T['n']
    print('all %d TUs, %d tokens: from headers %.1f%%, header bodies %.1f%%, never-used header bodies %.1f%%, main-file bodies %.1f%%'
          % (len(res), n, pc(T['hdr'], n), pc(T['hb'], n), pc(T['hbu'], n), pc(T['mb'], n)))


main()
