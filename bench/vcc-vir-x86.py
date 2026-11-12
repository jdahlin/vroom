#!/usr/bin/env python3
"""Require real x86 VIR lowering and SSE2 vectorization, then run both assembly
and vpp's own object output.  No work/ inputs.  --bin-dir selects the compiler.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

from hostenv import BUILD, llvm

ROOT = Path(__file__).resolve().parent.parent
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--bin-dir', default=os.environ.get('VCC_BIN', BUILD))
a = ap.parse_args()
vpp = str(Path(a.bin_dir).resolve() / 'vpp')
source = ROOT / 'ccs/vcc/tests/vcc/vir_x86_leaf.c'
flags = ['--target=x86_64-linux-gnu', '-ffp-contract=off', '-fno-pie']
required = {'vx_shift', 'vx_stack', 'vx_fp', 'vx_rotate', 'vx_f32', 'vx_f64', 'vx_i32', 'vx_sum', 'vx_series'}
vector = {'vx_f32': 'mulps', 'vx_f64': 'mulpd', 'vx_i32': 'paddd', 'vx_sum': 'paddq', 'vx_series': 'paddq'}
env = dict(os.environ, VCC_VIR='bvls', VCC_VIR_CHECK='1', VCC_VIR_VEC='r', VCC_THREADS='1')
env.pop('VCC_VIR_LIST', None)

def run(cmd, **kw):
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=60, **kw)
    if p.returncode:
        raise SystemExit(f'failed ({p.returncode}): {" ".join(map(str, cmd))}\n{p.stdout}{p.stderr}')
    return p

with tempfile.TemporaryDirectory(prefix='vcc-vir-x86-') as tmp:
    d = Path(tmp)
    ref = d / 'ref'
    run([llvm('clang'), *flags, '-O2', str(source), '-no-pie', '-o', str(ref)])
    expected = run([str(ref)]).stdout
    for vec in ('r', '0'):
        testenv = dict(env, VCC_VIR_VEC=vec)
        asm = d / f'vec-{vec}.s'
        p = run([vpp, *flags, '-O2', '-S', str(source), '-o', str(asm)], env=testenv)
        lowered = set(re.findall(r'vir: (\w+): lowered,', p.stderr))
        if not required <= lowered:
            raise SystemExit(f'not lowered: {sorted(required - lowered)}\n{p.stderr}')
        if 'vx_fallback' in lowered or 'verifier failures' in p.stderr or 'allocation check:' in p.stderr:
            raise SystemExit(f'incorrect fallback or invalid VIR:\n{p.stderr}')
        threaded = d / 'threaded.s'
        run([vpp, *flags, '-O2', '-S', str(source), '-o', str(threaded)],
            env=dict(testenv, VCC_THREADS='6'))
        if threaded.read_bytes() != asm.read_bytes():
            raise SystemExit('assembly differs with six emitter threads')
        text = asm.read_text()
        if vec != '0':
            for name, opcode in vector.items():
                body = text.split(name + ':\n', 1)[1].split('.Lfunc_end', 1)[0]
                if opcode not in body:
                    raise SystemExit(f'{name}: missing {opcode}')
        for obj in (False, True):
            inp = asm
            if obj:
                inp = d / 'own.o'
                run([vpp, *flags, '-O2', '-c', str(source), '-o', str(inp)], env=testenv)
            exe = d / 'run'
            run([llvm('clang'), '-no-pie', str(inp), '-o', str(exe)])
            if run([str(exe)]).stdout != expected:
                raise SystemExit('output differs from clang')
    # Enabling VIR must have no effect below -O2.
    for opt in ('-O0', '-O1'):
        outputs = []
        for mode in ('0', 'bvls'):
            p = run([vpp, *flags, opt, '-S', str(source), '-o', '-'], env=dict(env, VCC_VIR=mode))
            outputs.append(p.stdout)
        if outputs[0] != outputs[1]:
            raise SystemExit(f'{opt}: VIR changed assembly')
print('x86 VIR: 9 functions lowered; 5 SSE2 loops; scalar/vector assembly and own objects run; O0/O1 unchanged')
