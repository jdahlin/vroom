#!/usr/bin/env python3
"""Run pinned GCC/LLVM ABI generators through real Linux cross-compiler calls.

LLVM=/usr/lib/llvm-23 bench/vcc-abi-generated.py --count 1000 -j 6
Repeat with --target aarch64-linux-gnu (qemu-aarch64 + cross glibc).
Each generated case compiles both halves independently, then runs clang/clang,
clang/vcc, vcc/clang and vcc/vcc. The clang-only program is the output oracle.
Unsupported clang inputs are counted separately, never as compiler passes.
Sources, commands, stderr and output remain under work/abi-generated for reduction.
LLVM enumerates nested records to depth 2; all upstream scalar/vector/complex
categories remain enabled. --start allows disjoint, reproducible slices.
"""
import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
import json
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys

from hostenv import BUILD, WORK, llvm

CLANG = llvm('clang')
VPP = os.path.join(os.environ.get('VCC_BIN') or BUILD, 'vpp')
SUITES = Path(WORK) / 'testsuites'
GCC = SUITES / 'gcc/gcc/testsuite/gcc.dg/compat'
LLVM = SUITES / 'llvm/clang/utils/ABITest/ABITestGen.py'


def command(cmd, cwd, log, timeout=30):
    with (cwd / 'commands.jsonl').open('a') as f:
        f.write(json.dumps([str(s) for s in cmd]) + '\n')
    try:
        p = subprocess.run(cmd, cwd=cwd, capture_output=True, timeout=timeout)
        (cwd / (log + '.stderr')).write_bytes(p.stderr)
        (cwd / (log + '.stdout')).write_bytes(p.stdout)
        return p.returncode, p.stdout, p.stderr.decode(errors='replace')
    except subprocess.TimeoutExpired:
        return None, b'', 'timeout'


def detail(err):
    return next((s.strip() for s in err.splitlines() if 'error:' in s), err.strip())[:500]


def generate_gcc(root, count):
    gen = root / 'gcc-generator'
    subprocess.run([CLANG, '-O2', *[str(GCC / s) for s in (
        'struct-layout-1_generate.c', 'generate-random.c', 'generate-random_r.c')], '-o', str(gen)], check=True)
    raw = root / 'gcc-inputs'
    raw.mkdir(exist_ok=True)
    subprocess.run([str(gen), '-s', str(GCC), '-d', str(raw), '-n', str(max(3000, count))], check=True)
    cases = {}
    for h in sorted(raw.glob('t*_test.h')):
        for line in h.read_text().splitlines():
            m = re.match(r'(?:T|U|TX|TCI|UCI|TXCI)\((\d+),', line)
            if m:
                cases[int(m[1])] = line
    return cases


def generate(suite, index, wd, gcc):
    if suite == 'gcc':
        (wd / 'case.h').write_text(gcc[index] + '\n')
        (wd / 'callee.c').write_text('#include "struct-layout-1_y1.h"\n#include "case.h"\n#include "struct-layout-1_y2.h"\n#include "case.h"\n')
        (wd / 'caller.c').write_text('#include "struct-layout-1_x1.h"\n#include "case.h"\n#include "struct-layout-1_x2.h"\n#include "case.h"\n')
        (wd / 'driver.c').write_text('#include "struct-layout-1.h"\n#define TX(n,type,attrs,fields,ops) extern void test##n(void);\n#include "case.h"\n#undef TX\nint main(void) {\n#define TX(n,type,attrs,fields,ops) test##n();\n#include "case.h"\nreturn fails != 0;\n}\n')
        return 0, ''
    rc, _, err = command([sys.executable, str(LLVM), '--max-record-depth=2',
        '--count=1', '--min=' + str(index), '--seed=1', '-o', str(wd / 'callee.c'),
        '-T', str(wd / 'caller.c'), '-D', str(wd / 'driver.c')], wd, 'generate')
    return rc, err


def one(suite, index, args, base, runner, gcc):
    wd = args.output / suite / str(index)
    wd.mkdir(parents=True, exist_ok=True)
    (wd / 'commands.jsonl').write_text('')
    def result(status, reason=''):
        return {'suite': suite, 'index': index, 'status': status, 'reason': reason, 'artifacts': str(wd)}
    rc, err = generate(suite, index, wd, gcc)
    if rc != 0:
        return result('error', 'generator: ' + detail(err))
    # Suppress unused decimal typedefs in the common header; actual decimal
    # cases still reach clang and are reported unsupported.
    opts = [*base, '-DSKIP_DECIMAL_FLOAT', '-DDBG', '-I', str(GCC)] if suite == 'gcc' else base
    objects = {}
    # Run the complete clang oracle before invoking vcc, so broken generated
    # programs and unsupported upstream extensions cannot become false failures.
    ref = None
    for compiler in ('clang', 'vcc'):
        for half in ('driver', 'caller', 'callee'):
            if half == 'driver' and compiler == 'vcc':
                continue
            obj = wd / f'{half}.{compiler}.o'
            asm = wd / f'{half}.{compiler}.s'
            cmd = [CLANG if compiler == 'clang' else VPP, *opts,
                   '-c' if compiler == 'clang' else '-S', str(wd / (half + '.c')),
                   '-o', str(obj if compiler == 'clang' else asm)]
            rc, _, err = command(cmd, wd, f'{half}.{compiler}')
            if rc is None or rc < 0:
                return result('error' if compiler == 'clang' else 'crash', f'{compiler} {half}: {rc}: {detail(err)}')
            if rc != 0:
                return result('unsupported' if compiler == 'clang' else 'fail', f'{compiler} {half}: {detail(err)}')
            if compiler == 'vcc':
                rc, _, err = command([CLANG, '--target=' + args.target, '-c', str(asm), '-o', str(obj)], wd, half + '.assemble')
                if rc != 0:
                    return result('fail', f'assemble {half}: {detail(err)}')
            objects[half, compiler] = obj
        pairs = [('clang', 'clang')] if compiler == 'clang' else [('clang', 'vcc'), ('vcc', 'clang'), ('vcc', 'vcc')]
        for caller, callee in pairs:
            name = caller + '-' + callee
            exe = wd / name
            rc, _, err = command([CLANG, '--target=' + args.target, '-fuse-ld=lld', '-no-pie',
                str(objects['driver', 'clang']), str(objects['caller', caller]), str(objects['callee', callee]),
                '-lm', '-o', str(exe)], wd, name + '.link')
            if rc != 0:
                return result('error' if compiler == 'clang' else 'fail', name + ' link: ' + detail(err))
            rc, out, err = command([*runner, str(exe)], wd, name + '.run', args.timeout)
            if rc != 0:
                return result('error' if compiler == 'clang' else 'fail', f'{name} execution: {rc}: {detail(err or out.decode(errors="replace"))}')
            if compiler == 'clang':
                ref = out
            elif out != ref:
                return result('fail', name + ' stdout differs')
    return result('pass')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--target', choices=['x86_64-linux-gnu', 'aarch64-linux-gnu'], default='x86_64-linux-gnu')
    ap.add_argument('--suite', choices=['gcc', 'llvm', 'all'], default='all')
    ap.add_argument('--count', type=int, default=100)
    ap.add_argument('--start', type=int, default=0)
    ap.add_argument('--timeout', type=int, default=10)
    ap.add_argument('--output', type=Path)
    ap.add_argument('-j', type=int, default=6)
    args = ap.parse_args()
    if args.count < 1 or args.start < 0 or not 1 <= args.j <= 6 or args.timeout < 1:
        ap.error('positive count/timeout, nonnegative start and 1..6 jobs required')
    args.output = (args.output or Path(WORK) / 'abi-generated' / args.target).resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    suites = ['gcc', 'llvm'] if args.suite == 'all' else [args.suite]
    if any(not (GCC if s == 'gcc' else LLVM).exists() for s in suites):
        ap.error('fetch pinned testsuites first: bench/fetch-testsuites.sh gcc llvm')
    p = subprocess.run([CLANG, '--target=' + args.target, '-E', '-x', 'c', '-v', os.devnull], capture_output=True, text=True, check=True)
    listing = p.stderr.split('#include <...> search starts here:', 1)[1].split('End of search list.', 1)[0]
    includes = [a for line in listing.splitlines() if line.strip() for a in ('-isystem', line.strip())]
    base = ['--target=' + args.target, '-O0', '-fno-pie', '-std=gnu11', '-w', *includes]
    if args.target.startswith('x86_64'):
        base += ['-mno-mmx']  # GCC generator's x86 DejaGNU option.
    runner = ['qemu-aarch64', '-L', '/usr/aarch64-linux-gnu'] if args.target.startswith('aarch64') else []
    gcc = generate_gcc(args.output, args.start + args.count) if 'gcc' in suites else None
    jobs = [(s, i) for s in suites for i in range(args.start, args.start + args.count)]
    with ThreadPoolExecutor(max_workers=args.j) as pool:
        results = list(pool.map(lambda job: one(*job, args, base, runner, gcc), jobs))
    report = {'target': args.target, 'vpp': VPP,
              'vpp_sha256': hashlib.sha256(Path(VPP).read_bytes()).hexdigest(),
              'clang': CLANG, 'start': args.start, 'count_per_generator': args.count,
              'llvm_record_depth': 2, 'llvm_seed': 1, 'results': results}
    (args.output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    for suite in suites:
        counts = Counter(r['status'] for r in results if r['suite'] == suite)
        print(suite + ': ' + ' '.join(f'{k}={counts[k]}' for k in ('pass', 'fail', 'crash', 'unsupported', 'error')))
    for (status, reason), n in Counter((r['status'], r['reason']) for r in results if r['status'] != 'pass').most_common(20):
        print(f'{n:4d} {status}: {reason}')
    print('Detailed results: ' + str(args.output / 'results.json'))
    return any(r['status'] in ('fail', 'crash', 'error') for r in results)


if __name__ == '__main__':
    sys.exit(main())
