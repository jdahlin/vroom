#!/usr/bin/env python3
"""The C++ preprocessor's multiple-include guard skip against clang, on
src/test/cxx/include-guard (README.md there): per case, vpp
-fcxx-dump=preprocessed and clang -E -P give the same tokens, and both issue
the same diagnostics (file, line, column, level, text).

Usage: bench/cxx-include-guard.py [-v] [--bin-dir DIR] [PATTERN...]
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CASES = os.path.join(R, 'src', 'test', 'cxx', 'include-guard')
FLAGS = ['-x', 'c++', '-std=gnu++20', '-Wall', '-Wextra', '-Wunused-macros']
DIAG = re.compile(r'^(\S+:\d+:\d+: (?:warning|error|fatal error): .*)$')


def find(names, dirs):
    for d in dirs:
        for n in names:
            p = os.path.join(d, n)
            if os.path.exists(p):
                return p
    return None


def tokens(text):
    return text.split()


def diagnostics(stderr):
    return [m.group(1) for m in map(DIAG.match, stderr.splitlines()) if m]


def run(cmd, cwd):
    p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=60)
    return p.stdout, p.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-v', action='store_true')
    ap.add_argument('--bin-dir')
    ap.add_argument('patterns', nargs='*')
    a = ap.parse_args()
    vpp = find(['vpp'], ([a.bin_dir] if a.bin_dir else []) +
               [os.path.join(R, 'build', 'linux', 'src'), os.path.join(R, 'build', 'src')])
    clang = find(['clang++-23', 'clang++'], os.environ.get('PATH', '').split(':') + ['/usr/lib/llvm-23/bin'])
    if not vpp or not clang:
        sys.exit('cxx-include-guard: no vpp or no clang++')
    cases = sorted(c for c in os.listdir(CASES) if os.path.isdir(os.path.join(CASES, c)))
    if a.patterns:
        cases = [c for c in cases if any(p in c for p in a.patterns)]
    failed = 0
    for case in cases:
        with tempfile.TemporaryDirectory() as tmp:
            work = os.path.join(tmp, case)
            shutil.copytree(os.path.join(CASES, case), work, symlinks=True)
            links = os.path.join(work, 'LINKS')
            if os.path.exists(links):
                for line in open(links):
                    if line.split():
                        new, existing = line.split()
                        os.link(os.path.join(work, existing), os.path.join(work, new))
            vout, verr = run([vpp] + FLAGS + ['-fsyntax-only', '-fcxx-dump=preprocessed', 'main.cpp'], work)
            cout, cerr = run([clang] + FLAGS + ['-E', '-P', 'main.cpp'], work)
        bad = []
        if tokens(vout) != tokens(cout):
            bad.append(f'text: vcc {" ".join(tokens(vout))!r} clang {" ".join(tokens(cout))!r}')
        if diagnostics(verr) != diagnostics(cerr):
            bad.append(f'diagnostics: vcc {diagnostics(verr)} clang {diagnostics(cerr)}')
        failed += bool(bad)
        if bad or a.v:
            print(f'{"FAIL" if bad else "ok  "} {case}')
            for b in bad:
                print('    ' + b)
    print(f'cxx-include-guard: {len(cases) - failed}/{len(cases)} cases as clang')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
