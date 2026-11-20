#!/usr/bin/env python3
"""cxx-x86.py: the C++ front end's x86-64 programs, run natively (x86-64 host).

Each src/test/cxx/run/x86-*.cpp is built by clang++ -O2 and by vcc's driver
(vpp --target=x86_64-linux-gnu -c) at -O0..-O3, linked with clang against
the host's libc, and run: the exit status and the output must be clang's.  They use clang's
resource headers (<emmintrin.h>, ...) whose functions the program does not
use are left out (doc/PLAN-cxx-sema.md, GNU vectors V5).

    bench/cxx-x86.py [-O LEVELS] [PATTERN]
"""
import glob, os, re, subprocess, sys, tempfile

B = os.path.dirname(os.path.abspath(__file__))
R = os.path.dirname(B)
VPP = os.environ.get('VCC_BIN', f'{R}/build/linux/src') + '/vpp'
RES = subprocess.run(['clang', '-print-resource-dir'], capture_output=True,
                     text=True).stdout.strip() + '/include'
INC = ['-isystem', RES, '-isystem', '/usr/include/x86_64-linux-gnu',
       '-isystem', '/usr/include']


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True)


def main():
    levels = '0123'
    args = sys.argv[1:]
    if args[:1] == ['-O']:
        levels, args = args[1], args[2:]
    pattern = args[0] if args else ''
    failed = passed = 0
    for src in sorted(glob.glob(f'{R}/src/test/cxx/run/x86-*.cpp')):
        name = os.path.basename(src)[:-4]
        if pattern not in name:
            continue
        with tempfile.TemporaryDirectory() as d:
            ref = f'{d}/ref'
            r = run(['clang++', '-O2', *INC, src, '-o', ref])
            if r.returncode:
                print(f'FAIL {name}: clang++: {r.stderr[:300]}')
                failed += 1
                continue
            ref_run = run([ref])
            want = (ref_run.returncode, ref_run.stdout)
            for level in levels:
                o, exe = f'{d}/t{level}.o', f'{d}/t{level}'
                r = run([VPP, '--target=x86_64-linux-gnu', '-x', 'c++', '-c',
                         f'-O{level}', *INC, src, '-o', o])
                if r.returncode:
                    print(f'FAIL {name} -O{level}: vcc: '
                          f'{(r.stderr or r.stdout)[:300]}')
                    failed += 1
                    continue
                r = run(['clang', o, '-o', exe, '-lstdc++'])
                got = (-1, '')
                if not r.returncode:
                    ran = run([exe])
                    got = (ran.returncode, ran.stdout)
                if got != want:
                    print(f'FAIL {name} -O{level}: ran {got[0]}, clang {want[0]}'
                          f'{" (output differs)" if got[1] != want[1] else ""}')
                    failed += 1
                else:
                    passed += 1
    print(f'cxx-x86: {passed} passed, {failed} failed (-O{levels})')
    return 1 if failed else 0


sys.exit(main())
