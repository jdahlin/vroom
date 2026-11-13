#!/usr/bin/env python3
"""common-test.py: the unit tests of src/common and src/port (src/test/common/*-test.c).

  bench/common-test.py [--no-sanitize] [-j N] [PATTERN]

Each test is one C file that includes the headers it tests and returns the
number of failed CHECKs; it is compiled here with the host C compiler ($CC,
else cc) against -Isrc/include and common/fail.c, under ASan and UBSan when
that compiler has them, with warnings as errors, and run.  Needs only the
checkout (no build of vcc), so it is part of ci/premerge.sh.
"""
import concurrent.futures, os, subprocess, sys, tempfile

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
T = f'{R}/src/test/common'
OUT = f'{R}/build/common-test'
CC = os.environ.get('CC', 'cc')
SOURCES = [f'{R}/src/common/fail.c']


def sanitizer_flags():
    with tempfile.TemporaryDirectory() as d:
        open(f'{d}/t.c', 'w').write('int main(void) { return 0; }\n')
        p = subprocess.run([CC, '-fsanitize=address,undefined', f'{d}/t.c', '-o', f'{d}/t'], capture_output=True)
        return ['-fsanitize=address,undefined', '-fno-sanitize-recover=undefined'] if p.returncode == 0 else []


def run_one(name, flags):
    exe = f'{OUT}/{name}'
    cmd = [CC, '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-pthread', f'-I{R}/src/include', f'-I{T}',
           *flags, f'{T}/{name}.c', *SOURCES, '-o', exe]
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode:
        return name, False, 'compile failed:\n' + p.stderr[-2000:]
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    p = subprocess.run([exe], capture_output=True, text=True, env=env, timeout=120)
    return name, p.returncode == 0, p.stderr[-2000:] if p.returncode else ''


def main():
    av = sys.argv[1:]
    sanitize = '--no-sanitize' not in av
    jobs = 4
    if '-j' in av:
        k = av.index('-j')
        jobs = int(av[k + 1])
        del av[k:k + 2]
    av = [a for a in av if a != '--no-sanitize']
    pat = av[0] if av else ''
    names = sorted(f[:-2] for f in os.listdir(T) if f.endswith('-test.c') and pat in f)
    if not names:
        sys.exit('common-test: no test matches')
    os.makedirs(OUT, exist_ok=True)
    flags = sanitizer_flags() if sanitize else []
    bad = 0
    with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
        for name, ok, msg in ex.map(lambda n: run_one(n, flags), names):
            print(f'{"pass" if ok else "FAIL"}  {name}')
            if not ok:
                bad += 1
                print(msg)
    print(f'common-test: {len(names) - bad}/{len(names)} pass' + (' (sanitizers: ' + ('on' if flags else 'off') + ')'))
    sys.exit(1 if bad else 0)


main()
