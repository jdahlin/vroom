#!/usr/bin/env python3
"""Run small external compiler test suites on x86-64 or arm64 Linux.

Clang is the filter: it must compile and meet the suite runtime expectation.
c-testsuite requires its checked-in combined stdout/stderr. LLVM uses its
traditional combined output with an appended exit status; this covers only
immediate C files in UnitTests and Regression/C, whose default fpcmp checks
are exact (no floating-point tolerance or whitespace flags). vcc emits
assembly; clang assembles and links it against the target libc.  chibicc's shared test support
is built by clang, as in its Makefile.
"""
import argparse
import concurrent.futures
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
FLAGS = []
TARGET = ''
RUNNER = []


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
    if suite == 'llvm' and 'aarch64' in TARGET and (
            name == 'blockstret.c' or name.startswith('ms_struct')):
        return name, 'skip', 'excluded on AArch64 by suite CMakeLists.txt'
    expected = path.with_suffix('.c.expected') if suite == 'c-testsuite' else path.with_suffix('.reference_output')
    if suite == 'llvm':
        for suffix in ('.reference_output.little-endian', '.reference_output.Linux', '.reference_output'):
            candidate = path.with_suffix(suffix)
            if candidate.is_file():
                expected = candidate
                break
        if not expected.is_file():
            return name, 'skip', 'no reference output'
    if suite == 'c-testsuite' and not expected.is_file():
        return name, 'skip', 'missing expected output'
    with tempfile.TemporaryDirectory(prefix='vcc-small-') as d:
        exe, asm = str(Path(d) / 'test'), str(Path(d) / 'test.s')
        extra = []
        flags = FLAGS + (['-Wno-implicit-function-declaration', '-Wno-implicit-int',
                          '-Wno-error=incompatible-pointer-types']
                         if suite == 'llvm' else ['-std=gnu11' if suite == 'chibicc' else '-std=c11'])
        if suite == 'llvm':
            extra = ['-lm']
        if suite == 'chibicc':
            root = SUITES / 'chibicc'
            extra = ['-I', str(root / 'include'), '-I', str(root / 'test')]
        common = []
        if suite == 'chibicc':
            common = [str(Path(d) / 'common.o')]
            p = invoke([CLANG, TARGET, '-x', 'c', '-c', str(root / 'test/common'), '-o', common[0]])
            if p is None or p.returncode:
                return name, 'skip', 'clang could not build suite support'
        p = invoke([CLANG, *flags, str(path), *extra, *common, '-pthread', '-o', exe])
        if p is None or p.returncode:
            return name, 'skip', 'clang compile/link: ' + ('timeout' if p is None else p.stdout.decode(errors='replace')[-500:])
        input_path = path.with_suffix('.reference_input') if suite == 'llvm' else None
        stdin = input_path.read_bytes() if input_path and input_path.is_file() else b''
        p = invoke([*RUNNER, exe], input=stdin, cwd=d)
        if p is None:
            return name, 'skip', 'clang runtime timeout'
        reference = p.stdout
        if suite == 'llvm':
            reference += f'exit {p.returncode if p.returncode >= 0 else 128 - p.returncode}\n'.encode()
        if ((suite != 'llvm' and p.returncode) or
                (suite in ('c-testsuite', 'llvm') and reference != expected.read_bytes())):
            return name, 'skip', f'clang result differs from suite expectation (exit {p.returncode})'

        p = invoke([VPP, '-S', *flags, '-nostdinc', *(extra if suite == 'chibicc' else []), *inc, str(path), '-o', asm])
        if p is None or p.returncode:
            return name, 'fail', 'vcc: ' + ('timeout' if p is None else p.stdout.decode(errors='replace')[-500:])
        p = invoke([CLANG, TARGET, asm, *extra, *common, '-pthread', '-o', exe])
        if p is None or p.returncode:
            return name, 'fail', 'assembly/link: ' + ('timeout' if p is None else p.stdout.decode(errors='replace')[-500:])
        p = invoke([*RUNNER, exe], input=stdin, cwd=d)
        output = p.stdout if p is not None else b''
        if p is not None and suite == 'llvm':
            output += f'exit {p.returncode if p.returncode >= 0 else 128 - p.returncode}\n'.encode()
        if p is None or (suite != 'llvm' and p.returncode) or output != reference:
            return name, 'fail', 'runtime: ' + ('timeout' if p is None else f'exit {p.returncode}, output {p.stdout[:200]!r}')
        return name, 'pass', ''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--suite', choices=['c-testsuite', 'chibicc', 'llvm'], default='c-testsuite')
    parser.add_argument('--target', choices=['x86_64', 'aarch64'], default='x86_64')
    parser.add_argument('-j', type=int, default=6)
    parser.add_argument('-v', action='store_true')
    parser.add_argument('pattern', nargs='?', default='')
    args = parser.parse_args()
    if platform.system() != 'Linux':
        parser.error('requires a Linux host')
    global TARGET, FLAGS, RUNNER
    TARGET = f'--target={args.target}-linux-gnu'
    FLAGS = [TARGET, '-O2']
    if platform.machine() != args.target:
        if args.target != 'aarch64':
            parser.error('cross execution is supported for aarch64 only')
        RUNNER = ['qemu-aarch64', '-L', '/usr/aarch64-linux-gnu']
    roots = {'c-testsuite': ['c-testsuite/tests/single-exec'], 'chibicc': ['chibicc/test'],
             'llvm': ['llvm-test-suite/SingleSource/UnitTests', 'llvm-test-suite/SingleSource/Regression/C']}
    dirs = [SUITES / p for p in roots[args.suite]]
    if any(not root.is_dir() for root in dirs):
        parser.error(f'fetch the suite with bench/fetch-testsuites.sh {args.suite}')
    files = sorted(p for root in dirs for p in root.glob('*.c') if args.pattern in p.name)
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
