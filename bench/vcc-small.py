#!/usr/bin/env python3
"""Run small external compiler test suites on x86-64 or arm64 Linux.

Clang is the filter: it must compile and meet the suite runtime expectation.
c-testsuite requires its checked-in combined stdout/stderr. LLVM uses its
traditional combined output with an appended exit status; this covers only
C files in UnitTests and Regression/C, including enabled nested directories.
Directory-specific flags, arguments, architecture exclusions and fpcmp
tolerances follow their pinned CMakeLists.txt. Unmapped directories are skipped
explicitly; LLVM optimizer-specific variants are not counted. vcc emits
assembly; clang assembles and links it against the target libc.  chibicc's shared test support
is built by clang, as in its Makefile.
"""
import argparse
import concurrent.futures
import platform
import os
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path

from hostenv import BUILD, WORK, llvm

CLANG = llvm('clang')
VPP = str(Path(os.environ.get('VCC_BIN', BUILD)) / 'vpp')
SUITES = Path(WORK) / 'testsuites'
FLAGS = []
TARGET = ''
RUNNER = []
FPCMP = None
SMALL = False


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


def llvm_options(path):
    """Pinned SingleSource CMake settings; never silently default unknown dirs."""
    relative = path.relative_to(SUITES / 'llvm-test-suite/SingleSource')
    directory = relative.parent.as_posix()
    cflags, runargs, tolerance = [], [], None
    generic = {'UnitTests', 'Regression/C', 'UnitTests/Float',
               'UnitTests/Matrix', 'UnitTests/HashRecognize',
               'UnitTests/Vectorizer', 'UnitTests/C++11'}
    if directory in generic:
        pass
    elif directory == 'UnitTests/SignlessTypes':
        runargs = ['31415926']
    elif directory == 'UnitTests/Threads':
        cflags = ['-pthread']
    elif directory == 'UnitTests/Vector':
        cflags = ['-Wno-error=incompatible-pointer-types']
    elif directory == 'UnitTests/Vector/SSE' and 'x86_64' in TARGET:
        cflags = ['-msse2', '-ffp-contract=off', '-DFMA_DISABLED=1']
        tolerance = '0.016'
    elif directory in ('UnitTests/Vector/NEON', 'UnitTests/Vector/AArch64') and 'aarch64' in TARGET:
        cflags = ['-std=c99']
    elif directory == 'UnitTests/AArch64' and 'aarch64' in TARGET:
        pass  # compiler-rt is optional upstream; use the available Linux runtime.
    elif directory == 'UnitTests/X86' and 'x86_64' in TARGET:
        pass
    elif directory.startswith('Regression/C/gcc-c-torture'):
        return [], [], None, 'GCC torture subtree requires its separate CMake test manifest (not mapped here)'
    elif directory in ('UnitTests/Integer', 'UnitTests/EH') or directory.startswith('UnitTests/SetjmpLongjmp'):
        return [], [], None, 'directory is not added by upstream UnitTests/CMakeLists.txt'
    elif any(part in relative.parts for part in ('Mips', 'Altivec', 'LASX', 'LSX', 'HVX', 'AArch64', 'NEON', 'X86', 'SSE')) or any(part.startswith(('AVX512', 'AMX')) for part in relative.parts):
        return [], [], None, 'directory disabled for baseline target by upstream CMake'
    else:
        return [], [], None, 'upstream CMake directory settings not mapped'
    if directory == 'UnitTests' and 'aarch64' in TARGET and (
            path.name == 'blockstret.c' or path.name.startswith('ms_struct')):
        return [], [], None, 'excluded on AArch64 by suite CMakeLists.txt'
    return cflags, runargs, tolerance, ''


def matches(actual, expected, directory, tolerance=None):
    if actual == expected:
        return True
    if FPCMP is None:
        return False
    a, b = Path(directory) / 'actual.out', Path(directory) / 'expected.out'
    a.write_bytes(actual)
    b.write_bytes(expected)
    args = [FPCMP] + (['-r', tolerance] if tolerance else [])
    p = invoke([*args, str(a), str(b)])
    return p is not None and p.returncode == 0


def build_fpcmp(directory):
    """Use the pinned upstream checker, including its exact numeric semantics."""
    root = SUITES / 'llvm-test-suite'
    source = root / 'tools/fpcmp.c'
    data = source.read_bytes() if source.is_file() else subprocess.check_output(
        ['git', '-C', str(root), 'show', 'HEAD:tools/fpcmp.c'])
    src, exe = Path(directory) / 'fpcmp.c', Path(directory) / 'fpcmp'
    src.write_bytes(data)
    subprocess.run([CLANG, '-O2', str(src), '-lm', '-o', str(exe)], check=True)
    return str(exe)


def test(path, inc, suite):
    name = str(path.relative_to(SUITES / 'llvm-test-suite/SingleSource')) if suite == 'llvm' else path.name
    cflags, runargs, tolerance = [], [], None
    if suite == 'llvm':
        cflags, runargs, tolerance, reason = llvm_options(path)
        if reason:
            return name, 'skip', reason
    expected = path.with_suffix('.c.expected') if suite == 'c-testsuite' else path.with_suffix('.reference_output')
    if suite == 'llvm':
        suffixes = (['.reference_output.little-endian.small', '.reference_output.small'] if SMALL else [])
        for suffix in suffixes + ['.reference_output.little-endian', '.reference_output.Linux', '.reference_output']:
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
        flags = FLAGS + cflags + (['-Wno-implicit-function-declaration', '-Wno-implicit-int',
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
        p = invoke([*RUNNER, exe, *runargs], input=stdin, cwd=d)
        if p is None:
            return name, 'skip', 'clang runtime timeout'
        reference = p.stdout
        reference_status = p.returncode
        if suite == 'llvm':
            reference += f'exit {p.returncode if p.returncode >= 0 else 128 - p.returncode}\n'.encode()
        if ((suite != 'llvm' and p.returncode) or
                (suite in ('c-testsuite', 'llvm') and not matches(reference, expected.read_bytes(), d, tolerance))):
            return name, 'skip', f'clang result differs from suite expectation (exit {p.returncode})'

        p = invoke([VPP, '-S', *flags, '-nostdinc', *(extra if suite == 'chibicc' else []), *inc, str(path), '-o', asm])
        if p is None or p.returncode:
            return name, 'fail', 'vcc: ' + ('timeout' if p is None else p.stdout.decode(errors='replace')[-500:])
        p = invoke([CLANG, TARGET, asm, *extra, *common, '-pthread', '-o', exe])
        if p is None or p.returncode:
            return name, 'fail', 'assembly/link: ' + ('timeout' if p is None else p.stdout.decode(errors='replace')[-500:])
        p = invoke([*RUNNER, exe, *runargs], input=stdin, cwd=d)
        output = p.stdout if p is not None else b''
        if p is not None and suite == 'llvm':
            output += f'exit {p.returncode if p.returncode >= 0 else 128 - p.returncode}\n'.encode()
        if (p is None or p.returncode != reference_status or (suite != 'llvm' and p.returncode) or
                not matches(output, reference, d, tolerance) or
                (suite == 'llvm' and not matches(output, expected.read_bytes(), d, tolerance))):
            return name, 'fail', 'runtime: ' + ('timeout' if p is None else f'exit {p.returncode}, output {p.stdout[:200]!r}')
        return name, 'pass', ''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--suite', choices=['c-testsuite', 'chibicc', 'llvm'], default='c-testsuite')
    parser.add_argument('--target', choices=['x86_64', 'aarch64'], default='x86_64')
    parser.add_argument('-j', type=int, default=6)
    parser.add_argument('-v', action='store_true')
    parser.add_argument('--small', action='store_true', help='LLVM upstream SMALL_PROBLEM_SIZE mode')
    parser.add_argument('pattern', nargs='?', default='')
    args = parser.parse_args()
    if platform.system() != 'Linux':
        parser.error('requires a Linux host')
    global TARGET, FLAGS, RUNNER, FPCMP, SMALL
    TARGET = f'--target={args.target}-linux-gnu'
    SMALL = args.small
    FLAGS = [TARGET, '-O2'] + (['-DSMALL_PROBLEM_SIZE'] if SMALL and args.suite == 'llvm' else [])
    if platform.machine() != args.target:
        if args.target != 'aarch64':
            parser.error('cross execution is supported for aarch64 only')
        RUNNER = ['qemu-aarch64', '-L', '/usr/aarch64-linux-gnu']
    roots = {'c-testsuite': ['c-testsuite/tests/single-exec'], 'chibicc': ['chibicc/test'],
             'llvm': ['llvm-test-suite/SingleSource/UnitTests', 'llvm-test-suite/SingleSource/Regression/C']}
    dirs = [SUITES / p for p in roots[args.suite]]
    if any(not root.is_dir() for root in dirs):
        parser.error(f'fetch the suite with bench/fetch-testsuites.sh {args.suite}')
    files = sorted(p for root in dirs for p in (root.rglob('*.c') if args.suite == 'llvm' else root.glob('*.c'))
                   if args.pattern in str(p.relative_to(SUITES)))
    if not files:
        parser.error('no matching tests')
    inc = include_flags()
    with tempfile.TemporaryDirectory(prefix='vcc-small-tools-') as d:
        if args.suite == 'llvm':
            FPCMP = build_fpcmp(d)
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.j) as pool:
            results = list(pool.map(lambda p: test(p, inc, args.suite), files))
    counts = Counter(status for _, status, _ in results)
    for name, status, why in results:
        if status == 'fail' or (args.v and status == 'skip'):
            print(f'{status.upper()} {name}: {why}')
    print(f'{args.suite}: {counts["pass"]} passed, {counts["fail"]} failed, {counts["skip"]} skipped')
    if args.suite == 'llvm':
        exclusions = Counter(why.split(':', 1)[0] for _, status, why in results if status == 'skip')
        for why, count in sorted(exclusions.items()):
            print(f'  {count} skipped: {why}')
    return int(bool(counts['fail']))


if __name__ == '__main__':
    sys.exit(main())
