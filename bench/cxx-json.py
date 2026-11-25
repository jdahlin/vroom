#!/usr/bin/env python3
"""
bench/cxx-json.py: how far vcc's C++ front end gets on nlohmann/json's unit
tests (doc/PLAN-cxx.md: "Progress is measured by bench/cxx-json.py").

Each test TU of work/cxx/json/src/tests (src/unit.cpp, the doctest main, and
src/unit-*.cpp; unit-comparison.cpp a second time as test-comparison_legacy)
is compiled by `vpp -x c++ -std=c++17 -c` against libc++, with the flags the
tests' CMakeLists.txt gives it (DOCTEST_CONFIG_SUPER_FAST_ASSERTS,
JSON_TEST_KEEP_MACROS, doctest and fifo_map on the include path;
JSON_NOEXCEPTION and -fno-exceptions for unit-disabled_exceptions.cpp,
JSON_USE_LEGACY_DISCARDED_VALUE_COMPARISON=1 for the legacy comparison),
one TU at a time under nice -n 15 with a timeout.  Per TU: ok, or its first
error: the refused subclause and the vcc source file:line of the refusal
(the error's "refused by vcc's" note), and whether the analysis got through
(the error is the code generator's).  The summary groups the TUs by first
error, most frequent first.

Results are cached in build/cxx-json.json, keyed by the TU's flags, its
file's size and mtime and vpp's: a rerun compiles again only what changed.

  bench/cxx-json.py [--bin-dir DIR] [--timeout S] [--force] [-v] [PATTERN]
"""
import argparse
import json
import os
import re
import subprocess
import sys

B = os.path.dirname(os.path.abspath(__file__))
R = os.path.dirname(B)
JSON = os.path.join(R, 'work', 'cxx', 'json', 'src')
TESTS = os.path.join(JSON, 'tests')
LIBCXX = '/usr/lib/llvm-23/include/c++/v1'
CACHE = os.path.join(R, 'build', 'cxx-json.json')
GENERATED = os.path.join(R, 'build', 'cxx-json', 'include')


def tus():
    """(name, source, extra flags) for each test TU, as the tests' CMake
    builds them"""
    out = [('unit', os.path.join(TESTS, 'src', 'unit.cpp'), [])]
    for f in sorted(os.listdir(os.path.join(TESTS, 'src'))):
        if not (f.startswith('unit-') and f.endswith('.cpp')):
            continue
        extra = []
        if f == 'unit-disabled_exceptions.cpp':
            extra = ['-DJSON_NOEXCEPTION', '-fno-exceptions']
        out.append((f[:-4], os.path.join(TESTS, 'src', f), extra))
    out.append(('unit-comparison_legacy',
                os.path.join(TESTS, 'src', 'unit-comparison.cpp'),
                ['-DJSON_USE_LEGACY_DISCARDED_VALUE_COMPARISON=1']))
    return out


def common_flags():
    return ['-x', 'c++', '-std=c++17', '-nostdinc++', '-isystem', LIBCXX,
            '-I', os.path.join(JSON, 'include'),
            '-I', os.path.join(TESTS, 'thirdparty', 'doctest'),
            '-I', os.path.join(TESTS, 'thirdparty', 'fifo_map'),
            '-I', GENERATED,
            '-DDOCTEST_CONFIG_SUPER_FAST_ASSERTS', '-DJSON_TEST_KEEP_MACROS',
            '-DJSON_TEST_USING_MULTIPLE_HEADERS=0',
            '-Wno-deprecated', '-Wno-float-equal']


ERROR = re.compile(r'^(?P<where>[^:\s]+:\d+:\d+): error: (?P<message>.*)$')
REFUSED = re.compile(r"note: refused by vcc's (?P<site>\S+:\d+)")
RULES = re.compile(r'the rules of \[(?P<subclause>[^\]]+)\] are not implemented')


def first_error(stderr):
    """(message, subclause, vcc site, where) of the first error"""
    lines = stderr.splitlines()
    for i, line in enumerate(lines):
        m = ERROR.match(line)
        if not m:
            continue
        message = m.group('message')
        subclause = ''
        r = RULES.search(message)
        if r:
            subclause = r.group('subclause')
        site = ''
        for later in lines[i + 1:i + 6]:
            n = REFUSED.search(later)
            if n:
                site = n.group('site')
                break
            if ERROR.match(later):
                break
        return message, subclause, site, m.group('where')
    return '', '', '', ''


def key_of(vpp, source, flags):
    s = os.stat(source)
    v = os.stat(vpp)
    return json.dumps([source, s.st_size, int(s.st_mtime), v.st_size,
                       int(v.st_mtime), flags])


def run(vpp, name, source, flags, timeout):
    out = os.path.join(R, 'build', 'cxx-json', name + '.o')
    cmd = ['nice', '-n', '15', vpp] + flags + ['-c', source, '-o', out]
    try:
        p = subprocess.run(cmd, capture_output=True, text=True,
                           timeout=timeout)
    except subprocess.TimeoutExpired:
        return {'state': 'timeout'}
    if p.returncode == 0:
        return {'state': 'ok'}
    message, subclause, site, where = first_error(p.stderr)
    if not message:
        return {'state': 'crash', 'message': 'status %d' % p.returncode}
    state = 'analysis'
    if message.startswith('C++ code generation'):
        state = 'lowering'
    return {'state': state, 'message': message, 'subclause': subclause,
            'site': site, 'where': where}


def group_of(r):
    if r['state'] in ('ok', 'timeout', 'crash'):
        return r['state']
    if r.get('subclause'):
        return '[%s] %s' % (r['subclause'], r.get('site', ''))
    text = re.sub(r'\s+', ' ', r['message'])
    # a lowering refusal naming the entity: grouped by the construct the
    # mangler (or the analysis) did not handle, not by the entity
    m = re.search(r': the mangler does not handle (.*)$', text)
    if m:
        return 'symbol not mangled: ' + m.group(1)[:150]
    if text.startswith('C++ code generation does not handle a template member'):
        return 'C++ code generation: a template member whose definition was not instantiated'
    if r.get('site'):
        text += ' ' + r['site']
    return text[:110]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--bin-dir', default=os.environ.get('VCC_BIN') or
                    os.path.join(R, 'build', 'linux', 'src'))
    ap.add_argument('--timeout', type=int, default=60)
    ap.add_argument('--force', action='store_true')
    ap.add_argument('-v', action='store_true')
    ap.add_argument('pattern', nargs='?')
    a = ap.parse_args()
    vpp = os.path.join(a.bin_dir, 'vpp')
    if not os.path.isdir(TESTS):
        print('cxx-json: no %s (clone nlohmann/json into work/cxx/json/src)'
              % TESTS)
        return 0
    os.makedirs(GENERATED, exist_ok=True)
    data = os.path.join(GENERATED, 'test_data.hpp')
    if not os.path.exists(data):
        with open(data, 'w') as f:
            f.write('#define TEST_DATA_DIRECTORY "%s"\n'
                    % os.path.join(R, 'work', 'cxx', 'json-test-data'))
    cache = {}
    if os.path.exists(CACHE) and not a.force:
        with open(CACHE) as f:
            cache = json.load(f)
    results = {}
    for name, source, extra in tus():
        if a.pattern and a.pattern not in name:
            continue
        flags = common_flags() + extra
        key = key_of(vpp, source, flags)
        r = cache.get(name)
        if not r or r.get('key') != key:
            r = run(vpp, name, source, flags, a.timeout)
            r['key'] = key
            cache[name] = r
            with open(CACHE, 'w') as f:
                json.dump(cache, f, indent=1, sort_keys=True)
        results[name] = r
        if a.v:
            print('%-40s %s' % (name, group_of(r)))
    total = len(results)
    ok = sum(1 for r in results.values() if r['state'] == 'ok')
    analysed = sum(1 for r in results.values()
                   if r['state'] in ('ok', 'lowering'))
    print('cxx-json: %d TUs: %d through the analysis, %d through -c'
          % (total, analysed, ok))
    groups = {}
    for name, r in results.items():
        if r['state'] == 'ok':
            continue
        groups.setdefault(group_of(r), []).append(name)
    for g, names in sorted(groups.items(), key=lambda x: (-len(x[1]), x[0])):
        print('%4d  %s' % (len(names), g))
        if a.v:
            print('      ' + ' '.join(sorted(names)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
