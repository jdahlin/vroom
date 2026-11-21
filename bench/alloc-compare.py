#!/usr/bin/env python3
"""Register allocation quality: vcc against clang and gcc, per function, from -S output.

bench/alloc-compare.py [-j N] [-n N] [--suite DIR|FILE...] [--top K] [--no-inline-off]

Each C file is compiled to x86-64 assembly by vpp, clang and gcc at -O2 with the same
switches (no inlining, no unrolling or vectorization, no CET or unwind tables), so that a
function has the same shape in all three and what differs is selection and allocation.
Per function the metrics are: instructions, stack accesses (rsp/rbp-relative memory
operands that are not argument set-up: the spill proxy), register-to-register moves and
push/pop pairs (callee-saved registers used).  The report sums them per compiler over the
functions all three produced, then lists the functions where vcc is furthest above the
better of clang and gcc.  Static counts: a hot loop weighs the same as straight-line code.
"""
import argparse
import collections
import concurrent.futures as cf
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VPP = os.environ.get('VPP', str(ROOT / 'build/linux/src/vpp'))

COMMON = ['-O2', '-fno-asynchronous-unwind-tables', '-fno-unroll-loops']
TOOLS = {
    'vcc': [VPP, '-S', '-fno-inline', '-fcf-protection=none'] + COMMON,
    'clang': ['clang', '-S', '-fno-inline', '-fcf-protection=none', '-fno-vectorize',
              '-fno-slp-vectorize', '-fno-builtin'] + COMMON,
    'gcc': ['gcc', '-S', '-fno-inline', '-fcf-protection=none', '-fno-tree-vectorize',
            '-fno-builtin', '-fno-ipa-cp', '-fno-ipa-sra', '-fno-reorder-functions'] + COMMON,
}
FUNC = re.compile(r'^([A-Za-z_.$][\w.$]*):\s*(#.*)?$')
STACK = re.compile(r'-?\d*\((%rsp|%rbp)')
MOVE = re.compile(r'^\s*(mov[lqwb]?|movaps|movups|movdqa|movapd|movq)\s+%\w+,\s*%\w+\s*(#.*)?$')


def functions(text):
    """name -> list of instruction lines."""
    out, cur = {}, None
    for line in text.splitlines():
        s = line.strip()
        if s.startswith('.type') and s.endswith('@function'):
            continue
        m = FUNC.match(line)
        if m and not m.group(1).startswith(('.L', 'L')):
            cur = out.setdefault(m.group(1), [])
            continue
        if cur is None or not s or s.startswith(('#', '.')):
            continue
        if s.endswith(':'):  # a local label; another global label ends the function
            if not s.startswith('.L') and not s.startswith('L'):
                cur = None
            continue
        cur.append(line)
    return {k: v for k, v in out.items() if v}


def metrics(lines):
    n = st = mv = pp = 0
    for l in lines:
        n += 1
        t = l.strip()
        if t.startswith(('push', 'pop')) or (STACK.search(l) and not t.startswith(('lea', 'sub', 'add', 'call'))):
            st += 1
        if MOVE.match(l):
            mv += 1
        if l.strip().startswith(('push', 'pop')):
            pp += 1
    return dict(ins=n, stack=st, mov=mv, pp=pp)


def compile_one(args):
    tool, src, flags = args
    with tempfile.TemporaryDirectory() as d:
        out = Path(d) / 'o.s'
        r = subprocess.run(TOOLS[tool] + flags + [str(src), '-o', str(out)],
                           capture_output=True, text=True, timeout=120)
        if r.returncode or not out.exists():
            return None
        return out.read_text()


def one_file(src):
    texts = {t: compile_one((t, src, ['-I' + str(src.parent), '-w'])) for t in TOOLS}
    if any(v is None for v in texts.values()):
        return None
    fns = {t: functions(v) for t, v in texts.items()}
    common = set(fns['vcc']) & set(fns['clang']) & set(fns['gcc'])
    return {f: {t: metrics(fns[t][f]) for t in TOOLS} for f in common}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('paths', nargs='*', default=[str(ROOT / 'src/test/gcc-torture')])
    ap.add_argument('-j', type=int, default=6)
    ap.add_argument('-n', type=int, default=300, help='files, spread evenly')
    ap.add_argument('--top', type=int, default=25)
    ap.add_argument('--min', type=int, default=8, help='fewest instructions of a listed function')
    a = ap.parse_args()
    files = []
    for p in map(Path, a.paths):
        files += sorted(p.rglob('*.c')) if p.is_dir() else [p]
    if len(files) > a.n:
        step = len(files) / a.n
        files = [files[int(i * step)] for i in range(a.n)]
    rows, skipped = {}, 0
    with cf.ThreadPoolExecutor(a.j) as ex:
        for src, res in zip(files, ex.map(one_file, files)):
            if res is None:
                skipped += 1
                continue
            for f, m in res.items():
                rows[(src.name, f)] = m
    tot = {t: collections.Counter() for t in TOOLS}
    for m in rows.values():
        for t in TOOLS:
            tot[t].update(m[t])
    print(f'{len(files) - skipped} files ({skipped} not compiled by all three), {len(rows)} functions')
    print(f'{"":8}{"ins":>9}{"stack":>9}{"mov":>9}{"push/pop":>10}')
    for t in TOOLS:
        c = tot[t]
        print(f'{t:8}{c["ins"]:9}{c["stack"]:9}{c["mov"]:9}{c["pp"]:10}')
    # functions of similar size in all three: the optimizer's folding and inlining differences
    # (vcc 1.5x larger than the better of clang/gcc) are left out, what is left is allocation
    sim = [m for m in rows.values() if m['vcc']['ins'] <= 1.5 * min(m['clang']['ins'], m['gcc']['ins'])]
    print(f'\nthe {len(sim)} functions where vcc is at most 1.5x the size of the smaller of clang/gcc:')
    print(f'{"":8}{"ins":>9}{"stack":>9}{"mov":>9}{"push/pop":>10}')
    for t in TOOLS:
        c = collections.Counter()
        for m in sim:
            c.update(m[t])
        print(f'{t:8}{c["ins"]:9}{c["stack"]:9}{c["mov"]:9}{c["pp"]:10}')
    for k, base in (('stack', 'stack accesses'), ('mov', 'reg moves'), ('ins', 'instructions')):
        sc = sorted(((m['vcc'][k] - min(m['clang'][k], m['gcc'][k]), key, m) for key, m in rows.items()
                     if m['vcc']['ins'] >= a.min and m['vcc']['ins'] <= 1.5 * min(m['clang']['ins'], m['gcc']['ins'])), key=lambda x: -x[0])
        print(f'\nsimilar-size functions with the most {base} above the better of clang/gcc:')
        for d, (src, f), m in sc[:a.top]:
            print(f'  {d:+4d}  {src}:{f}   vcc {m["vcc"][k]} clang {m["clang"][k]} gcc {m["gcc"][k]}'
                  f'   (ins {m["vcc"]["ins"]}/{m["clang"]["ins"]}/{m["gcc"]["ins"]})')


if __name__ == '__main__':
    main()
