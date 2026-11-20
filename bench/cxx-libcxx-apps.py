#!/usr/bin/env python3
"""cxx-libcxx-apps.py: small programs using libc++ headers never crash vpp.

Each program of src/test/libcxx-apps includes one std header and uses it.
vpp -x c++ -std=c++20 -c compiles it (against libc++, no run); at -O0 for
all, and -O1..-O3 for the ones in DEEP.  The result is OK, REFUSED (an
error message with a reason, exit 1) or CRASH (a signal, abort or more than
30 s).  Only CRASH fails the check; the refusals are listed with their reason.

    bench/cxx-libcxx-apps.py [-j N] [--bin-dir DIR] [-v] [PATTERN]
"""
import argparse, glob, os, re, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEEP = ('vector', 'string', 'algorithm', 'memory', 'optional')
LIBCXX = os.environ.get('LIBCXX_INC', '/usr/lib/llvm-23/include/c++/v1')


def run(vpp, src, level, tmp):
    out = os.path.join(tmp, os.path.basename(src) + f'.O{level}.o')
    cmd = [vpp, '-x', 'c++', '-std=c++20', '-nostdinc++', '-isystem', LIBCXX,
           f'-O{level}', '-c', src, '-o', out]
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
    except subprocess.TimeoutExpired:
        return 'CRASH', 'timeout > 30 s'
    if p.returncode == 0:
        return 'OK', ''
    if p.returncode < 0 or p.returncode > 1:
        return 'CRASH', f'exit {p.returncode}'
    m = re.search(r'error: (.*)', p.stderr)
    return 'REFUSED', (m.group(1) if m else p.stderr.strip()[:100])[:140]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-j', type=int, default=8)
    ap.add_argument('--bin-dir', default=os.environ.get('VCC_BIN', f'{R}/build/src'))
    ap.add_argument('-v', action='store_true')
    ap.add_argument('pattern', nargs='?', default='')
    a = ap.parse_args()
    vpp = os.path.join(a.bin_dir, 'vpp')
    srcs = sorted(s for s in glob.glob(f'{R}/src/test/libcxx-apps/*.cpp') if a.pattern in s)
    jobs = []
    for s in srcs:
        name = os.path.basename(s)[:-4]
        for level in (0, 1, 2, 3):
            if level == 0 or name in DEEP:
                jobs.append((name, s, level))
    with tempfile.TemporaryDirectory() as tmp, ThreadPoolExecutor(a.j) as ex:
        res = list(ex.map(lambda j: run(vpp, j[1], j[2], tmp), jobs))
    counts = {'OK': 0, 'REFUSED': 0, 'CRASH': 0}
    for (name, _, level), (state, why) in zip(jobs, res):
        counts[state] += 1
        if a.v or state != 'OK':
            print(f'{state:8} {name:15} -O{level}  {why}')
        elif level == 0:
            print(f'{state:8} {name:15} -O{level}')
    print(f'OK {counts["OK"]}, REFUSED {counts["REFUSED"]}, CRASH {counts["CRASH"]}')
    return 1 if counts['CRASH'] else 0


sys.exit(main())
