#!/usr/bin/env python3
"""Check cproc C tests on Linux against clang acceptance.

This is not cproc's exact QBE-IR/diagnostic-text regression test: .qbe
cases check compilation and assembly, .err cases check rejection, and
.pp cases check tokens against upstream expected output. Runtime semantics
of QBE cases are not checked. Target-suffixed cases run only on that target.
"""
import argparse
import concurrent.futures
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path

from hostenv import BUILD, WORK, llvm


def invoke(args):
    try:
        return subprocess.run(args, capture_output=True, timeout=20)
    except subprocess.TimeoutExpired:
        return None


def detail(p):
    return 'timeout' if p is None else f'exit {p.returncode}: ' + p.stderr.decode(errors='replace')[-700:]


def test(path, target):
    root = Path(WORK) / 'testsuites/cproc/test'
    name = str(path.relative_to(root))
    arch = {'x86_64': 'x86_64-sysv', 'aarch64': 'aarch64'}[target]
    if '+' in path.stem and path.stem.rsplit('+', 1)[1] != arch:
        return name, 'skip', 'other target'
    clang, vpp = llvm('clang'), str(Path(BUILD) / 'vpp')
    flags = [f'--target={target}-linux-gnu', '-std=c23']
    with tempfile.TemporaryDirectory(prefix='vcc-cproc-') as d:
        asm, obj, ref = [str(Path(d) / n) for n in ('out.s', 'out.o', 'ref.i')]
        if path.with_suffix('.qbe').is_file():
            p = invoke([clang, *flags, '-c', str(path), '-o', obj])
            if p is None or p.returncode:
                return name, 'skip', 'clang compile: ' + detail(p)
            p = invoke([vpp, *flags, '-S', str(path), '-o', asm])
            if p is None or p.returncode:
                return name, 'fail', 'vcc compile: ' + detail(p)
            p = invoke([clang, flags[0], '-c', asm, '-o', obj])
            if p is None or p.returncode:
                return name, 'fail', 'assemble: ' + detail(p)
        elif path.with_suffix('.pp').is_file():
            expected = str(path.with_suffix('.pp'))
            p = invoke([clang, *flags, '-E', '-P', str(path), '-o', ref])
            if p is None or p.returncode:
                return name, 'skip', 'clang preprocess: ' + detail(p)
            p = invoke([vpp, *flags, '--cmp', expected, ref])
            if p is None or p.returncode:
                return name, 'skip', 'clang tokens differ from upstream expectation: ' + detail(p)
            p = invoke([vpp, *flags, '--cmp', expected, str(path)])
            if p is None or p.returncode:
                return name, 'fail', 'preprocessor tokens: ' + detail(p)
        elif path.with_suffix('.err').is_file():
            p = invoke([clang, *flags, '-fsyntax-only', str(path)])
            if p is None or p.returncode <= 0:
                return name, 'skip', 'clang did not reject normally: ' + detail(p)
            p = invoke([vpp, *flags, '-fsyntax-only', str(path)])
            if p is None or p.returncode <= 0:
                return name, 'fail', 'vcc did not reject normally: ' + detail(p)
        else:
            return name, 'skip', 'no upstream expectation'
    return name, 'pass', ''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', choices=['x86_64', 'aarch64'], default='x86_64')
    parser.add_argument('-j', type=int, default=6)
    parser.add_argument('-v', action='store_true')
    parser.add_argument('pattern', nargs='?', default='')
    args = parser.parse_args()
    root = Path(WORK) / 'testsuites/cproc/test'
    files = sorted(p for p in root.rglob('*.c') if args.pattern in str(p.relative_to(root)))
    if not files:
        parser.error('no matching tests; fetch with bench/fetch-testsuites.sh cproc')
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.j) as pool:
        results = list(pool.map(lambda p: test(p, args.target), files))
    for name, status, why in results:
        if status == 'fail' or (args.v and status == 'skip'):
            print(f'{status.upper()} {name}: {why}')
    counts = Counter(status for _, status, _ in results)
    print(f'cproc compile/pp/reject ({args.target}): {counts["pass"]} passed, '
          f'{counts["fail"]} failed, {counts["skip"]} skipped (no QBE IR or runtime comparison)')
    return int(bool(counts['fail']))


if __name__ == '__main__':
    sys.exit(main())
