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
resource = subprocess.check_output([llvm('clang'), '-print-resource-dir'], text=True).strip()
flags = ['--target=x86_64-linux-gnu', '-ffp-contract=off', '-fno-pie', '-isystem', resource + '/include']
required = {'vx_shift', 'vx_stack', 'vx_fp', 'vx_rotate', 'vx_f32', 'vx_f64', 'vx_i32', 'vx_sum', 'vx_series', 'vx_division', 'vx_srem', 'vx_udiv64', 'vx_udiv32'}
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
        if 'verifier failures' in p.stderr or 'allocation check:' in p.stderr:
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
    for filename, required_more, packed in (
        ('vir_vector_stride.c', {'stride', 'wide', 'reverse', 'ordered', 'residue', 'gather', 'carry', 'chain', 'maxval', 'maxindex', 'lastnegative'}, {'stride': 'mulps', 'reverse': 'addpd', 'ordered': 'mulps', 'residue': 'addps', 'gather': 'mulps', 'carry': 'mulps'}),
        ('vir_x86_dispatch.c', {'dense', 'signed_range', 'high', 'shigh', 'atom'}, {}),
        ('vir_x86_calls.c', {'ints', 'floats', 'caller', 'indirect', 'variadic_caller'}, {}),
        ('vir_x86_vectors.c', {'eq', 'ne', 'lt', 'le', 'gt', 'ge', 'compare', 'mul', 'cvt', 'cvt64', 'negate'},
         {'eq': 'cmpps', 'ne': 'cmpps', 'lt': 'cmpps', 'le': 'cmpps', 'gt': 'cmpps', 'ge': 'cmpps',
          'mul': 'pmuludq', 'cvt': 'cvtdq2ps', 'cvt64': 'unpcklpd', 'negate': 'psllq'})):
        extra = source.with_name(filename)
        run([llvm('clang'), *flags, '-O2', str(extra), '-no-pie', '-o', str(ref)])
        extra_expected = run([str(ref)]).stdout
        for vec in ('r', '0'):
            testenv = dict(env, VCC_VIR_VEC=vec)
            asm = d / 'extra.s'
            p = run([vpp, *flags, '-O2', '-S', str(extra), '-o', str(asm)], env=testenv)
            lowered = set(re.findall(r'vir: (\w+): lowered,', p.stderr))
            if not required_more <= lowered or 'verifier failures' in p.stderr or 'allocation check:' in p.stderr:
                raise SystemExit(f'{filename}: missing {required_more-lowered} or invalid VIR\n{p.stderr}')
            threaded = d / 'extra-threaded.s'
            run([vpp, *flags, '-O2', '-S', str(extra), '-o', str(threaded)],
                env=dict(testenv, VCC_THREADS='6'))
            if threaded.read_bytes() != asm.read_bytes():
                raise SystemExit(f'{filename}: assembly differs with six emitter threads')
            if vec != '0':
                if filename == 'vir_vector_stride.c':
                    loops = set(re.findall(r'vec: (\w+): loop b\d+: vectorized', p.stderr))
                    if not required_more <= loops:
                        raise SystemExit(f'{filename}: not vectorized: {required_more-loops}\n{p.stderr}')
                for name, op in packed.items():
                    body = asm.read_text().split(name + ':\n', 1)[1].split('.Lfunc_end', 1)[0]
                    if op not in body:
                        raise SystemExit(f'{name}: missing packed {op}')
            for obj in (False, True):
                inp = asm
                if obj:
                    inp = d / 'extra.o'
                    run([vpp, *flags, '-O2', '-c', str(extra), '-o', str(inp)], env=testenv)
                exe = d / 'extra'
                run([llvm('clang'), '-no-pie', str(inp), '-o', str(exe)])
                if run([str(exe)]).stdout != extra_expected:
                    raise SystemExit(f'{filename}: output differs')
    # Enabling VIR must have no effect below -O2.
    for opt in ('-O0', '-O1'):
        outputs = []
        for mode in ('0', 'bvls'):
            p = run([vpp, *flags, opt, '-S', str(source), '-o', '-'], env=dict(env, VCC_VIR=mode))
            outputs.append(p.stdout)
        if outputs[0] != outputs[1]:
            raise SystemExit(f'{opt}: VIR changed assembly')
print('x86 VIR: leaf/call/vector coverage, NaN comparisons, SSE2 multiply/conversions; assembly and own objects run; O0/O1 unchanged')
