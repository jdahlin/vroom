#!/usr/bin/env python3
"""
bench/cxx-mangle-unit.py: the Itanium mangler's unit tests
(src/test/cxx/mangle, one file per section of the ABI's mangling
grammar).  Each `// MANGLE: SYMBOL` line of a file is a symbol vpp -c's
object must have (defined or referenced, llvm-nm); `// MANGLE-XFAIL:
SYMBOL reason` one it is known not to have yet (an error once it has it).
--clang also has clang++ -c compile each file and checks that its object
has every expected symbol: the expectations are clang's, not vcc's.  No
clang is needed otherwise.  Fast (<2 s), needs only the checkout.

  bench/cxx-mangle-unit.py [-j N] [--bin-dir DIR] [--clang] [-v] [PATTERN]
"""
import argparse
import concurrent.futures
import os
import re
import subprocess
import sys
import tempfile

B = os.path.dirname(os.path.abspath(__file__))
R = os.path.dirname(B)
TESTS = os.path.join(R, 'src', 'test', 'cxx', 'mangle')
NM = '/usr/lib/llvm-23/bin/llvm-nm'
CLANG = '/usr/lib/llvm-23/bin/clang++'
FLAGS = ['--target=aarch64-linux-gnu', '-nostdinc', '-x', 'c++',
         '-std=c++20', '-fno-exceptions', '-O0', '-w', '-c']
EXPECT = re.compile(r'^//\s*MANGLE(-XFAIL)?:\s*(\S+)', re.M)


def symbols(compiler, src, extra):
    with tempfile.TemporaryDirectory() as tmp:
        obj = os.path.join(tmp, 'x.o')
        p = subprocess.run([compiler] + extra + FLAGS + [src, '-o', obj],
                           capture_output=True, text=True)
        if p.returncode:
            return None, p.stderr.strip()
        n = subprocess.run([NM, obj], capture_output=True, text=True)
        return {l.split()[-1] for l in n.stdout.splitlines() if l.split()}, ''


def check(name, vpp, with_clang):
    src = os.path.join(TESTS, name)
    with open(src) as f:
        text = f.read()
    expected = EXPECT.findall(text)
    problems = []
    have, err = symbols(vpp, src, [])
    if have is None:
        problems.append('vpp -c failed: ' + err.splitlines()[0])
        have = set()
    for xfail, sym in expected:
        if xfail and sym in have:
            problems.append('XPASS ' + sym)
        elif not xfail and sym not in have:
            problems.append('missing ' + sym)
    if with_clang:
        ref, err = symbols(CLANG, src, [])
        if ref is None:
            problems.append('clang failed: ' + err.splitlines()[0])
        else:
            for _, sym in expected:
                if sym not in ref:
                    problems.append('not clang\'s: ' + sym)
    return name, len(expected), problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-j', type=int, default=6)
    ap.add_argument('--bin-dir', default=os.environ.get('VCC_BIN') or
                    os.path.join(R, 'build', 'linux', 'src'))
    ap.add_argument('--clang', action='store_true')
    ap.add_argument('-v', action='store_true')
    ap.add_argument('pattern', nargs='?')
    a = ap.parse_args()
    vpp = os.path.join(a.bin_dir, 'vpp')
    names = sorted(f for f in os.listdir(TESTS) if f.endswith('.cpp') and
                   (not a.pattern or a.pattern in f))
    failed = total = 0
    with concurrent.futures.ThreadPoolExecutor(a.j) as ex:
        for name, count, problems in ex.map(
                lambda n: check(n, vpp, a.clang), names):
            total += count
            if problems:
                failed += 1
                print('FAIL %s' % name)
                for p in problems:
                    print('  ' + p)
            elif a.v:
                print('ok   %s (%d symbols)' % (name, count))
    print('cxx-mangle-unit: %d files, %d symbols, %d failed'
          % (len(names), total, failed))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
