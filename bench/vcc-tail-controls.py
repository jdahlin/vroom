#!/usr/bin/env python3
"""Sibling-call flags and attributes survive mixed shared-header batch jobs.

Cross-compiles x86 and arm64 by default; no target runner or SDK is needed.
Every job has -fno-vir: the levels' flags are checked on the direct emitter's
sibling calls (VIR on x86-64 makes none yet: TODO.md).
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

from hostenv import BUILD

HEADER = r'''
#ifndef TC_CONTROL_HEADER
#define TC_CONTROL_HEADER
extern int ctl_callee(int);
__attribute__((__not_tail_called__)) int ctl_notail_gnu(int);
int ctl_notail_gnu(int);
[[clang::not_tail_called]] int ctl_notail_ns(int);
int ctl_notail_ns(int);
__attribute__((__returns_twice__)) int ctl_twice_callee(int);
int ctl_twice_callee(int);
__attribute__((noinline)) int ctl_plain(int a) { return ctl_callee(a); }
__attribute__((__disable_tail_calls__)) int ctl_disabled_gnu(int);
__attribute__((noinline)) int ctl_disabled_gnu(int a) { return ctl_callee(a); }
[[clang::disable_tail_calls]] int ctl_disabled_ns(int);
__attribute__((noinline)) int ctl_disabled_ns(int a) { return ctl_callee(a); }
__attribute__((__returns_twice__)) int ctl_twice_caller(int);
__attribute__((noinline)) int ctl_twice_caller(int a) { return ctl_callee(a); }
__attribute__((noinline)) int ctl_notail_gnu_use(int a) { return ctl_notail_gnu(a); }
__attribute__((noinline)) int ctl_notail_ns_use(int a) { return ctl_notail_ns(a); }
__attribute__((noinline)) int ctl_twice_use(int a) { return ctl_twice_callee(a); }
#endif
'''

VARIANTS = [
    (['-O0'], False), (['-O1'], True), (['-O2'], True), (['-O3'], True),
    (['-O2', '-fno-optimize-sibling-calls'], False),
    (['-O2', '-fno-optimize-sibling-calls', '-foptimize-sibling-calls'], True),
    (['-O2', '-foptimize-sibling-calls', '-fno-optimize-sibling-calls'], False),
    (['-fno-optimize-sibling-calls', '-O2'], False),
    (['-foptimize-sibling-calls', '-O0'], False),
    (['-O2', '-fno-optimize-sibling-calls', '-O3'], False),
    (['-O0', '-foptimize-sibling-calls', '-O2'], True),
    (['-O2', '-fno-optimize-sibling-calls', '-foptimize-sibling-calls',
      '-fno-optimize-sibling-calls'], False),
]


def body(assembly, name):
    match = re.search(r'^' + re.escape(name) + r':[^\n]*\n', assembly, re.M)
    end = re.search(r'^\s*\.size\s+' + re.escape(name) + r'\s*,', assembly[match.end():], re.M) if match else None
    if not match or not end:
        raise RuntimeError('missing function ' + name)
    return assembly[match.end():match.end() + end.start()]


def check(assembly, tail):
    text = body(assembly, 'ctl_plain')
    transfer = r'\b(?:jmpq?|b)\s+ctl_callee\b' if tail else r'\b(?:callq?|bl)\s+ctl_callee\b'
    if not re.search(transfer, text):
        raise RuntimeError(f'ctl_plain: expected {"tail" if tail else "ordinary"} call\n{text}')
    negatives = {'ctl_disabled_gnu': 'ctl_callee', 'ctl_disabled_ns': 'ctl_callee',
                 'ctl_twice_caller': 'ctl_callee', 'ctl_notail_gnu_use': 'ctl_notail_gnu',
                 'ctl_notail_ns_use': 'ctl_notail_ns', 'ctl_twice_use': 'ctl_twice_callee'}
    for name, callee in negatives.items():
        text = body(assembly, name)
        if not re.search(r'\b(?:callq?|bl)\s+' + re.escape(callee) + r'\b', text):
            raise RuntimeError(f'{name}: attribute lost across redeclaration or cache\n{text}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--vpp', default=str(Path(BUILD) / 'vpp'))
    parser.add_argument('--target', action='append', choices=['x86_64-linux-gnu', 'aarch64-linux-gnu'])
    args = parser.parse_args()
    targets = args.target or ['x86_64-linux-gnu', 'aarch64-linux-gnu']
    with tempfile.TemporaryDirectory(prefix='vcc-tail-controls-') as temp:
        directory = Path(temp)
        (directory / 'shared.h').write_text(HEADER)
        source = directory / 'cache.c'
        source.write_text('#include "shared.h"\n')
        for target in targets:
            for replay in (False, True):
                rows = []
                for i, (flags, _) in enumerate(VARIANTS):
                    rows.append('\x1f'.join(['-', '--target=' + target, '-std=c23',
                                             '-ffreestanding', '-fno-stack-protector', *flags, '-fno-vir',
                                             '-S', str(source), '-o', str(directory / f'job-{i}.s')]))
                jobs = directory / 'jobs'
                jobs.write_text('\n'.join(rows) + '\n')
                env = dict(os.environ, VCC_DR='1' if replay else '0', VCC_DR_GAIN='0')
                result = subprocess.run([args.vpp, '--batch', str(jobs), '-j', '1', '-C', temp],
                                        env=env, capture_output=True, text=True, timeout=60)
                if result.returncode:
                    raise RuntimeError(result.stdout + result.stderr)
                for i, (_, tail) in enumerate(VARIANTS):
                    try:
                        check((directory / f'job-{i}.s').read_text(), tail)
                    except RuntimeError as error:
                        raise RuntimeError(f'{target}, replay={int(replay)}, job={i}: {error}') from error
                print(f'{target}: 12 mixed flag/attribute jobs, replay={int(replay)} PASS')


if __name__ == '__main__':
    main()
