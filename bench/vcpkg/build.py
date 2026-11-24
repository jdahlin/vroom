#!/usr/bin/env python3
"""vcpkg/build.py: compile microsoft/vcpkg-tool (C++17) with vpp, clang as the reference.

Sources in work/vcpkg-tool; fmt, curl's headers and cmrc in work/vcpkg-deps-*
(git clone --depth 1; fmt 11.0.2).  cmrc's header is cut out of CMakeRC.cmake and
the locale resource library is a stub, since only compiling is measured (no cmake).

    bench/vcpkg/build.py [-j N] [--bin-dir DIR] [--clang] [-O N] [-v] [PATTERN]

Per TU: OK, REFUSED (an error with a reason, exit 1) or CRASH (a signal or timeout).
"""
import argparse, glob, os, re, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor

R = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
W = os.environ.get('VROOM_WORK', os.path.join(R, 'work'))
if not os.path.isdir(W):
    W = '/source/vroom/work'
SRC = f'{W}/vcpkg-tool'
LIBCXX = os.environ.get('LIBCXX_INC', '/usr/lib/llvm-23/include/c++/v1')


def prepare(tmp):
    """cmrc/cmrc.hpp from CMakeRC.cmake and a stub resource library header."""
    d = os.path.join(tmp, 'cmrc')
    os.makedirs(d)
    text = open(f'{W}/vcpkg-deps-cmrc/CMakeRC.cmake').read()
    m = re.search(r'set\(hpp_content \[==\[\n(.*?)\]==\]\)', text, re.S)
    open(os.path.join(d, 'cmrc.hpp'), 'w').write(m.group(1))
    return tmp


def flags(tmp):
    return ['-x', 'c++', '-std=c++17', '-nostdinc++', '-isystem', LIBCXX,
            f'-I{SRC}/include', f'-I{W}/vcpkg-deps-fmt/include',
            f'-I{W}/vcpkg-deps-curl/include', f'-I{tmp}',
            '-DVCPKG_VERSION=unknown', '-DVCPKG_BASE_VERSION=unknown',
            '-D_FILE_OFFSET_BITS=64', '-DFMT_HEADER_ONLY=1', '-include', f'{SRC}/include/pch.h']


def run(cc, src, level, tmp, clang):
    out = os.path.join(tmp, src.replace('/', '_') + '.o')
    cmd = [cc] + flags(tmp) + [f'-O{level}', '-c', src, '-o', out]
    if clang:
        cmd += ['-w']
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired:
        return 'CRASH', 'timeout > 120 s'
    if p.returncode == 0:
        return 'OK', ''
    if p.returncode < 0 or p.returncode > 1:
        return 'CRASH', f'exit {p.returncode}'
    m = re.search(r'error: (.*)', p.stderr)
    return 'REFUSED', (m.group(1) if m else p.stderr.strip()[:100])[:160]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-j', type=int, default=6)
    ap.add_argument('--bin-dir', default=os.environ.get('VCC_BIN', f'{R}/build/linux/src'))
    ap.add_argument('--clang', action='store_true', help='clang++ instead of vpp (the reference)')
    ap.add_argument('-O', dest='level', type=int, default=0)
    ap.add_argument('-v', action='store_true')
    ap.add_argument('pattern', nargs='?', default='')
    a = ap.parse_args()
    cc = 'clang++' if a.clang else os.path.join(a.bin_dir, 'vpp')
    srcs = sorted(s for s in glob.glob(f'{SRC}/src/vcpkg/**/*.cpp', recursive=True)
                  + [f'{SRC}/src/vcpkg.cpp'] if a.pattern in s)
    with tempfile.TemporaryDirectory() as tmp, ThreadPoolExecutor(a.j) as ex:
        prepare(tmp)
        res = list(ex.map(lambda s: run(cc, s, a.level, tmp, a.clang), srcs))
    counts = {'OK': 0, 'REFUSED': 0, 'CRASH': 0}
    for s, (state, why) in zip(srcs, res):
        counts[state] += 1
        if a.v or state != 'OK':
            print(f'{state:8} {os.path.relpath(s, SRC):40} {why}')
    print(f'OK {counts["OK"]}, REFUSED {counts["REFUSED"]}, CRASH {counts["CRASH"]}')
    return 1 if counts['CRASH'] else 0


sys.exit(main())
