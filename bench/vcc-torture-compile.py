#!/usr/bin/env python3
"""Run GCC c-torture/compile against clang and vcc on x86-64 Linux.

    LLVM=/usr/lib/llvm-23 bench/vcc-torture-compile.py [-j N] [-v] [PAT]

Fetch the pinned GCC tree with bench/fetch-testsuites.sh gcc first. Tests with
conditional or otherwise unsupported DejaGNU directives are reported as skips,
not silently run under different conditions. Clang's -c is the oracle; vcc's
x86 backend currently emits assembly, which clang then assembles (X5).
"""
import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hostenv import BUILD, WORK, llvm

ROOT = Path(WORK) / 'testsuites/gcc/gcc/testsuite/gcc.c-torture/compile'
CLANG = llvm('clang')
VPP = os.path.join(os.environ.get('VCC_BIN') or BUILD, 'vpp')
BASE = ['--target=x86_64-linux-gnu', '-O2', '-w',
        '-Wno-error=implicit-function-declaration', '-Wno-error=implicit-int',
        '-Wno-error=int-conversion', '-Wno-error=incompatible-pointer-types',
        '-Wno-error=return-type', '-Wno-error=incompatible-function-pointer-types']

def system_includes():
    # vpp does not discover clang's Linux header search path on its own.
    p = subprocess.run([CLANG, '--target=x86_64-linux-gnu', '-E', '-x', 'c',
                        '-v', os.devnull], capture_output=True, text=True, check=True)
    listing = p.stderr.split('#include <...> search starts here:', 1)[1].split('End of search list.', 1)[0]
    return [arg for line in listing.splitlines() for arg in ('-isystem', line.strip()) if line.strip()]


DIRECTIVE = re.compile(r'\{\s*(dg-[\w-]+)\b([^\n]*)')
OPTIONS = re.compile(r'^\s*"([^"\n]*)"\s*\}')
DO = re.compile(r'^\s*compile\s*\}')


def flags_for(src):
    """Return unconditional options, or a reason we cannot run the test."""
    flags = []
    for match in DIRECTIVE.finditer(src):
        directive, tail = match.groups()
        if directive in ('dg-options', 'dg-additional-options'):
            m = OPTIONS.match(tail)
            if not m:
                return None, 'conditional or malformed options'
            try:
                flags.extend(shlex.split(m.group(1)))
            except ValueError:
                return None, 'malformed options'
        elif directive == 'dg-do':
            if not DO.match(tail):
                return None, 'conditional or non-compile dg-do'
        else:
            # Requirements, skip/xfail selectors, diagnostic expectations,
            # additional sources and final scans all affect the test contract.
            return None, 'unsupported ' + directive
    return flags, ''


def run(cmd, cwd):
    try:
        p = subprocess.run(cmd, cwd=cwd, capture_output=True, timeout=60)
        return p.returncode, p.stderr.decode(errors='replace')
    except subprocess.TimeoutExpired:
        return None, 'timeout'


def detail(err):
    for line in err.splitlines():
        if 'error:' in line:
            return line.split('error:', 1)[1].strip()
    return err.splitlines()[0][:200] if err.splitlines() else '(no diagnostic)'


def one(path):
    flags, why = flags_for(path.read_text(errors='replace'))
    if flags is None:
        return path.name, 'skip', why
    with tempfile.TemporaryDirectory(prefix='vcc-torture-') as wd:
        obj, asm = os.path.join(wd, 'out.o'), os.path.join(wd, 'out.s')
        rc, err = run([CLANG, *BASE, *flags, '-c', str(path), '-o', obj], path.parent)
        if rc is None or rc < 0:
            return path.name, 'error', 'clang ' + ('timeout' if rc is None else f'signal {-rc}')
        if rc:
            return path.name, 'skip', 'clang rejects: ' + detail(err)
        rc, err = run([VPP, *BASE, *flags, '-S', str(path), '-o', asm], path.parent)
        if rc is None or rc < 0:
            return path.name, 'crash', 'vpp ' + ('timeout' if rc is None else f'signal {-rc}')
        if rc:
            return path.name, 'fail', 'vpp: ' + detail(err)
        rc, err = run([CLANG, '--target=x86_64-linux-gnu', '-c', asm, '-o', obj], path.parent)
        if rc is None or rc < 0:
            return path.name, 'error', 'assembler ' + ('timeout' if rc is None else f'signal {-rc}')
        if rc:
            return path.name, 'fail', 'assembly: ' + detail(err)
    return path.name, 'pass', ''


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('-j', type=int, default=6)
    ap.add_argument('-v', action='store_true')
    ap.add_argument('pattern', nargs='?', default='')
    args = ap.parse_args()
    if args.j < 1:
        ap.error('-j must be positive')
    if not ROOT.is_dir():
        ap.error(f'{ROOT} missing; run bench/fetch-testsuites.sh gcc')
    BASE.extend(system_includes())
    files = [p for p in sorted(ROOT.glob('*.c')) if re.search(args.pattern, p.name)]
    if not files:
        ap.error('no matching tests')
    with ThreadPoolExecutor(max_workers=args.j) as pool:
        results = list(pool.map(one, files))
    counts = Counter(status for _, status, _ in results)
    reasons = Counter((status, reason) for _, status, reason in results if status != 'pass')
    if args.v:
        for name, status, reason in results:
            if status != 'pass':
                print(f'{status} {name}: {reason}')
    for (status, reason), n in reasons.most_common(20):
        print(f'{n:4d} {status}: {reason}')
    print(' '.join(f'{status}={counts[status]}' for status in ('pass', 'fail', 'crash', 'skip', 'error')))
    return bool(counts['fail'] or counts['crash'] or counts['error'])


if __name__ == '__main__':
    sys.exit(main())
