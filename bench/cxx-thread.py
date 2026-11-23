#!/usr/bin/env python3
"""C++ programs that need threads: src/test/cxx/thread/*.cpp.

  bench/cxx-thread.py [-v] [PAT]

The run suite (bench/cxx-run.py) links freestanding, without threads, so a bug that only
shows with two threads (a variable that should be thread-local or a local, emitted as one
static) needs these.  Each program is built for the host by vpp (-x c++ -c, at -O0 and -O2),
linked by clang++ -pthread against the host's C library, run a few times, and must exit 0;
it checks itself and prints how many mismatches it saw.  Where the program has no meaning on
the host (x86's builtins on arm64) it returns 0.  vcc's tools: build/linux/src (or build/src);
VCC_BIN picks another directory.
"""
import glob, os, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.environ.get('VCC_BIN') or next(
    d for d in (f'{ROOT}/build/linux/src', f'{ROOT}/build/src') if os.path.exists(f'{d}/vpp'))
SRC = f'{ROOT}/src'
RUNS = 3


def main():
    av = [a for a in sys.argv[1:] if not a.startswith('-')]
    verbose = '-v' in sys.argv
    files = sorted(glob.glob(f'{SRC}/test/cxx/thread/*.cpp'))
    if av:
        files = [f for f in files if av[0] in f]
    failed = 0
    with tempfile.TemporaryDirectory() as d:
        for f in files:
            name = os.path.basename(f)[:-4]
            for level in ('-O0', '-O2'):
                obj, exe = f'{d}/{name}{level}.o', f'{d}/{name}{level}'
                r = subprocess.run([f'{BIN}/vpp', '-x', 'c++', '-std=gnu++20', level, '-c', '-w', f, '-o', obj],
                                   capture_output=True, text=True)
                if r.returncode:
                    print(f'FAIL {name} {level}: vpp: {r.stderr.strip().splitlines()[0] if r.stderr else r.returncode}')
                    failed += 1
                    continue
                r = subprocess.run(['clang++', '-pthread', obj, '-o', exe], capture_output=True, text=True)
                if r.returncode:
                    print(f'FAIL {name} {level}: link: {r.stderr.strip().splitlines()[0]}')
                    failed += 1
                    continue
                bad = []
                for _ in range(RUNS):
                    r = subprocess.run([exe], capture_output=True, text=True, timeout=120)
                    if r.returncode:
                        bad.append(r.stdout.strip() or f'exit {r.returncode}')
                if bad:
                    print(f'FAIL {name} {level}: {bad[0]} ({len(bad)} of {RUNS} runs)')
                    failed += 1
                elif verbose:
                    print(f'ok   {name} {level}')
    print(f'cxx-thread: {len(files) * 2 - failed} passed, {failed} failed ({len(files)} programs, -O0 -O2)')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
