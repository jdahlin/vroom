#!/usr/bin/env python3
"""
bench/v8/run.py: V8 (C++20) compiled by vcc, TU by TU, with the command lines
of a clang build of d8.

Source: work/v8-root/v8, tag 15.9.135 with its DEPS (depot_tools' `gclient
sync --nohooks`), and a reference build in out/clang (`ninja -C out/clang
d8`).

The reference build's args.gn: is_debug=false, is_component_build=false,
use_sysroot=false, clang_use_chrome_plugins=false,
treat_warnings_as_errors=false, symbol_level=0, use_siso=false,
v8_monolithic=true, v8_use_external_startup_data=false,
v8_enable_i18n_support=false, v8_enable_temporal_support=false,
v8_enable_sandbox=true, enable_rust=false, use_custom_libcxx=true,
clang_version="23", and clang_base_path a directory of symlinks to
/usr/lib/llvm-23 that also has compiler-rt as
lib/clang/23/lib/x86_64-unknown-linux-gnu/libclang_rt.builtins.a.

Each TU of d8 is compiled by vpp with clang's own arguments (without the
dependency file); the summary groups the TUs that fail by their first error,
as bench/cxx-json.py does.  Nothing is linked yet.

  bench/v8/run.py [-j N] [--timeout S] [--force] [-v] [PAT]

Results are cached in build/v8/cache.json (key: command line, source and vpp
size and mtime); a change to a header alone needs --force.
"""
import argparse
import collections
import concurrent.futures
import importlib.util
import json
import os
import shlex
import subprocess
import sys

B = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
R = os.path.dirname(B)
WORK = os.environ.get('VROOM_WORK') or os.path.join(R, 'work')
BUILD = os.path.join(WORK, 'v8-root', 'v8', 'out', 'clang')
OUT = os.path.join(R, 'build', 'v8')
CACHE = os.path.join(OUT, 'cache.json')

spec = importlib.util.spec_from_file_location(
    'cxx_json', os.path.join(B, 'cxx-json.py'))
cj = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cj)


def commands():
    """[(source, is C++, arguments without compiler, -MF and -o)]"""
    text = subprocess.run(['ninja', '-C', BUILD, '-t', 'commands', 'd8'],
                          check=True, capture_output=True, text=True).stdout
    out = []
    for line in text.splitlines():
        if ' -c ' not in line:
            continue
        words = shlex.split(line)
        compiler = os.path.basename(words[0])
        if compiler not in ('clang', 'clang++'):
            continue
        args = []
        skip = False
        for i, word in enumerate(words[1:], 1):
            if skip:
                skip = False
            elif word in ('-MF', '-o'):
                skip = True
            elif word == '-c':
                src = words[i + 1]
                skip = True
            elif word != '-MD':
                args.append(word)
        out.append((src, compiler == 'clang++', args))
    return out


def compile_one(vpp, src, args, timeout):
    obj = os.path.join(OUT, src.replace('../', '').replace('/', '_') + '.o')
    cmd = ['nice', '-n', '15', vpp] + args + ['-c', src, '-o', obj]
    try:
        p = subprocess.run(cmd, cwd=BUILD, capture_output=True, text=True,
                           errors='replace', timeout=timeout)
    except subprocess.TimeoutExpired:
        return {'state': 'timeout'}
    if p.returncode == 0:
        return {'state': 'ok'}
    message, subclause, site, where = cj.first_error(p.stderr)
    if not message:
        lines = p.stderr.strip().splitlines()
        if p.returncode < 0:
            message = 'signal %d' % -p.returncode
        elif lines:
            message = lines[0]
        else:
            message = 'exit %d' % p.returncode
    return {'state': 'error', 'message': message, 'subclause': subclause,
            'site': site, 'where': where}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('pattern', nargs='?', default='')
    ap.add_argument('-j', type=int, default=8)
    ap.add_argument('--timeout', type=int, default=120)
    ap.add_argument('--force', action='store_true')
    ap.add_argument('-v', action='store_true')
    ap.add_argument('--bin-dir', default=os.path.join(R, 'build/linux/src'))
    a = ap.parse_args()
    vpp = os.path.abspath(os.path.join(a.bin_dir, 'vpp'))
    os.makedirs(OUT, exist_ok=True)
    cache = {}
    if os.path.exists(CACHE):
        with open(CACHE) as f:
            cache = json.load(f)
    st = os.stat(vpp)
    todo, results = [], {}
    for src, _, args in commands():
        if a.pattern not in src:
            continue
        s = os.stat(os.path.join(BUILD, src))
        key = json.dumps([args, s.st_size, s.st_mtime, st.st_size,
                          st.st_mtime])
        if not a.force and cache.get(src, {}).get('key') == key:
            results[src] = cache[src]['result']
        else:
            todo.append((src, args, key))

    def save():
        with open(CACHE, 'w') as f:
            json.dump(cache, f)

    with concurrent.futures.ThreadPoolExecutor(a.j) as pool:
        futures = {pool.submit(compile_one, vpp, src, args, a.timeout):
                   (src, key) for src, args, key in todo}
        for n, future in enumerate(concurrent.futures.as_completed(futures)):
            src, key = futures[future]
            results[src] = future.result()
            cache[src] = {'key': key, 'result': results[src]}
            if n % 100 == 99:
                save()
                print('%d/%d' % (n + 1, len(todo)), file=sys.stderr)
    save()

    groups = collections.defaultdict(list)
    for src, result in sorted(results.items()):
        if result['state'] != 'ok':
            head = (result.get('message', result['state']),
                    result.get('site') or '', result.get('where') or '')
            groups[head].append(src)
        if a.v:
            print(src, result)
    ok = sum(r['state'] == 'ok' for r in results.values())
    print('%d of %d TUs compile' % (ok, len(results)))
    for (message, site, where), srcs in sorted(groups.items(),
                                               key=lambda g: -len(g[1])):
        print('%5d  %s  [%s %s]\n       e.g. %s' %
              (len(srcs), message, site, where, srcs[0]))


if __name__ == '__main__':
    main()
