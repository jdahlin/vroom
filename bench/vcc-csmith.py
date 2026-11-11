#!/usr/bin/env python3
"""Reproducible Csmith/YARPGen differential tests on Linux.

LLVM=/usr/lib/llvm-23 bench/vcc-csmith.py --build --generator all --count 3
Repeat with --target aarch64-linux-gnu (qemu-aarch64 + cross glibc).
Every sample retains generated sources, compiler commands, stdout/stderr and a
result.json under work/random-tests. Clang -O0 and -O2 must agree before a vcc
result is compared; a Clang UBSan trap build must also pass and agree. This is a bounded smoke runner, not evidence of the S7 24h
gate. YARPGen defaults to at most two array dimensions to bound smoke-test
memory; --max-array-dims=0 restores upstream defaults. Use --seed to replay a retained sample with the same pinned generator.
CMAKE may name a locally installed cmake executable.
"""
import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import time

from hostenv import BUILD, WORK, llvm

ROOT = Path(__file__).resolve().parent.parent
GENROOT = ROOT / 'build/generators'
SOURCES = Path(WORK) / 'testsuites'


def command(cmd, cwd, timeout):
    start = time.monotonic()
    try:
        p = subprocess.run(list(map(str, cmd)), cwd=cwd, capture_output=True,
                           timeout=timeout)
        rc, out, err = p.returncode, p.stdout, p.stderr
    except OSError as e:
        rc, out, err = 127, b'', str(e).encode()
    except subprocess.TimeoutExpired as e:
        rc, out, err = None, e.stdout or b'', e.stderr or b''
    return {'command': list(map(str, cmd)), 'returncode': rc,
            'seconds': time.monotonic() - start,
            'stdout': out.decode(errors='replace'),
            'stderr': err.decode(errors='replace')}


def build(generator, jobs):
    cmake = os.environ.get('CMAKE', 'cmake')
    dest = GENROOT / generator
    cmds = [[cmake, '-S', SOURCES / generator, '-B', dest,
             '-DCMAKE_BUILD_TYPE=Release']]
    if generator == 'csmith':
        cmds[0].append(f'-DCMAKE_INSTALL_PREFIX={GENROOT}/csmith-install')
    cmds.append([cmake, '--build', dest, '-j', str(jobs)])
    if generator == 'csmith':
        cmds.append([cmake, '--install', dest])
    for cmd in cmds:
        subprocess.run(list(map(str, cmd)), check=True)


def one(generator, seed, args, base, runner, runroot):
    wd = runroot / generator / str(seed)
    wd.mkdir(parents=True)
    records = []
    result = {'generator': generator, 'seed': seed, 'target': args.target,
              'commands': records}

    def run(cmd, timeout):
        r = command(cmd, wd, timeout)
        records.append(r)
        return r

    def done(status, reason=''):
        result.update(status=status, reason=reason)
        (wd / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
        return result

    if generator == 'csmith':
        r = run([GENROOT / 'csmith-install/bin/csmith', '--seed', seed], args.generate_timeout)
        (wd / 'test.c').write_text(r['stdout'])
        extra = ['-I', str(GENROOT / 'csmith-install/include')]
        # The versioned installed runtime directory contains csmith.h.
        headers = list((GENROOT / 'csmith-install/include').rglob('csmith.h'))
        if not headers:
            return done('error', 'missing Csmith runtime headers; use --build')
        extra = ['-I', str(headers[0].parent)]
    else:
        r = run([GENROOT / 'yarpgen/yarpgen', '--std=c', '--seed=' + str(seed),
                 '--max-array-dims=' + str(args.max_array_dims),
                 '--out-dir=' + str(wd)], args.generate_timeout)
        extra = []
    if r['returncode'] != 0:
        return done('error', 'generator failed or timed out')
    sources = sorted(wd.glob('*.c'))
    if not sources or any(not p.stat().st_size for p in sources):
        return done('error', 'generator produced no C source')
    result['sources'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
    outputs = []
    for compiler in ('clang-O0', 'clang-O2', 'clang-ubsan', 'vcc'):
        objs = []
        for i, source in enumerate(sources):
            obj = wd / f'{compiler}-{i}.o'
            opts = [*base, *extra, '-O2' if compiler == 'clang-O2' else '-O0']
            if compiler == 'clang-ubsan':
                opts += ['-fsanitize=undefined', '-fsanitize-trap=undefined']
            if compiler == 'vcc':
                asm = wd / f'vcc-{i}.s'
                r = run([args.vpp, *opts, '-S', source, '-o', asm], args.compile_timeout)
                if r['returncode'] == 0:
                    r = run([args.clang, f'--target={args.target}', '-c', asm, '-o', obj], args.compile_timeout)
            else:
                r = run([args.clang, *opts, '-c', source, '-o', obj], args.compile_timeout)
            if r['returncode'] != 0:
                return done('fail' if compiler == 'vcc' else 'skip', compiler + ' compile failed or timed out')
            objs.append(obj)
        exe = wd / compiler
        r = run([args.clang, f'--target={args.target}', '-fuse-ld=lld', '-no-pie',
                 *objs, '-lm', '-o', exe], args.compile_timeout)
        if r['returncode'] != 0:
            return done('fail' if compiler == 'vcc' else 'skip', compiler + ' link failed or timed out')
        r = run([*runner, exe], args.timeout)
        if r['returncode'] != 0 or not r['stdout'].strip():
            return done('fail' if compiler == 'vcc' else 'skip', compiler + ' run failed, timed out or had no checksum')
        outputs.append(r['stdout'])
        if compiler.startswith('clang-') and outputs[0] != outputs[-1]:
            return done('skip', 'clang oracle disagreement; investigate oracle or undefined behavior')
    return done('pass' if outputs[0] == outputs[-1] else 'fail',
                '' if outputs[0] == outputs[-1] else 'checksum differs from all clang oracles')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--generator', choices=['csmith', 'yarpgen', 'all'], default='all')
    ap.add_argument('--target', choices=['x86_64-linux-gnu', 'aarch64-linux-gnu'], default='x86_64-linux-gnu')
    ap.add_argument('--build', action='store_true')
    ap.add_argument('--count', type=int, default=3)
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--max-array-dims', type=int, default=2,
                    help='YARPGen array dimensions (default 2 for bounded smoke; 0 is upstream default)')
    ap.add_argument('-j', type=int, default=2)
    ap.add_argument('--timeout', type=int, default=10)
    ap.add_argument('--compile-timeout', type=int, default=60)
    ap.add_argument('--generate-timeout', type=int, default=30)
    ap.add_argument('--out', type=Path, default=Path(WORK) / 'random-tests')
    args = ap.parse_args()
    if min(args.count, args.seed, args.j, args.timeout, args.compile_timeout, args.generate_timeout) < 1 or args.j > 6:
        ap.error('counts, seed and timeouts must be positive; jobs must be 1..6')
    if args.max_array_dims < 0:
        ap.error('max-array-dims cannot be negative')
    if args.target.startswith('x86_64') and platform.machine() not in ('x86_64', 'amd64'):
        ap.error('x86 native runner requires an x86_64 host')
    generators = ['csmith', 'yarpgen'] if args.generator == 'all' else [args.generator]
    if args.build:
        for generator in generators:
            build(generator, args.j)
    args.clang = llvm('clang')
    source_vpp = Path(os.environ.get('VCC_BIN') or BUILD) / 'vpp'
    runroot = args.out.resolve() / (time.strftime('%Y%m%d-%H%M%S') + f'-{os.getpid()}-{args.target}')
    runroot.mkdir(parents=True)
    # Each run uses a fixed compiler even if another agent rebuilds the tools.
    args.vpp = runroot / 'vpp'
    shutil.copy2(source_vpp, args.vpp)
    r = command([args.clang, f'--target={args.target}', '-E', '-x', 'c', '-v', os.devnull], runroot, 30)
    if r['returncode'] != 0 or '#include <...> search starts here:' not in r['stderr']:
        ap.error('cannot discover target include paths')
    listing = r['stderr'].split('#include <...> search starts here:', 1)[1].split('End of search list.', 1)[0]
    includes = [a for line in listing.splitlines() if line.strip() for a in ('-isystem', line.strip())]
    base = [f'--target={args.target}', '-std=gnu11', '-fno-pie', *includes]
    runner = ['qemu-aarch64', '-L', '/usr/aarch64-linux-gnu'] if args.target.startswith('aarch64') else []
    manifest = {'target': args.target, 'seed': args.seed, 'count': args.count,
                'vpp_sha256': hashlib.sha256(args.vpp.read_bytes()).hexdigest(),
                'clang': command([args.clang, '--version'], runroot, 10),
                'suite_manifest': (SOURCES / 'MANIFEST').read_text(),
                'max_array_dims': args.max_array_dims, 'started': time.time()}
    jobs = [(g, seed) for g in generators for seed in range(args.seed, args.seed + args.count)]
    with ThreadPoolExecutor(max_workers=args.j) as pool:
        results = list(pool.map(lambda job: one(*job, args, base, runner, runroot), jobs))
    manifest.update(finished=time.time(), results=results)
    (runroot / 'summary.json').write_text(json.dumps(manifest, indent=2) + '\n')
    for r in results:
        print(f"{r['generator']} seed={r['seed']} {r['status']}: {r['reason']}")
    counts = Counter(r['status'] for r in results)
    print(' '.join(f'{s}={counts[s]}' for s in ('pass', 'fail', 'skip', 'error')))
    print('Evidence:', runroot)
    return int(bool(counts['fail'] or counts['error'] or not counts['pass']))


if __name__ == '__main__':
    sys.exit(main())
