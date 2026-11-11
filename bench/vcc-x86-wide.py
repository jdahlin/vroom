#!/usr/bin/env python3
"""Exercise AVX-512 intrinsics, including masked loads at a guard page.

Run on x86-64 Linux with Clang 23 headers: bench/vcc-x86-wide.py
The test is skipped when the host lacks the required CPU/OS features.
"""
import os
from pathlib import Path
import platform
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
os.chdir(root)
if platform.system() != 'Linux' or platform.machine() != 'x86_64':
    print('SKIP: requires x86-64 Linux')
    raise SystemExit(0)
flags = set(Path('/proc/cpuinfo').read_text().split('flags', 1)[1].split('\n', 1)[0].split())
needed = {'avx512f', 'avx512bw', 'avx512_vpopcntdq', 'xsave'}
if not needed <= flags:
    print('SKIP: host lacks ' + ', '.join(sorted(needed - flags)))
    raise SystemExit(0)
clang = os.environ.get('CLANG', '/usr/lib/llvm-23/bin/clang')
vpp = os.environ.get('VPP', str(root / 'build/linux/ccs/vpp'))
src = str(root / 'ccs/vcc/tests/x86-intrinsics/wide.c')
resource = subprocess.check_output([clang, '-print-resource-dir'], text=True).strip()
opts = ['--target=x86_64-linux-gnu', '-isystem', resource + '/include', '-isystem', '/usr/include/x86_64-linux-gnu', '-isystem', '/usr/include', '-O2', '-mavx512f', '-mavx512bw',
        '-mavx512vpopcntdq', '-mxsave']
with tempfile.TemporaryDirectory(prefix='vcc-x86-wide-') as tmp:
    for mode in ('clang', 'vpp-asm', 'vpp-obj'):
        obj, exe = f'{tmp}/{mode}.o', f'{tmp}/{mode}'
        if mode == 'vpp-asm':
            asm = f'{tmp}/test.s'
            subprocess.run([vpp, *opts, '-S', src, '-o', asm], check=True)
            subprocess.run([clang, '-c', asm, '-o', obj], check=True)
        else:
            subprocess.run([clang if mode == 'clang' else vpp, *opts, '-c', src, '-o', obj], check=True)
        subprocess.run([clang, obj, '-o', exe], check=True)
        subprocess.run([exe], check=True)
        print(f'{mode}: PASS')
