#!/usr/bin/env python3
"""vcc-eh.py: exceptions through vcc's code (doc/PLAN-vir-eh.md), hosted Linux.

  bench/vcc-eh.py [-j N] [-v] [-O LEVELS] [--target aarch64|x86_64] [PAT]

Every src/test/eh/NAME.c is a test (cxx_NAME.cpp: a C++ program built whole by vcc and by clang++): built by vcc (`vpp -c -fexceptions`) and,
as the reference, by clang; NAME.cpp next to it, when there is one, is built
by clang++ (the C++ half: throws, catches, main).  Linked by clang++ against
glibc and libstdc++ and run: natively on an x86-64 host, under qemu-aarch64
with the aarch64 cross libraries (/usr/aarch64-linux-gnu) for arm64.  A test
passes when it exits 0; clang's build failing is a broken test.  Each target
is run at every level of -O (default 0123); a target whose tools are missing
(or a C++ program clang++ cannot link and run) is reported as skipped.  Exit status 1 when anything fails.
"""
import argparse
import os
import platform
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hostenv import llvm, BUILD

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
T = f'{R}/src/test/eh'
CL = llvm('clang')
CXX = llvm('clang++')
VPP = os.path.join(os.environ.get('VCC_BIN') or BUILD, 'vpp')
OUT = f'{BUILD}/eh'
CROSS = '/usr/aarch64-linux-gnu'


def sh(cmd, timeout=60):
    try:
        p = subprocess.run(cmd, capture_output=True, timeout=timeout)
        return p.returncode, p.stderr.decode(errors='replace')
    except subprocess.TimeoutExpired:
        return 'timeout', ''


def resource_include():
    return subprocess.run([CL, '-print-resource-dir'], capture_output=True,
                          text=True).stdout.strip() + '/include'


def targets(want):
    """(name, triple, vpp include flags, extra link inputs, run prefix) per usable target"""
    out, skipped = [], []
    res = resource_include()
    host = platform.machine()
    if want in (None, 'x86_64'):
        if host == 'x86_64':
            inc = ['-nostdinc', '-isystem', res, '-isystem', '/usr/include/x86_64-linux-gnu',
                   '-isystem', '/usr/include']
            out.append(('x86_64', 'x86_64-linux-gnu', inc, [], []))
        else:
            skipped.append(('x86_64', 'not an x86-64 host'))
    if want in (None, 'aarch64'):
        qemu = shutil.which('qemu-aarch64')
        lib = f'{CROSS}/lib/libstdc++.so.6'
        if host == 'aarch64':
            inc = ['-nostdinc', '-isystem', res, '-isystem', '/usr/include/aarch64-linux-gnu',
                   '-isystem', '/usr/include']
            out.append(('aarch64', 'aarch64-linux-gnu', inc, [], []))
        elif qemu and os.path.exists(lib) and os.path.isdir(f'{CROSS}/include'):
            inc = ['-nostdinc', '-isystem', res, '-isystem', f'{CROSS}/include']
            out.append(('aarch64', 'aarch64-linux-gnu', inc, ['-nostdlib++', lib],
                        [qemu, '-L', CROSS]))
        else:
            skipped.append(('aarch64', f'needs qemu-aarch64 and {lib}'))
    usable = []
    for t in out:
        why = link_probe(t)
        if why:
            skipped.append((t[0], why))
        else:
            usable.append(t)
    return usable, skipped


def link_probe(t):
    """None when clang++ links and runs a program that throws for target t, else why not"""
    tname, triple, _, link, run = t
    wd = f'{OUT}/{tname}'
    os.makedirs(wd, exist_ok=True)
    with open(f'{wd}/probe.cpp', 'w') as f:
        f.write('int main() { try { throw 1; } catch (int k) { return k - 1; } }\n')
    rc, err = sh([CXX, f'--target={triple}', f'{wd}/probe.cpp'] + link + ['-o', f'{wd}/probe'])
    if rc:
        return 'clang++ cannot link for it: ' + (err.strip().split('\n') or [''])[-1]
    rc, _ = sh(run + [f'{wd}/probe'])
    if rc:
        return f'a program clang++ built exits {rc}'
    return None


def run_one(job):
    name, (tname, triple, inc, link, run), level = job
    wd = f'{OUT}/{tname}/O{level}/{name}'
    os.makedirs(wd, exist_ok=True)
    src = f'{T}/{name}.c'
    cpp = f'{T}/{name}.cpp'
    objs = []
    if name.startswith('cxx_'):
        return run_cxx(job, wd, cpp)
    if os.path.exists(cpp):
        rc, err = sh([CXX, f'--target={triple}', '-O1', '-c', cpp, '-o', f'{wd}/cxx.o'])
        if rc:
            return job, 'broken', 'clang++: ' + err.strip().split('\n')[0]
        objs.append(f'{wd}/cxx.o')
    # a test whose first line says "vcc-only" uses a vcc extension: no clang build of the C half
    with open(src) as f:
        vcc_only = 'vcc-only' in f.readline()
    for side, cc in (('clang', [CL, f'--target={triple}', f'-O{level}']),
                     ('vcc', [VPP, f'--target={triple}', f'-O{level}'] + inc)):
        if side == 'clang' and vcc_only:
            continue
        rc, err = sh(cc + ['-fexceptions', '-c', src, '-o', f'{wd}/{side}.o'])
        if rc:
            if side == 'clang':
                return job, 'broken', 'clang: ' + err.strip().split('\n')[0]
            return job, 'fail', 'compile: ' + (err.strip().split('\n') or [''])[0]
        rc, err = sh([CXX, f'--target={triple}', f'{wd}/{side}.o'] + objs + link +
                     ['-o', f'{wd}/{side}'])
        if rc:
            if side == 'clang':
                return job, 'broken', 'link: ' + err.strip().split('\n')[0]
            return job, 'fail', 'link: ' + err.strip().split('\n')[0]
        rc, err = sh(run + [f'{wd}/{side}'])
        if rc != 0:
            if side == 'clang':
                return job, 'broken', f'clang build exits {rc}'
            return job, 'fail', f'exit {rc}'
    shutil.rmtree(wd, ignore_errors=True)
    return job, 'pass', ''


def run_cxx(job, wd, src):
    """a C++ test (cxx_NAME.cpp): the whole program compiled by vcc's C++ front end and by clang++, each linked
    and run; exit 0 is a pass"""
    name, (tname, triple, inc, link, run), level = job
    for side, cc in (('clang', [CXX, f'--target={triple}', f'-O{level}', '-std=c++20']),
                     ('vcc', [VPP, f'--target={triple}', f'-O{level}', '-x', 'c++', '-std=c++20'])):
        rc, err = sh(cc + ['-fexceptions', '-c', src, '-o', f'{wd}/{side}.o'])
        if rc:
            kind = 'broken' if side == 'clang' else 'fail'
            return job, kind, f'{side}: ' + (err.strip().split('\n') or [''])[0]
        rc, err = sh([CXX, f'--target={triple}', f'{wd}/{side}.o'] + link + ['-o', f'{wd}/{side}'])
        if rc:
            kind = 'broken' if side == 'clang' else 'fail'
            return job, kind, 'link: ' + err.strip().split('\n')[0]
        rc, err = sh(run + [f'{wd}/{side}'])
        if rc != 0:
            return job, ('broken' if side == 'clang' else 'fail'), f'{side} exit {rc}'
    shutil.rmtree(wd, ignore_errors=True)
    return job, 'pass', ''


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('-j', type=int, default=os.cpu_count())
    ap.add_argument('-v', action='store_true')
    ap.add_argument('-O', default='0123', help='levels, e.g. 02')
    ap.add_argument('--target', choices=['aarch64', 'x86_64'])
    ap.add_argument('pattern', nargs='?')
    a = ap.parse_args()
    tests = sorted([f[:-2] for f in os.listdir(T) if f.endswith('.c')] +
                   [f[:-4] for f in os.listdir(T) if f.startswith('cxx_') and f.endswith('.cpp')])
    tests = [t for t in tests if not a.pattern or a.pattern in t]
    tgts, skipped = targets(a.target)
    for name, why in skipped:
        print(f'skip {name}: {why}')
    jobs = [(t, tg, lv) for tg in tgts for lv in a.O for t in tests]
    if not jobs:
        print('vcc-eh: nothing to run')
        return 0
    counts = {}
    bad = 0
    with ThreadPoolExecutor(a.j) as ex:
        for (name, tg, lv), st, why in ex.map(run_one, jobs):
            counts[st] = counts.get(st, 0) + 1
            if st != 'pass':
                bad += 1
                print(f'{st.upper():6} {tg[0]} -O{lv} {name}: {why}')
            elif a.v:
                print(f'pass   {tg[0]} -O{lv} {name}')
    total = len(jobs)
    print(f'vcc-eh: {counts.get("pass", 0)}/{total} pass ' +
          ' '.join(f'{k}={v}' for k, v in sorted(counts.items()) if k != 'pass'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
