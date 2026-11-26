#!/usr/bin/env python3
"""
bench/highway/run.py: Google Highway (C++17, SIMD) built by vcc's C++ front
end and checked by its own tests.

Source: work/highway (VROOM_WORK=DIR looks in DIR):

  git clone https://github.com/google/highway.git work/highway
  git -C work/highway checkout c7e9bed0ac0506357007d52d2eae0f3880ab19d6

Built for the static x86-64 baseline target (SSE2, HWY_COMPILE_ONLY_STATIC),
with the tests' standalone mode (no gtest), against libc++, on an x86-64
host.  Every TU is compiled by clang++ and by `vpp -x c++ -c`; each test
program is linked by clang++ from vcc's object of the test and the library's
objects, vcc's where vcc compiles one and clang++'s otherwise, and run.  The
programs check themselves: exit status 0.

A test that passed before (bench/highway/passing-O<level>) and no longer
does is an error; --bless rewrites the list.  The summary also gives the
TUs vcc does not compile, grouped by their first error.

  bench/highway/run.py [-O 0123] [-j N] [--bless] [-v] [PAT]

PAT limits the tests (not the library).  Objects are kept in
build/highway and reused while the compiler, flags, source and the
harness's inputs are unchanged.
"""
import argparse
import concurrent.futures
import glob
import hashlib
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
R = os.path.dirname(os.path.dirname(HERE))
WORK = os.environ.get('VROOM_WORK') or os.path.join(R, 'work')
SRC = os.path.join(WORK, 'highway')
OUT = os.path.join(R, 'build', 'highway')
LIBCXX = '/usr/lib/llvm-23/include/c++/v1'
FLAGS = ['-std=c++17', '-I', SRC, '-DHWY_TEST_STANDALONE',
         '-DHWY_COMPILE_ONLY_STATIC']


def sources():
    """(library TUs, test TUs), relative to SRC"""
    def found(pattern):
        return sorted(os.path.relpath(p, SRC)
                      for p in glob.glob(os.path.join(SRC, pattern)))
    lib = [s for s in found('hwy/*.cc') if not s.endswith('_test.cc')]
    lib.append('hwy/tests/test_util.cc')
    return lib, found('hwy/tests/*_test.cc')


def headers_stamp():
    """the newest header of Highway, so an edited one recompiles"""
    return max(os.stat(p).st_mtime_ns for p in
               glob.glob(os.path.join(SRC, 'hwy', '**', '*.h'),
                         recursive=True))


def stamp(path):
    s = os.stat(path)
    return '%d:%d' % (s.st_size, s.st_mtime_ns)


def first_error(text):
    for line in text.splitlines():
        m = re.search(r'\berror: (.*)', line)
        if m:
            # without paths, so the same refusal groups together
            return re.sub(r'/\S+/', '', m.group(1))[:150]
    lines = text.strip().splitlines()
    if lines:
        return lines[-1][:150]
    return 'no diagnostic'


def compile_one(cmd, src, obj, key, timeout):
    """None, or the first error; the result is cached beside the object"""
    record = obj + '.json'
    key = hashlib.sha256(json.dumps([cmd, key]).encode()).hexdigest()
    if os.path.exists(record):
        with open(record) as f:
            r = json.load(f)
        if r['key'] == key and (r['error'] or os.path.exists(obj)):
            return r['error']
    try:
        p = subprocess.run(['nice'] + cmd +
                           ['-c', os.path.join(SRC, src), '-o', obj],
                           capture_output=True, text=True, timeout=timeout)
        error = None
        if p.returncode:
            error = first_error(p.stderr)
    except subprocess.TimeoutExpired:
        error = 'timeout'
    with open(record, 'w') as f:
        json.dump({'key': key, 'error': error}, f)
    return error


def link_and_run(test, objs, exe, timeout):
    """'' when the program passes, else why not"""
    p = subprocess.run(['clang++', '-stdlib=libc++', '-pthread', '-no-pie'] +
                       objs + ['-o', exe], capture_output=True, text=True)
    if p.returncode:
        missing = re.findall(r"undefined reference to `([^']*)'", p.stderr)
        if missing:
            return 'link: undefined ' + ', '.join(sorted(set(missing)))[:100]
        return 'link failed'
    try:
        p = subprocess.run(['nice', exe], capture_output=True, text=True,
                           errors='replace', timeout=timeout)
    except subprocess.TimeoutExpired:
        return 'timeout'
    if p.returncode:
        last = (p.stdout + p.stderr).strip().splitlines()[-1:]
        return 'exit %d %s' % (p.returncode, ' '.join(last)[:100])
    return ''


def object_path(d, src):
    return os.path.join(d, src.replace('/', '_')[:-3] + '.o')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-O', dest='levels', default='2')
    ap.add_argument('-j', type=int, default=4)
    ap.add_argument('--bin-dir', default=os.environ.get('VCC_BIN') or
                    os.path.join(R, 'build', 'linux', 'src'))
    ap.add_argument('--timeout', type=int, default=300)
    ap.add_argument('--bless', action='store_true')
    ap.add_argument('-v', action='store_true')
    ap.add_argument('pattern', nargs='?')
    a = ap.parse_args()
    if not os.path.isdir(os.path.join(SRC, 'hwy')):
        print('highway: no %s (see this file\'s header)' % SRC)
        return 0
    if os.uname().machine != 'x86_64':
        print('highway: needs an x86-64 host')
        return 0
    vpp = os.path.join(a.bin_dir, 'vpp')
    lib, tests = sources()
    if a.pattern:
        tests = [t for t in tests if a.pattern in t]
    headers = headers_stamp()
    failed = 0
    for level in a.levels:
        flags = FLAGS + ['-O' + level]
        commands = {
            'clang': ['clang++', '-stdlib=libc++'] + flags,
            'vcc': [vpp, '-x', 'c++', '-nostdinc++', '-isystem', LIBCXX,
                    '-fPIE'] + flags,
        }
        versions = {
            'clang': subprocess.run(['clang++', '--version'],
                                    capture_output=True, text=True).stdout,
            'vcc': stamp(vpp),
        }
        dirs = {who: os.path.join(OUT, '%s-O%s' % (who, level))
                for who in commands}
        for d in dirs.values():
            os.makedirs(d, exist_ok=True)

        def build(job):
            who, src = job
            key = [versions[who], stamp(os.path.join(SRC, src)), headers]
            return job, compile_one(commands[who], src,
                                    object_path(dirs[who], src), key,
                                    a.timeout)

        with concurrent.futures.ThreadPoolExecutor(a.j) as pool:
            errors = dict(pool.map(build, [(who, s) for who in commands
                                           for s in lib + tests]))
        broken = [s for s in lib + tests if errors[('clang', s)]]
        if broken:
            sys.exit('highway: clang++ failed on %s: %s' %
                     (broken[0], errors[('clang', broken[0])]))
        library = []
        for s in lib:
            who = 'vcc'
            if errors[('vcc', s)]:
                who = 'clang'
            library.append(object_path(dirs[who], s))

        def run(test):
            if errors[('vcc', test)]:
                return test, errors[('vcc', test)]
            exe = object_path(dirs['vcc'], test)[:-2]
            return test, link_and_run(
                test, library + [object_path(dirs['vcc'], test)], exe,
                a.timeout)

        with concurrent.futures.ThreadPoolExecutor(a.j) as pool:
            results = dict(pool.map(run, tests))

        names = {t: os.path.basename(t)[:-3] for t in tests}
        passed = {names[t] for t in tests if not results[t]}
        compiled = [t for t in tests if not errors[('vcc', t)]]
        refused = [s for s in lib + tests if errors[('vcc', s)]]
        print('highway -O%s: library %d of %d TUs by vcc; tests %d, compiled '
              '%d, passed %d' %
              (level, sum(not errors[('vcc', s)] for s in lib), len(lib),
               len(tests), len(compiled), len(passed)))
        groups = {}
        for s in refused:
            groups.setdefault(errors[('vcc', s)], []).append(s)
        for error, where in sorted(groups.items(),
                                   key=lambda g: (-len(g[1]), g[0])):
            print('%4d  %s' % (len(where), error))
            if a.v:
                print('      ' + ' '.join(where))
        for t in compiled:
            if results[t]:
                print('  FAIL %s: %s' % (names[t], results[t]))

        listed = os.path.join(HERE, 'passing-O' + level)
        before = set()
        if os.path.exists(listed):
            with open(listed) as f:
                before = set(f.read().split())
        lost = sorted((before & set(names.values())) - passed)
        new = sorted(passed - before)
        for name in lost:
            print('  LOST %s' % name)
        if new:
            print('  %d passing tests not in %s (--bless)' %
                  (len(new), os.path.relpath(listed, R)))
        if a.bless:
            # (a pattern's run keeps the other tests' lines)
            keep = before - set(names.values())
            with open(listed, 'w') as f:
                f.writelines(n + '\n' for n in sorted(keep | passed))
        elif lost:
            failed += 1
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
