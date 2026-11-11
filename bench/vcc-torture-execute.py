#!/usr/bin/env python3
"""GCC IEEE/builtins execution tests on Linux, with a clang execution oracle.

LLVM=/usr/lib/llvm-23 bench/vcc-torture-execute.py -j 6 --target x86_64-linux-gnu
Use --target aarch64-linux-gnu for qemu-aarch64 and the cross glibc sysroot.
Builtins run at -O0: the GCC-specific optimization passes disabled by
builtins.exp are inactive there. Optimized builtins configurations are reported
unsupported, never silently substituted. Known Linux selectors and the builtins .x scripts are interpreted conservatively;
unknown directives or script contents remain explicit skips. Each builtin's companion and main.c are built
by the tested compiler, in separate translation units as in builtins.exp.
"""
import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

spec = importlib.util.spec_from_file_location('torture_compile', Path(__file__).with_name('vcc-torture-compile.py'))
tc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tc)
from torture_directives import legacy_options, replaces_default_options

ROOT = tc.ROOT.parent / 'execute'


def command(cmd, cwd, timeout=30):
    try:
        p = subprocess.run(cmd, cwd=cwd, capture_output=True, timeout=timeout)
        return p.returncode, p.stdout, p.stderr.decode(errors='replace')
    except subprocess.TimeoutExpired:
        return None, b'', 'timeout'


def one(path, args, base, runner):
    name = str(path.relative_to(ROOT))
    def result(status, reason=''):
        return {'name': name, 'status': status, 'reason': reason}
    suite = path.parent.name
    legacy_flags, why = legacy_options(path.with_suffix('.x'), args.opt)
    if why:
        return result('skip', why)
    if suite == 'builtins' and args.opt != '0':
        return result('skip', 'optimized builtins require GCC-specific pass controls')
    src = path.read_text(errors='replace')
    flags, reason = tc.flags_for(src, args.target, 'run')
    if flags is None:
        return result('skip', reason)
    sources = [path]
    defaults = ['-fno-inline'] if suite == 'ieee' else ['-fno-inline-functions']
    if legacy_flags is not None:
        defaults = legacy_flags
    if suite == 'builtins':
        sources += [path.with_name(path.stem + '-lib.c'), path.parent / 'lib/main.c']
        if not all(p.exists() for p in sources):
            return result('error', 'missing builtin companion source')
    # dg-options replaces IEEE_CFLAGS; dg-additional-options augments it.
    if replaces_default_options(src, args.target):
        defaults = []
    opts = [*base, *defaults, *flags]
    with tempfile.TemporaryDirectory(prefix='vcc-torture-run-') as wd:
        outputs = []
        for compiler in ('clang', 'vcc'):
            objects = []
            for i, source in enumerate(sources):
                obj, asm = f'{wd}/{compiler}-{i}.o', f'{wd}/{compiler}-{i}.s'
                cmd = ([tc.CLANG, *opts, '-c', str(source), '-o', obj] if compiler == 'clang'
                       else [tc.VPP, *opts, '-S', str(source), '-o', asm])
                rc, _, err = command(cmd, wd)
                if rc is None or rc < 0:
                    return result('error' if compiler == 'clang' else 'crash', f'{compiler} compile: {rc}: {tc.detail(err)}')
                if rc != 0:
                    return result('skip' if compiler == 'clang' else 'fail', f'{compiler} compile: {tc.detail(err)}')
                if compiler == 'vcc':
                    rc, _, err = command([tc.CLANG, f'--target={args.target}', '-c', asm, '-o', obj], wd)
                    if rc != 0:
                        return result('fail', 'vcc assembly: ' + tc.detail(err))
                objects.append(obj)
            exe = f'{wd}/{compiler}.exe'
            rc, _, err = command([tc.CLANG, f'--target={args.target}', '-fuse-ld=lld', '-no-pie', *objects, '-lm', '-o', exe], wd)
            if rc != 0:
                return result('skip' if compiler == 'clang' else 'fail', f'{compiler} link: {tc.detail(err)}')
            rc, out, err = command([*runner, exe], wd, args.timeout)
            if rc is None or rc != 0:
                return result('skip' if compiler == 'clang' else 'fail', f'{compiler} execution: {rc}: {tc.detail(err)}')
            outputs.append(out)
        if outputs[0] != outputs[1]:
            return result('fail', 'stdout differs from clang')
    return result('pass')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--target', choices=['x86_64-linux-gnu', 'aarch64-linux-gnu'], default='x86_64-linux-gnu')
    ap.add_argument('--suite', choices=['all', 'ieee', 'builtins'], default='all')
    ap.add_argument('--opt', choices=['0', '1', '2', '3', 's'], default='0')
    ap.add_argument('--timeout', type=int, default=10)
    ap.add_argument('--json', type=Path)
    ap.add_argument('-j', type=int, default=6)
    ap.add_argument('-v', action='store_true')
    ap.add_argument('pattern', nargs='?', default='')
    args = ap.parse_args()
    if args.j < 1 or args.timeout < 1:
        ap.error('jobs and timeout must be positive')
    p = subprocess.run([tc.CLANG, f'--target={args.target}', '-E', '-x', 'c', '-v', os.devnull], capture_output=True, text=True, check=True)
    listing = p.stderr.split('#include <...> search starts here:', 1)[1].split('End of search list.', 1)[0]
    includes = [arg for line in listing.splitlines() if line.strip() for arg in ('-isystem', line.strip())]
    base = [f'--target={args.target}', f'-O{args.opt}', '-fno-pie', '-std=gnu17', *tc.BASE[2:], *includes]
    runner = ['qemu-aarch64', '-L', '/usr/aarch64-linux-gnu'] if args.target.startswith('aarch64') else []
    suites = ['ieee', 'builtins'] if args.suite == 'all' else [args.suite]
    paths = [p for s in suites for p in sorted((ROOT / s).glob('*.c')) if not p.stem.endswith('-lib') and re.search(args.pattern, p.name)]
    if not paths:
        ap.error('no matching tests; fetch GCC with bench/fetch-testsuites.sh gcc')
    with ThreadPoolExecutor(max_workers=args.j) as pool:
        results = list(pool.map(lambda p: one(p, args, base, runner), paths))
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps({'target': args.target, 'optimization': args.opt, 'results': results}, indent=2) + '\n')
    if args.v:
        for r in results:
            if r['status'] != 'pass':
                print(f"{r['status']} {r['name']}: {r['reason']}")
    counts = Counter(r['status'] for r in results)
    for suite in suites:
        sub = Counter(r['status'] for r in results if r['name'].startswith(suite + '/'))
        print(suite + ': ' + ' '.join(f'{s}={sub[s]}' for s in ('pass', 'fail', 'crash', 'skip', 'error')))
    for (status, reason), count in Counter((r['status'], r['reason']) for r in results if r['status'] != 'pass').most_common(20):
        print(f'{count:4d} {status}: {reason}')
    print(' '.join(f'{s}={counts[s]}' for s in ('pass', 'fail', 'crash', 'skip', 'error')))
    return bool(counts['fail'] or counts['crash'] or counts['error'])


if __name__ == '__main__':
    sys.exit(main())
