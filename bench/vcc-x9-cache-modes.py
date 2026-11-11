#!/usr/bin/env python3
"""Interleave language and target-feature contexts through one header cache."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
from hostenv import BUILD, llvm

vpp = os.environ.get('VPP', str(Path(BUILD) / 'vpp'))
clang = os.environ.get('CLANG', llvm('clang'))
with tempfile.TemporaryDirectory(prefix='vcc-x9-cache-') as tmp:
    d = Path(tmp)
    (d / 'shared.h').write_text('''#define S0(x) #x
#define S(x) S0(x)
#define JOIN(a,b) S(a %:%: b)
const char *value(void) { return JOIN(1,2); }
#if __has_builtin(__builtin_ia32_crc32di)
int feature(void) { return 1; }
#else
int feature(void) { return 0; }
#endif
''')
    modes = [(std, feature) for std in ('c89', 'c99') for feature in ('-mno-crc32', '-mcrc32')]
    refs = {}
    jobs = []
    for turn in range(3):
        for i, (std, feature) in enumerate(modes):
            src = d / f'unit-{turn}-{i}.c'
            out = d / f'unit-{turn}-{i}.s'
            src.write_text('#include "shared.h"\n')
            flags = ['--target=x86_64-linux-gnu', '-std=' + std, feature]
            if turn == 0:
                results = []
                for cc in (clang, vpp):
                    r = subprocess.run([cc, *flags, '-E', '-P', str(src)], capture_output=True, text=True, check=True)
                    results.append(' '.join(r.stdout.split()))
                if results[0] != results[1]:
                    raise RuntimeError(f'Clang preprocessing differs for {std} {feature}: {results}')
                subprocess.run([vpp, *flags, '-S', str(src), '-o', str(out)], check=True)
                refs[i] = out.read_bytes()
            jobs.append((i, out, ['-', *flags, '-S', str(src), '-o', str(out)]))
    if len(set(refs.values())) != 4:
        raise RuntimeError('test did not distinguish all language/feature contexts')
    jobfile = d / 'jobs'
    jobfile.write_text(''.join('\x1f'.join(args) + '\n' for _, _, args in jobs))
    run = subprocess.run([vpp, '--batch', str(jobfile), '-j', '1', '-C', str(d)],
                         capture_output=True, text=True, check=True)
    log = run.stdout + run.stderr
    if not re.search(r'header cache: [1-9][0-9]* hits', log):
        raise RuntimeError('header cache was not used: ' + log)
    for i, out, _ in jobs:
        if out.read_bytes() != refs[i]:
            raise RuntimeError('mixed language/feature cache mismatch: ' + str(out))
    print('4 language/feature combinations, 12 interleaved jobs: Clang and cache agree')
