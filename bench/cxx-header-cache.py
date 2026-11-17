#!/usr/bin/env python3
"""The new C++ front end's header cache (OPTIMIZATION, src/frontend/cplusplus/
cpp/cpp.header_cache.cpp; doc/PLAN-cxx-codegen.md S0b): the same tokens, and
how much faster.

Runs cxxparse -batch over a set of translation units twice, without and with
-header-cache, and checks that -dump-tokens (every token with its location,
spelling location and flags) and the diagnostics are byte for byte the same;
lines holding __DATE__/__TIME__ strings are left out of the comparison.
Then times -stats -tokens-only both ways (phases 1-7, the sum of each TU's
nanoseconds) and prints the cache's counters.

Sets:
  libstdcxx   json.hpp and each libstdc++ 14 header alone (work/cxx-sysroot,
              work/cxx/json), gnu++17, aarch64-linux-gnu
  clangpp     clang's test/Preprocessor *.c and *.cpp, each twice
              (work/testsuites/llvm; bench/fetch-testsuites.sh llvm)

  bench/cxx-header-cache.py [--set libstdcxx|clangpp|all] [--bin-dir DIR]
"""
import argparse
import importlib.util
import os
import re
import subprocess
import sys
import tempfile

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORK = os.environ.get('VROOM_WORK', os.path.join(R, 'work'))
TIME = re.compile(r"'\\\"(\d\d:\d\d:\d\d|[A-Z][a-z][a-z] [ \d]\d \d{4})\\\"'")

spec = importlib.util.spec_from_file_location('speed', os.path.join(R, 'bench', 'cxx-parse-speed.py'))
speed = importlib.util.module_from_spec(spec)
spec.loader.exec_module(speed)


def run(cmd, cwd):
    p = subprocess.run(cmd, capture_output=True, cwd=cwd)
    if p.returncode not in (0, 1):
        sys.exit('cxx-header-cache: %s exited %d\n%s' % (cmd[0], p.returncode, p.stderr.decode()[-2000:]))
    return p.stdout, p.stderr


def strip_time(b):
    return b'\n'.join(l for l in b.split(b'\n') if not TIME.search(l.decode('utf-8', 'replace')))


def check(name, cxxparse, flags, paths, cwd):
    base = [cxxparse, '-batch'] + flags
    t0, e0 = run(base + ['-dump-tokens'] + paths, cwd)
    t1, e1 = run(base + ['-header-cache', '-dump-tokens'] + paths, cwd)
    same = strip_time(t0) == strip_time(t1) and e0 == e1
    s0, _ = run(base + ['-stats', '-tokens-only'] + paths, cwd)
    s1, _ = run(base + ['-header-cache', '-stats', '-tokens-only'] + paths, cwd)

    def total(out):
        return sum(int(m) for m in re.findall(rb'^nanoseconds (\d+)$', out, re.M)) / 1e9
    counters = dict((k.decode(), int(v)) for k, v in re.findall(rb'^(header-cache-[a-z-]+) (\d+)$', s1, re.M))
    tokens = sum(int(m) for m in re.findall(rb'^tokens (\d+)$', s0, re.M))
    print('%-10s %4d TUs %9d tokens: %s; phases 1-7 %.2f s -> %.2f s; hits %d, misses %d, '
          'recorded %d, not kept %d, tokens replayed %d' % (
              name, len(paths), tokens, 'same tokens and diagnostics' if same else 'DIFFERENT',
              total(s0), total(s1), counters.get('header-cache-hits', 0),
              counters.get('header-cache-misses', 0), counters.get('header-cache-recorded', 0),
              counters.get('header-cache-not-kept', 0), counters.get('header-cache-tokens-replayed', 0)))
    return same


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--set', default='all', choices=('libstdcxx', 'clangpp', 'all'))
    ap.add_argument('--bin-dir')
    a = ap.parse_args()
    cxxparse = speed.find_cxxparse(a.bin_dir)
    ok = True
    tmp = tempfile.mkdtemp(prefix='cxx-header-cache-')
    if a.set in ('libstdcxx', 'all'):
        flags = speed.flags(speed.find_clang())
        d = os.path.join(speed.SYSROOT, 'c++', '14')
        paths = [os.path.join(tmp, 'json.cpp')]
        open(paths[0], 'w').write('#include <nlohmann/json.hpp>\nint main() {}\n')
        for h in sorted(os.listdir(d)):
            if os.path.isfile(os.path.join(d, h)) and '.' not in h:
                f = os.path.join(tmp, 'h-%s.cpp' % h)
                open(f, 'w').write('#include <%s>\n' % h)
                paths.append(f)
        ok &= check('libstdcxx', cxxparse, flags, paths, tmp)
    if a.set in ('clangpp', 'all'):
        d = os.path.join(WORK, 'testsuites', 'llvm', 'clang', 'test', 'Preprocessor')
        if not os.path.isdir(d):
            sys.exit('cxx-header-cache: no %s (bench/fetch-testsuites.sh llvm)' % d)
        names = sorted(f for f in os.listdir(d) if f.endswith(('.c', '.cpp')))
        ok &= check('clangpp', cxxparse, ['-std=gnu++17', '-I' + os.path.join(d, 'Inputs')],
                    names + names, d)
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
