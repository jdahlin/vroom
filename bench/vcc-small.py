#!/usr/bin/env python3
"""Run small external compiler test suites on x86-64 Linux.

Clang is the filter: it must compile and exit zero; c-testsuite also
requires its checked-in combined stdout/stderr.  vcc emits assembly; clang
assembles and links it against the host libc.  chibicc's shared test support
is built by clang, as in its Makefile.
"""
import argparse
import concurrent.futures
import os
import platform
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path

from hostenv import BUILD, WORK, llvm

CLANG = llvm('clang')
VPP = str(Path(BUILD) / 'vpp')
SUITES = Path(WORK) / 'testsuites'
FLAGS = ['--target=x86_64-linux-gnu', '-std=c11', '-O2']


def invoke(args, **kwargs):
    try:
        return subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              timeout=20, **kwargs)
    except subprocess.TimeoutExpired:
        return None


def include_flags():
    p = subprocess.run([CLANG, *FLAGS, '-E', '-x', 'c', '-', '-v'],
                       input=b'', stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, check=True)
    lines = p.stderr.decode().splitlines()
    start = lines.index('#include <...> search starts here:') + 1
    end = lines.index('End of search list.', start)
    return [item for line in lines[start:end] for item in ('-isystem', line.strip())]


def test(path, inc, suite):
    name = path.name
    expected = path.with_suffix('.c.expected')
    if suite == 'c-testsuite' and not expected.is_file():
        return name, 'skip', 'missing expected output'
    with tempfile.TemporaryDirectory(prefix='vcc-small-') as d:
        exe, asm = str(Path(d) / 'test'), str(Path(d) / 'test.s')
        extra = []
        if suite == 'chibicc':
            root = SUITES / 'chibicc'
            extra = ['-I', str(root / 'include'), '-I', str(root / 'test')]
        common = []
        if suite == 'chibicc':
            common = [str(Path(d) / 'common.o')]
            p = invoke([CLANG, '-x', 'c', '-c', str(root / 'test/common'), '-o', common[0]])
            if p is None or p.returncode:
                return name, 'skip', 'clang could not build suite support'
        p = invoke([CLANG, *FLAGS, *extra, str(path), *common, '-pthread', '-o', exe])
        if p is None or p.returncode:
            return name, 'skip', 'clang compile/link: ' + ('timeout' if p is None else p.stdout.decode(errors='replace')[-500:])
        p = invoke([exe], stdin=subprocess.DEVNULL, cwd=d)
        if p is None or p.returncode or (suite == 'c-testsuite' and p.stdout != expected.read_bytes()):
            return name, 'skip', 'clang result differs from suite expectation'
        reference = p.stdout
        p = invoke([VPP, '-S', *FLAGS, '-nostdinc', *extra, *inc, str(path), '-o', asm])
        if p is None or p.returncode:
            return name, 'fail', 'vcc: ' + ('timeout' if p is None else p.stdout.decode(errors='replace')[-500:])
        p = invoke([CLANG, '--target=x86_64-linux-gnu', asm, *common, '-pthread', '-o', exe])
        if p is None or p.returncode:
            return name, 'fail', 'assembly/link: ' + ('timeout' if p is None else p.stdout.decode(errors='replace')[-500:])
        p = invoke([exe], stdin=subprocess.DEVNULL, cwd=d)
        if p is None or p.returncode or p.stdout != reference:
            return name, 'fail', 'runtime: ' + ('timeout' if p is None else f'exit {p.returncode}, output {p.stdout[:200]!r}')
        return name, 'pass', ''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--suite', choices=['c-testsuite', 'chibicc'], default='c-testsuite')
    parser.add_argument('-j', type=int, default=6)
    parser.add_argument('-v', action='store_true')
    parser.add_argument('pattern', nargs='?', default='')
    args = parser.parse_args()
    if platform.system() != 'Linux' or platform.machine() != 'x86_64':
        parser.error('requires an x86-64 Linux host')
    root = SUITES / ('c-testsuite/tests/single-exec' if args.suite == 'c-testsuite' else 'chibicc/test')
    if not root.is_dir():
        parser.error(f'fetch the suite with bench/fetch-testsuites.sh {args.suite}')
    files = sorted(p for p in root.glob('*.c') if args.pattern in p.name)
    if not files:
        parser.error('no matching tests')
    inc = include_flags()
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.j) as pool:
        results = list(pool.map(lambda p: test(p, inc, args.suite), files))
    counts = Counter(status for _, status, _ in results)
    for name, status, why in results:
        if status == 'fail' or (args.v and status == 'skip'):
            print(f'{status.upper()} {name}: {why}')
    print(f'{args.suite}: {counts["pass"]} passed, {counts["fail"]} failed, {counts["skip"]} skipped')
    return int(bool(counts['fail']))


if __name__ == '__main__':
    sys.exit(main())
