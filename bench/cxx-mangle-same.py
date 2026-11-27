#!/usr/bin/env python3
"""
bench/cxx-mangle-same.py: the identity gate of doc/PLAN-mangling.md (M0).

For each input, vpp's -fsyntax-only -fcxx-dump=symbols output (every
symbol, and every declaration the mangler refuses, with its reason on
stderr) and the symbols of vpp -c's object (llvm-nm, defined and
referenced: vtables, type_info, guards, thunks), with -c's diagnostics.
--save writes them to a file; --against compares a build with that file
and prints what differs, input by input: a restructuring of the mangler
must keep every byte.

Inputs: src/test/cxx/run (cxx-run.py's flags), clang's mangling tests
(work/testsuites, cxx-mangle.py's configuration), nlohmann/json's test TUs
(work/cxx/json) and Stockfish (work/stockfish), each when it is there.

  bench/cxx-mangle-same.py [-j N] [--bin-dir DIR] [--only KIND] --save F | --against F
"""
import argparse
import concurrent.futures
import importlib.util
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile

B = os.path.dirname(os.path.abspath(__file__))
R = os.path.dirname(B)
NM = '/usr/lib/llvm-23/bin/llvm-nm'


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def inputs():
    """(key, source, flags) for every input"""
    out = []
    run = load('cxxrun', os.path.join(B, 'cxx-run.py'))
    for f in sorted(os.listdir(run.TESTS)):
        if not f.endswith('.cpp'):
            continue
        src = os.path.join(run.TESTS, f)
        with open(src, errors='replace') as fh:
            head = fh.read(2000)
        found = re.search(r'cxx-run-flags:(.*?)\*/', head)
        flags = run.CXX + (shlex.split(found.group(1)) if found else [])
        out.append(('run/' + f, src, flags))
    mangle = load('cxxmangle', os.path.join(B, 'cxx-mangle.py'))
    if os.path.isdir(mangle.SUITE):
        for f in mangle.candidates():
            c = mangle.configuration(os.path.join(mangle.SUITE, f))
            if not c:
                continue
            target, flags = c
            out.append(('clang/' + f, os.path.join(mangle.SUITE, f),
                        ['-x', 'c++', '--target=' + target, '-O0', '-w'] +
                        flags))
    cj = load('cxxjson', os.path.join(B, 'cxx-json.py'))
    if os.path.isdir(cj.TESTS) and os.path.exists(
            os.path.join(cj.GENERATED, 'test_data.hpp')):
        for name, src, extra in cj.tus():
            out.append(('json/' + name, src,
                        cj.common_flags() + extra + ['-w']))
    sf = load('stockfish', os.path.join(B, 'stockfish', 'run.py'))
    if os.path.isdir(sf.SRC):
        for s in sf.sources():
            out.append(('stockfish/' + s, os.path.join(sf.SRC, s),
                        ['-x', 'c++', '-std=c++17', '-fPIE', '-nostdinc++',
                         '-isystem', sf.LIBCXX, '-O0', '-w'] + sf.DEFINES))
    return out


def scrub(text, tmp):
    return text.replace(tmp, '$TMP')


def one(vpp, key, src, flags, timeout):
    r = {}
    with tempfile.TemporaryDirectory() as tmp:
        try:
            p = subprocess.run(['nice', '-n', '10', vpp] + flags +
                               ['-fsyntax-only', '-fcxx-dump=symbols', src],
                               capture_output=True, text=True,
                               errors='replace', timeout=timeout)
            r['dump'] = p.stdout
            r['dump_err'] = scrub(p.stderr, tmp)
            r['dump_rc'] = p.returncode
        except subprocess.TimeoutExpired:
            r['dump'] = 'timeout'
        obj = os.path.join(tmp, 'x.o')
        try:
            p = subprocess.run(['nice', '-n', '10', vpp] + flags +
                               ['-c', src, '-o', obj],
                               capture_output=True, text=True,
                               errors='replace', timeout=timeout)
            r['c_err'] = scrub(p.stderr, tmp)
            r['c_rc'] = p.returncode
            if p.returncode == 0 and os.path.exists(obj):
                n = subprocess.run([NM, obj], capture_output=True,
                                   text=True, errors='replace')
                r['nm'] = n.stdout
        except subprocess.TimeoutExpired:
            r['c_err'] = 'timeout'
    return key, r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-j', type=int, default=6)
    ap.add_argument('--bin-dir', default=os.environ.get('VCC_BIN') or
                    os.path.join(R, 'build', 'linux', 'src'))
    ap.add_argument('--only', help='inputs whose key starts with this')
    ap.add_argument('--timeout', type=int, default=120)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument('--save')
    g.add_argument('--against')
    a = ap.parse_args()
    vpp = os.path.join(a.bin_dir, 'vpp')
    todo = [i for i in inputs() if not a.only or i[0].startswith(a.only)]
    results = {}
    with concurrent.futures.ThreadPoolExecutor(a.j) as ex:
        for key, r in ex.map(lambda i: one(vpp, i[0], i[1], i[2],
                                           a.timeout), todo):
            results[key] = r
    if a.save:
        with open(a.save, 'w') as f:
            json.dump(results, f, indent=0, sort_keys=True)
        print('%d inputs saved to %s' % (len(results), a.save))
        return 0
    with open(a.against) as f:
        base = json.load(f)
    differ = 0
    for key in sorted(results):
        if key not in base:
            continue
        if results[key] == base[key]:
            continue
        differ += 1
        print('DIFFER', key)
        for field in sorted(set(results[key]) | set(base[key])):
            x, y = base[key].get(field), results[key].get(field)
            if x == y:
                continue
            xs = str(x).splitlines()
            ys = str(y).splitlines()
            gone = [l for l in xs if l not in ys][:5]
            new = [l for l in ys if l not in xs][:5]
            print('  %s:' % field)
            for l in gone:
                print('    - ' + l[:300])
            for l in new:
                print('    + ' + l[:300])
    print('%d inputs, %d differ' % (len(results), differ))
    return 1 if differ else 0


if __name__ == '__main__':
    sys.exit(main())
