#!/usr/bin/env python3
"""Measure only vpp child CPU time during kbuild (standalone, no server cache).

Prepare an out-of-tree kernel with LLVM first, then run:
  bench/vcc-kernel-time.py --source work/linux --out work/allmod -j 6
  bench/vcc-kernel-time.py --out work/allmod --replay -j 6
  bench/vcc-kernel-time.py --out work/allmod --replay --failed-only --vpp build/linux/src/vpp
Uses the existing config; does not clean or silently change it. Use a fresh
prepared tree for a complete measurement. Probes/preprocessing use the setup
compiler; every real target C/assembler compilation uses vpp, without fallback.
JSON records include argv for replay and failures, excluded from success timing.
user_s + sys_s is compiler CPU time. wall_s is summed per-job elapsed time,
not parallel build duration. make_wall_s includes all kbuild work. Filesystem
caches are not flushed; each compile starts a fresh compiler process.
Replay runs the recorded kernel jobs in one fresh vpp --batch process, with
shared caches and object output to /dev/null. Userspace samples are excluded.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import shlex
import shutil
import subprocess
import sys
import time


def compiler():
    args = sys.argv[2:]
    sources = [x for x in args if x.endswith(('.c', '.S', '.s')) and os.path.isfile(x)]
    if '-c' not in args or not sources or '-o' not in args:
        os.execv(os.environ['VKT_CLANG'], [os.environ['VKT_CLANG']] + args)
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    start = time.monotonic()
    try:
        status = subprocess.run([str(Path(os.environ['VKT_RECORDS']) / 'vpp')] + args,
                                timeout=120).returncode
    except subprocess.TimeoutExpired:
        status = 124
    elapsed = time.monotonic() - start
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    obj = args[args.index('-o') + 1]
    row = dict(source=sources[-1], output=obj, argv=args, cwd=os.getcwd(),
               status=status, wall_s=elapsed,
               user_s=after.ru_utime-before.ru_utime,
               sys_s=after.ru_stime-before.ru_stime)
    key = hashlib.sha256(obj.encode()).hexdigest() + f'-{os.getpid()}-{time.time_ns()}'
    Path(os.environ['VKT_RECORDS'], key + '.json').write_text(json.dumps(row) + '\n')
    sys.exit(status if status >= 0 else 128 - status)


def main():
    from hostenv import BUILD, llvm
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--source', type=Path)
    ap.add_argument('--out', required=True, type=Path)
    ap.add_argument('--vpp', type=Path, help='compiler to capture; replay defaults to the pinned binary')
    ap.add_argument('--clang', default=llvm('clang'), type=Path)
    ap.add_argument('-j', type=int, default=6)
    ap.add_argument('--replay', action='store_true')
    ap.add_argument('--failed-only', action='store_true', help='replay only failed kernel compilations')
    a = ap.parse_args()
    if not 1 <= a.j <= 64:
        ap.error('-j must be between 1 and 64')
    out = a.out.resolve()
    records = out / 'vcc-timing'
    if a.replay:
        return replay(records, out, a.j, a.vpp, a.failed_only)
    if a.failed_only:
        ap.error('--failed-only requires --replay')
    if a.source is None:
        ap.error('--source is required unless --replay is used')
    records.mkdir()  # Refuse to mix measurements from separate runs.
    shutil.copy2((a.vpp or Path(BUILD) / 'vpp').resolve(), records / 'vpp')
    env = dict(os.environ, VKT_CLANG=str(a.clang.resolve()), VKT_RECORDS=str(records))
    env['PATH'] = str(a.clang.resolve().parent) + ':' + env['PATH']
    wrapper = shlex.join([sys.executable, str(Path(__file__).resolve()), '--compiler'])
    cmd = ['make', '-C', str(a.source.resolve()), f'O={out}', 'ARCH=arm64',
           'LLVM=1', f'CC={wrapper}', f'-j{a.j}', '-k', 'vmlinux', 'modules']
    start = time.monotonic()
    with (records / 'make.log').open('w') as log:
        p = subprocess.run(cmd, env=env, stdout=log, stderr=subprocess.STDOUT)
    rows = [json.loads(f.read_text()) for f in sorted(records.glob('*.json'))]
    result = dict(make_status=p.returncode, make_wall_s=time.monotonic()-start,
                  jobs=len(rows), distinct_outputs=len({r['output'] for r in rows}),
                  command=cmd, vpp_sha256=hashlib.sha256((records / 'vpp').read_bytes()).hexdigest(),
                  config_sha256=hashlib.sha256(
                      (out / '.config').read_bytes()).hexdigest())
    for name, selected in [('success', [r for r in rows if r['status'] == 0]),
                           ('failed', [r for r in rows if r['status'] != 0])]:
        result[name] = dict(jobs=len(selected), **{k: sum(r[k] for r in selected)
                           for k in ('user_s', 'sys_s', 'wall_s')})
    (records / 'summary.txt').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    return p.returncode


def replay(records, out, threads, binary=None, failed_only=False):
    summary = json.loads((records / 'summary.txt').read_text())
    if hashlib.sha256((out / '.config').read_bytes()).hexdigest() != summary['config_sha256']:
        raise ValueError('kernel configuration changed since command capture')
    rows = [json.loads(f.read_text()) for f in sorted(records.glob('*.json'))]
    jobs = {}
    for row in rows:
        if '-D__KERNEL__' not in row['argv'] or (failed_only and row['status'] == 0):
            continue
        if Path(row['cwd']).resolve() != out:
            raise ValueError('recorded job working directory differs from --out')
        args = iter(row['argv'])
        clean = []
        for arg in args:
            if arg in ('-o', '-MF', '-MT', '-MQ'):
                next(args)
            elif arg not in ('-MD', '-MMD', '-MP') and not arg.startswith(('-Wp,-MMD,', '-Wp,-MD,')):
                clean.append(arg)
        jobs[row['output']] = ['-'] + clean  # batch defaults to -o /dev/null
    if not jobs:
        raise ValueError('no recorded kernel compilations')
    prefix = 'batch-failed' if failed_only else 'batch'
    with (records / f'{prefix}.jobs').open('w') as f:
        for key in sorted(jobs):
            if any('\n' in arg or '\x1f' in arg for arg in jobs[key]):
                raise ValueError('argument cannot be represented in the batch format')
            f.write('\x1f'.join(jobs[key]) + '\n')
    cmd = [str(binary.resolve() if binary else records / 'vpp'), '--batch', str(records / f'{prefix}.jobs'),
           '-j', str(threads), '-C', str(out), '--stats', str(records / f'{prefix}.stats')]
    with (records / f'{prefix}.log').open('w') as log:
        p = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT)
    print((records / f'{prefix}.log').read_text())
    return p.returncode


if __name__ == '__main__':
    if sys.argv[1:2] == ['--compiler']:
        compiler()
    sys.exit(main())
