#!/usr/bin/env python3
"""Hosted whole-object atomic regressions (requires libatomic and pthread)."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--bin-dir', type=Path, default=Path(os.environ.get('VCC_BIN', ROOT/'build/linux/src')))
p.add_argument('--target', choices=('x86_64-linux-gnu', 'aarch64-linux-gnu'), default='x86_64-linux-gnu')
p.add_argument('--clang', default=str(Path(os.environ.get('LLVM', '/usr/lib/llvm-23'))/'bin/clang'))
a = p.parse_args()
with tempfile.TemporaryDirectory(prefix='vcc-atomic-') as temp:
    temp = Path(temp)
    for name in ('complex_concurrent', 'wide', 'lock_free_runtime'):
        source = ROOT/'src/tests/atomic'/f'{name}.c'
        assembly, binary = temp/f'{name}.s', temp/name
        subprocess.run([str(a.bin_dir.resolve()/'vpp'), '--target='+a.target, '-std=c2y', '-O2', '-S', str(source), '-o', str(assembly)], check=True, timeout=30)
        subprocess.run([a.clang, '--target='+a.target, str(assembly), '-pthread', '-latomic', '-o', str(binary)], check=True, timeout=30)
        runner = ['qemu-aarch64', '-L', '/usr/aarch64-linux-gnu'] if a.target.startswith('aarch64') and os.uname().machine != 'aarch64' else []
        subprocess.run([*runner, str(binary)], check=True, timeout=30)
        print(f'{a.target}: {name} PASS', flush=True)
