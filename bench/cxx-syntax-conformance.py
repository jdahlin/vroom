#!/usr/bin/env python3
"""The C++ syntax tree against the grammar the views are generated from.

Runs `vpp -x c++ -fsyntax-only -fcxx-dump=syntax-conformance` on every C++
file of src/test/cxx (and, with --suites, clang's test/CXX, SemaCXX and
SemaTemplate from work/testsuites/llvm) and fails on any child node its
parent's productions do not allow, or a list nested in a list of its kind:
the generated accessors (src/include/frontend/cplusplus/syntax-views.inc,
src/tools/gen-syntax-views.py) read the tree as the grammar has it.  A
difference is either the parser's (fix it) or a choice of the parser the
generator lists in KNOWN_CHILDREN, ALWAYS_NODE or NOT_LISTS.

Usage: bench/cxx-syntax-conformance.py [-j N] [--suites] [--bin-dir DIR]
"""
import argparse
import collections
import os
import subprocess
import sys
from multiprocessing import Pool

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def run(job):
    vpp, path = job
    p = subprocess.run([vpp, '-x', 'c++', '-std=c++20', '-fsyntax-only',
                        '-fcxx-dump=syntax-conformance', path],
                       stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                       timeout=120)
    return [line.split() for line in p.stdout.decode('utf-8', 'replace').splitlines()
            if line.strip()]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-j', type=int, default=os.cpu_count())
    ap.add_argument('--suites', action='store_true')
    ap.add_argument('--bin-dir', default=os.environ.get('VCC_BIN'))
    a = ap.parse_args()
    bin_dir = a.bin_dir or os.path.join(R, 'build', 'src')
    if not os.path.exists(os.path.join(bin_dir, 'vpp')) and os.path.exists(os.path.join(R, 'build', 'linux', 'src', 'vpp')):
        bin_dir = os.path.join(R, 'build', 'linux', 'src')
    vpp = os.path.join(bin_dir, 'vpp')
    roots = [os.path.join(R, 'src', 'test', 'cxx')]
    if a.suites:
        t = os.path.join(R, 'work', 'testsuites', 'llvm', 'clang', 'test')
        roots += [os.path.join(t, d) for d in ('CXX', 'SemaCXX', 'SemaTemplate')]
    files = sorted(os.path.join(d, f) for root in roots for d, _, fs in os.walk(root)
                   for f in fs if f.endswith('.cpp'))
    with Pool(a.j) as pool:
        results = pool.map(run, [(vpp, f) for f in files], chunksize=8)
    counts = collections.Counter()
    first = {}
    for rows in results:
        for row in rows:
            if len(row) != 4:
                continue
            key = tuple(row[:3])
            counts[key] += 1
            first.setdefault(key, row[3])
    for key, n in counts.most_common():
        print('%6d %s %s %s  (first at %s)' % (n, key[0], key[1], key[2], first[key]))
    print('cxx-syntax-conformance: %d files, %d nodes not as the grammar has them' %
          (len(files), sum(counts.values())))
    sys.exit(1 if counts else 0)


if __name__ == '__main__':
    main()
