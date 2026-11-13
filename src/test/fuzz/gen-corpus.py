#!/usr/bin/env python3
"""gen-corpus.py: seed corpora for fuzz-lex and fuzz-pp from kernel headers.

  src/test/fuzz/gen-corpus.py [--linux DIR] [--out DIR] [-n N]

Takes small snippets (at most ~1.5 KB, cut at line boundaries outside
continued lines) of headers in work/linux (include/linux, include/uapi,
arch/arm64/include) and writes, deterministically:

  OUT/lex/   raw snippets, plus the hand-written seeds of fuzz/seeds/
  OUT/pp/    snippets with #include lines turned into includes of the fake
             headers fuzz-pp serves ("a.h", "b.h", <c.h>), and multi-file
             inputs (main \\x01 a.h \\x01 b.h, see fuzz-pp.c)

OUT defaults to build/src/fuzz/corpus.  Nothing here is committed: the
kernel text stays in work/.
"""
import argparse, hashlib, os, re, sys

H = os.path.dirname(os.path.abspath(__file__))
R = os.path.dirname(os.path.dirname(os.path.dirname(H)))	# src/test/fuzz/ -> the checkout


def default_linux():
    for root in (R, os.path.dirname(os.path.dirname(os.path.dirname(R)))):	# worktree: the main checkout
        d = f'{root}/work/linux'
        if os.path.isdir(f'{d}/include/linux'):
            return d
    return f'{R}/work/linux'


def snippets(text, maxlen=1500):
    """consecutive pieces of text, cut at line ends that are not continued"""
    lines = text.split('\n')
    cur, n = [], 0
    for l in lines:
        cur.append(l)
        n += len(l) + 1
        if n >= maxlen and not l.endswith('\\'):
            yield '\n'.join(cur) + '\n'
            cur, n = [], 0
    if cur and n > 40:
        yield '\n'.join(cur) + '\n'


INC = re.compile(r'^(\s*#\s*include(?:_next)?\s*)([<"][^>"]*[>"])', re.M)


def fake_includes(s, k):
    """#include lines: the fake headers (k picks which), else they are fatal"""
    names = ['"a.h"', '"b.h"', '<c.h>']
    i = [k]

    def sub(m):
        i[0] += 1
        return m.group(1) + names[i[0] % 3]
    return INC.sub(sub, s)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--linux', default=default_linux())
    ap.add_argument('--out', default=f'{R}/build/src/fuzz/corpus')
    ap.add_argument('-n', type=int, default=1500, help='snippets per corpus')
    a = ap.parse_args()
    srcs = []
    for sub in ('include/linux', 'include/uapi/linux', 'arch/arm64/include/asm', 'include/asm-generic'):
        d = f'{a.linux}/{sub}'
        if os.path.isdir(d):
            srcs += sorted(f'{d}/{f}' for f in os.listdir(d) if f.endswith('.h'))
    if not srcs:
        sys.exit(f'gen-corpus: no headers under {a.linux}')
    pieces = []
    for f in srcs:
        try:
            t = open(f, encoding='latin-1').read()
        except OSError:
            continue
        ps = list(snippets(t))
        # a spread over each header: macro-dense middles, not only licence headers
        pieces += ps[1::max(1, len(ps) // 3)][:3] if len(ps) > 1 else ps
    # deterministic thinning to n
    pieces.sort(key=lambda s: hashlib.sha1(s.encode('latin-1')).digest())
    pieces = pieces[:a.n]
    for kind in ('lex', 'pp'):
        os.makedirs(f'{a.out}/{kind}', exist_ok=True)

    def put(kind, data):
        h = hashlib.sha1(data).hexdigest()[:16]
        with open(f'{a.out}/{kind}/{h}', 'wb') as f:
            f.write(data)
    for i, s in enumerate(pieces):
        b = s.encode('latin-1')
        put('lex', b)
        p = fake_includes(s, i)
        if i % 4 == 3 and i + 2 < len(pieces):
            # main with two headers taken from the next snippets
            a1 = fake_includes(pieces[i + 1], i + 1)
            b1 = fake_includes(pieces[i + 2], i + 2)
            p = '#include "a.h"\n' + p + '\x01' + a1 + '\x01' + '#pragma once\n' + b1
        put('pp', p.encode('latin-1'))
    for kind in ('lex', 'pp'):
        for d in (f'{H}/seeds/{kind}', f'{H}/regress/{kind}'):
            if os.path.isdir(d):
                for f in sorted(os.listdir(d)):
                    put(kind, open(f'{d}/{f}', 'rb').read())
    # every pp seed is a lexer seed too
    for f in sorted(os.listdir(f'{a.out}/pp')):
        put('lex', open(f'{a.out}/pp/{f}', 'rb').read())
    for kind in ('lex', 'pp'):
        print(f'{a.out}/{kind}: {len(os.listdir(a.out + "/" + kind))} inputs')


if __name__ == '__main__':
    main()
