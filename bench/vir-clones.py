#!/usr/bin/env python3
"""Semantic clones in vcc's own sources, found through VIR.

Every C source of the toolchain is compiled as C11 by vcc at -O2 with
-fvir-dump; each function's VIR, after the builder's simplifier, value
numbering and canonicalization, is fingerprinted:

  - unconditional jumps without arguments are dropped (block layout noise);
  - operands of commutative operations are sorted, a compare with swapped
    operands becomes the mirrored predicate (a < b == b > a);
  - symbols are abstracted (VIR numbers them per TU), slots too;
  - each value gets a label from its operation and its operands' labels,
    refined for ROUNDS rounds (Weisfeiler-Lehman), so the label of a value
    describes the computation that produces it, not its name or position.

A function's fingerprint is the multiset of its final labels.  Equal
fingerprints are clones that may differ in syntax (for vs while, a+b vs
b+a, temporaries, x*2 vs x<<1 once the simplifier has seen them); a
weighted Jaccard similarity of at least --near over the multisets, with
MinHash LSH for candidates, is a near clone.

Not seen: functions VIR gives up on (they go to the direct emitter), the
C++ front end (vcc compiles vcc's C, not its C++), and which function a
call calls (symbols are abstracted; --min-insns keeps small wrappers out).

  bench/vir-clones.py -j 6                 # report, longest first
  bench/vir-clones.py -j 6 --only ld/ arch/a64/ld/
  bench/vir-clones.py --json out.json      # machine-readable groups
"""
import argparse
import collections
import concurrent.futures
import hashlib
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'src')
SKIP = ('frontend/cplusplus/', 'test/', 'port/wasm/', 'arch/wasm/',
        'platform/wasm/', 'runtime/', 'headers/', 'tools/', 'ld/wasmld.cpp')
ROUNDS = 3
COMMUTATIVE = {'add', 'mul', 'and', 'or', 'xor', 'fadd', 'fmul', 'icmp.eq',
               'icmp.ne', 'fcmp.oeq', 'fcmp.une', 'smin', 'smax', 'umin',
               'umax'}
MIRROR = {}
for a, b in (('slt', 'sgt'), ('sle', 'sge'), ('ult', 'ugt'), ('ule', 'uge'),
             ('olt', 'ogt'), ('ole', 'oge')):
    for p in ('icmp.', 'fcmp.'):
        MIRROR[p + a], MIRROR[p + b] = p + b, p + a

TOK = re.compile(r'v\d+|b\d+|@s\d+(?:[+-]\d+)?|\$\d+(?:[+-]\d+)?|-?\d+|'
                 r'[A-Za-z_.%?][\w.%?]*|\S')
DEF = re.compile(r'\s*(v\d+):(\S+) = (.*)$')
BLOCK = re.compile(r'(b\d+)(?:\((.*)\))?:$')


def h(*parts):
    return hashlib.blake2b(repr(parts).encode(), digest_size=8).hexdigest()


def sources(only):
    out = []
    for d, _, fs in os.walk(SRC):
        for f in fs:
            if not f.endswith('.cpp'):
                continue
            rel = os.path.relpath(os.path.join(d, f), SRC)
            if rel.startswith(SKIP) or rel in SKIP:
                continue
            if only and not rel.startswith(tuple(only)):
                continue
            out.append(rel)
    return sorted(out)


def dump(vpp, rel):
    cmd = [vpp, '-x', 'c', '-std=c11', '-O2', '-D_DARWIN_C_SOURCE',
           '-Iinclude', '-DVCC_INCDIR="x"', '-fvir-dump', '-S',
           '-o', '/dev/null', rel]
    r = subprocess.run(cmd, cwd=SRC, capture_output=True, text=True,
                       errors='replace')
    return rel, r.returncode, r.stderr


def split_functions(text):
    """(name, [lines]) for every dumped function."""
    name, body = None, None
    for line in text.splitlines():
        if line.startswith('; ') and body is None:
            name = line[2:].strip()
        elif line.startswith('func '):
            body = []
        elif line == '}' and body is not None:
            yield name, body
            name, body = None, None
        elif body is not None:
            body.append(line)


def fingerprint(lines, loose):
    """(multiset of labels, instruction count) of one function."""
    nodes = []          # [op, operand value names, own value name or None]
    for line in lines:
        s = line.strip()
        if not s or s.startswith(('slot ', ';')):
            continue
        m = BLOCK.match(s)
        if m:
            for i, p in enumerate((m.group(2) or '').split(',')):
                if p.strip():
                    v, t = p.strip().split(':', 1)
                    nodes.append([('bparam', t, i), [], v])
            continue
        m = DEF.match(s)
        own, typ, rest = (m.group(1), m.group(2), m.group(3)) if m \
            else (None, None, s)
        toks = TOK.findall(rest)
        op = toks[0]
        if op == 'br' and len(toks) == 2:
            continue                    # plain jump: layout only
        shape, refs = [op, typ], []
        for t in toks[1:]:
            if t[0] == 'v' and t[1:].isdigit():
                shape.append('_')
                refs.append(t)
            elif t[0] == 'b' and t[1:].isdigit():
                shape.append('B')
            elif t[0] == '@':
                shape.append('@')
            elif t[0] == '$':
                shape.append('$')
            elif loose and t.lstrip('-').isdigit():
                shape.append('#')
            else:
                shape.append(t)
        if len(refs) == 2 and op in MIRROR:
            nodes.append([(MIRROR[op],) + tuple(shape[1:]), refs[::-1], own,
                          'mirror', tuple(shape)])
        else:
            nodes.append([tuple(shape), refs, own,
                          'comm' if op in COMMUTATIVE else None, None])
    label = {}
    lab = []
    for n in nodes:
        lab.append(h(n[0]))
    for _ in range(ROUNDS):
        for n, l in zip(nodes, lab):
            if n[2]:
                label[n[2]] = l
        new = []
        for n, l in zip(nodes, lab):
            ops = [label.get(r, '?') for r in n[1]]
            kind = n[3] if len(n) > 3 else None
            if kind == 'comm':
                ops.sort()
                new.append(h(n[0], ops))
            elif kind == 'mirror':
                # a < b and b > a get the same label: use the form whose
                # operand labels sort first
                fwd = (n[4], ops[::-1])
                rev = (n[0], ops)
                new.append(h(*min(fwd, rev)))
            else:
                new.append(h(n[0], ops))
        lab = new
    return collections.Counter(lab), len(nodes)


def jaccard(a, b):
    inter = sum((a & b).values())
    return inter / max(sum((a | b).values()), 1)


def minhash(c, k=32):
    items = [f'{l}#{i}' for l, n in c.items() for i in range(n)]
    return tuple(min(int(h(s, j), 16) for s in items) for j in range(k))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('-j', type=int, default=6)
    ap.add_argument('--bin-dir', default=os.path.join(ROOT, 'build/src'))
    ap.add_argument('--only', nargs='*', default=[])
    ap.add_argument('--min-insns', type=int, default=20)
    ap.add_argument('--near', type=float, default=0.85)
    ap.add_argument('--loose', action='store_true',
                    help='constants abstracted too (type-2 on constants)')
    ap.add_argument('--top', type=int, default=40)
    ap.add_argument('--json')
    a = ap.parse_args()
    vpp = os.path.join(a.bin_dir, 'vpp')

    funcs = []          # (file, name, counter, ninsns)
    failed = []
    with concurrent.futures.ThreadPoolExecutor(a.j) as ex:
        for rel, rc, err in ex.map(lambda r: dump(vpp, r), sources(a.only)):
            if rc:
                failed.append(rel)
            for name, body in split_functions(err):
                c, n = fingerprint(body, a.loose)
                if n >= a.min_insns:
                    funcs.append((rel, name, c, n))

    # one copy per (name, fingerprint): a static inline from a header is
    # emitted in every TU that uses it, that is not a clone
    # the shared walker compiled a second time under WK_PREFIX (virbuild_)
    # is one source, also not a clone
    seen, uniq = set(), []
    for f in funcs:
        key = (f[1].removeprefix('virbuild_'), frozenset(f[2].items()))
        if key not in seen:
            seen.add(key)
            uniq.append(f)

    exact = collections.defaultdict(list)
    for f in uniq:
        exact[frozenset(f[2].items())].append(f)
    groups = [g for g in exact.values() if len(g) > 1]
    groups.sort(key=lambda g: -g[0][3] * (len(g) - 1))

    # near clones: MinHash LSH (8 bands of 4), then the real similarity
    reps = [g[0] for g in exact.values()]
    sig = [minhash(f[2]) for f in reps]
    buckets = collections.defaultdict(list)
    for i, s in enumerate(sig):
        for b in range(8):
            buckets[(b, s[b * 4:b * 4 + 4])].append(i)
    near, done = [], set()
    for idx in buckets.values():
        for x in range(len(idx)):
            for y in range(x + 1, len(idx)):
                p = (idx[x], idx[y])
                if p in done:
                    continue
                done.add(p)
                fa, fb = reps[p[0]], reps[p[1]]
                if fa[1].removeprefix('virbuild_') == fb[1].removeprefix('virbuild_'):
                    continue
                s = jaccard(fa[2], fb[2])
                if s >= a.near:
                    near.append((s, fa, fb))
    near.sort(key=lambda t: -t[0] * min(t[1][3], t[2][3]))

    total = sum(f[3] for f in uniq)
    red = sum(g[0][3] * (len(g) - 1) for g in groups)
    print(f'{len(uniq)} functions of >= {a.min_insns} VIR instructions '
          f'({total} instructions) from {len(sources(a.only)) - len(failed)} files'
          f' ({len(failed)} not compiled as C)')
    print(f'identical after canonicalization: {len(groups)} groups, '
          f'{red} redundant instructions ({100.0 * red / max(total, 1):.2f} %)')
    print(f'near clones (similarity >= {a.near}): {len(near)} pairs\n')
    print('identical:')
    for g in groups[:a.top]:
        print(f'  {g[0][3]:5d} insns x{len(g)}')
        for f in g:
            print(f'        {f[0]}: {f[1]}')
    print('\nnear:')
    for s, fa, fb in near[:a.top]:
        print(f'  {s:.2f}  {fa[3]:5d} {fa[0]}: {fa[1]}\n'
              f'        {fb[3]:5d} {fb[0]}: {fb[1]}')
    if a.json:
        with open(a.json, 'w') as fp:
            json.dump({
                'identical': [[{'file': f[0], 'function': f[1], 'insns': f[3]}
                               for f in g] for g in groups],
                'near': [{'similarity': round(s, 3),
                          'a': {'file': fa[0], 'function': fa[1], 'insns': fa[3]},
                          'b': {'file': fb[0], 'function': fb[1], 'insns': fb[3]}}
                         for s, fa, fb in near],
                'not_compiled': failed}, fp, indent=1)
    return 0


if __name__ == '__main__':
    sys.exit(main())
