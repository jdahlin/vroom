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
# Shared loop/join regressions use the native Linux C library headers.
if Path('/usr/include/x86_64-linux-gnu').is_dir():
    flags += ['-isystem', '/usr/include/x86_64-linux-gnu', '-isystem', '/usr/include']
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
        ('vir_break_dup.c', {'run'}, {}),
        ('vir_loop_rotate.c', {'sum', 'nest', 'skips', 'twoways', 'str', 'middle', 'counted'}, {}),
        ('vir_join_dup.c', {'search', 'chain', 'onearm', 'cmp1', 'cmp2'}, {}),
        ('vir_low32.c', {'lowloop', 'wideloop', 'lowselect', 'wideselect', 'narrowarg', 'widearg'}, {}),
        ('vir_x86_scratch.c', {'rotate', 'negd', 'absd', 'negf', 'absf'}, {}),
        ('vir_x86_frame.c', {'frames', 'pressure', 'callee'}, {}),
        ('vir_x86_cmpmem.c', {'left', 'right', 'full32', 'sign8', 'zero8', 'sign16', 'indexed', 'keep', 'vol', 'outside', 'narrow'}, {}),
        ('vir_x86_testmem.c', {'flag8', 'flag16', 'flag32', 'flag64', 'indexed', 'vol', 'sign', 'shared', 'sharedmask', 'member', 'keep'}, {}),
        ('vir_x86_blocks.c', {'copy1', 'copy24', 'copy127', 'copy128', 'copy129', 'zero24', 'zero256', 'zero257', 'zero_local', 'readbyte', 'dynamic', 'nonzero', 'lowzero'}, {}),
        ('vir_x86_ext.c', {'clean', 'trim', 'mask', 'signedload', 'fromcall'}, {}),
        ('vir_x86_select.c', {'choose', 'uchoose', 'keep', 'boolean', 'pointer'}, {}),
        ('vir_x86_bytes.c', {'be16', 'be32', 'signed32', 'signedlow32', 'signedvol32', 'be64', 'be48', 'offset32', 'bev64', 'indexed16', 'indexed32', 'bev16', 'signed16', 'changed'}, {}),
        ('vir_x86_inline.c', {'work'}, {}),
        ('vir_inline_context.c', {'wrapped', 'layer1', 'layer2', 'constant0', 'constant1', 'general', 'ignored0', 'ignored1'}, {}),
        ('vir_inline_large.c', {'helper', 'large', 'reference'}, {}),
        ('vir_x86_nsw_index.c', {'be16p', 'be16cast', 'be32cast', 'be64cast', 'load4signed', 'store4signed', 'shared', 'keep_input', 'rawshift', 'vol', 'min32', 'min64', 'clean16', 'clean8', 'change', 'pressure_indices'}, {}),
        ('vir_x86_index.c', {'load8', 'load4', 'load2', 'load1', 'store8', 'store4', 'cast8', 'cast4', 'cast2', 'caststore4', 'castmod', 'cast3', 'sharedcast'}, {}),
        ('vir_vector_stride.c', {'stride', 'intstride', 'wide', 'reverse', 'ordered', 'residue', 'gather', 'carry', 'chain', 'maxval', 'maxindex', 'lastnegative'}, {'stride': 'mulps', 'intstride': 'mulps', 'reverse': 'addpd', 'ordered': 'mulps', 'residue': 'addps', 'gather': 'mulps', 'carry': 'mulps'}),
        ('vir_x86_dispatch.c', {'dense', 'byte_switch', 'signed_range', 'high', 'shigh', 'atom'}, {}),
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
            if filename == 'vir_inline_large.c':
                body = asm.read_text().split('large:\n', 1)[1].split('.Lfunc_end', 1)[0]
                if not re.search(r'\bcall\w*\s+helper\b', body):
                    raise SystemExit('large: once-only bonus copied an oversized helper into a large caller')
            if filename == 'vir_x86_inline.c':
                body = asm.read_text().split('work:\n', 1)[1].split('.Lfunc_end', 1)[0]
                if re.search(r'\bcall\w*\b', body):
                    raise SystemExit('work: single-call helper was not inlined')
            if filename == 'vir_inline_context.c':
                bodies = {name: asm.read_text().split(name + ':\n', 1)[1].split('.Lfunc_end', 1)[0]
                          for name in required_more}
                for name in ('constant0', 'constant1', 'wrapped'):
                    if re.search(r'\bcall\w*\b', bodies[name]):
                        raise SystemExit(f'{name}: selected helper was not inlined')
                for name, target in [('layer1', 'wrapped'), ('layer2', 'wrapped'), ('general', 'specialize'),
                                     ('ignored0', 'unused'), ('ignored1', 'unused')]:
                    if not re.search(r'\bcall\w*\s+' + target + r'\b', bodies[name]):
                        raise SystemExit(f'{name}: helper with no applicable inline budget was copied')
            if filename == 'vir_x86_select.c':
                for name in required_more:
                    body = asm.read_text().split(name + ':\n', 1)[1].split('.Lfunc_end', 1)[0]
                    if 'cmov' not in body:
                        raise SystemExit(f'{name}: missing conditional move')
            if filename == 'vir_x86_dispatch.c':
                body = asm.read_text().split('byte_switch:\n', 1)[1].split('.Lfunc_end', 1)[0]
                if not re.search(r'movzbl\s+[^\n]+, %r10d', body):
                    raise SystemExit('byte_switch: selector was not loaded into dispatch register')
                if re.search(r'mov[lq]\s+%r10d?,', body):
                    raise SystemExit('byte_switch: dead selector was copied or unnecessarily widened')
            if filename == 'vir_x86_ext.c':
                body = asm.read_text().split('mask:\n', 1)[1].split('.Lfunc_end', 1)[0]
                if 'movslq' in body:
                    raise SystemExit('mask: nonnegative masked value was sign-extended')
            if filename == 'vir_x86_blocks.c':
                for name in ('copy24', 'zero24', 'zero_local'):
                    body = asm.read_text().split(name + ':\n', 1)[1].split('.Lfunc_end', 1)[0]
                    if 'rep ' in body or 'movups' not in body:
                        raise SystemExit(f'{name}: missing direct SSE2 block accesses')
                for name in ('copy129', 'zero257', 'dynamic', 'nonzero'):
                    body = asm.read_text().split(name + ':\n', 1)[1].split('.Lfunc_end', 1)[0]
                    if 'rep ' not in body and not re.search(r'\bcall\w*\s+mem(?:cpy|set)', body):
                        raise SystemExit(f'{name}: missing generic block-operation fallback')
            if filename == 'vir_x86_cmpmem.c':
                for name, op in [('left', 'cmpq'), ('right', 'cmpq'), ('full32', 'cmpl'), ('sign8', 'cmpb'), ('zero8', 'cmpb'), ('sign16', 'cmpw'), ('indexed', 'cmpq')]:
                    body = asm.read_text().split(name + ':\n', 1)[1].split('.Lfunc_end', 1)[0]
                    if not re.search(op + r'\s+[^\n]*\(%', body):
                        raise SystemExit(f'{name}: missing direct memory comparison')
                for name in ('keep', 'vol', 'narrow'):
                    body = asm.read_text().split(name + ':\n', 1)[1].split('.Lfunc_end', 1)[0]
                    if re.search(r'cmp[bwlq]\s+[^\n]*\(%', body):
                        raise SystemExit(f'{name}: unsafe memory comparison fold')
            if filename == 'vir_x86_testmem.c':
                for name, op in [('flag8', 'testb'), ('flag16', 'testw'), ('flag32', 'testl'), ('flag64', 'testq')]:
                    body = asm.read_text().split(name + ':\n', 1)[1].split('.Lfunc_end', 1)[0]
                    if not re.search(op + r'\s+\$[^,]+,.*\(%', body):
                        raise SystemExit(f'{name}: missing direct memory test')
                body = asm.read_text().split('vol:\n', 1)[1].split('.Lfunc_end', 1)[0]
                if len(re.findall(r'movzbl\s+\(', body)) != 1 or re.search(r'testb\s+\$[^,]+,.*\(%', body):
                    raise SystemExit('vol: volatile load was combined')
            if filename == 'vir_x86_frame.c':
                body = asm.read_text().split('pressure:\n', 1)[1].split('.Lfunc_end', 1)[0]
                if re.search(r'pushq\s+%rbp', body) or not re.search(r'\.cfi_offset\s+%rbp', body):
                    raise SystemExit('pressure: rbp was not allocated with a stack-relative frame')
            if filename == 'vir_x86_bytes.c':
                body = asm.read_text().split('be16:\n', 1)[1].split('.Lfunc_end', 1)[0]
                if 'rorw' not in body or 'movzwl' not in body:
                    raise SystemExit('be16: missing combined word read and byte rotate')
                body = asm.read_text().split('be32:\n', 1)[1].split('.Lfunc_end', 1)[0]
                if 'bswapl' not in body:
                    raise SystemExit('be32: missing combined word read and byte swap')
                body = asm.read_text().split('signed32:\n', 1)[1].split('.Lfunc_end', 1)[0]
                if 'bswapl' not in body or 'movslq' not in body:
                    raise SystemExit('signed32: missing combined signed word read')
                for name in ('signedlow32', 'signedvol32'):
                    body = asm.read_text().split(name + ':\n', 1)[1].split('.Lfunc_end', 1)[0]
                    if 'bswap' in body:
                        raise SystemExit(f'{name}: invalid combined word read')
                for name, opcode in [('be64', 'bswapq'), ('be48', 'bswapl'), ('offset32', 'bswapl')]:
                    body = asm.read_text().split(name + ':\n', 1)[1].split('.Lfunc_end', 1)[0]
                    if opcode not in body:
                        raise SystemExit(f'{name}: missing combined read and byte swap')
                body = asm.read_text().split('bev64:\n', 1)[1].split('.Lfunc_end', 1)[0]
                if body.count('movzbl') != 8 or 'bswap' in body:
                    raise SystemExit('bev64: volatile byte reads were combined')
                for name in ('indexed16', 'indexed32'):
                    body = asm.read_text().split(name + ':\n', 1)[1].split('.Lfunc_end', 1)[0]
                    if not re.search(r'\(%\w+,%\w+,1\)', body):
                        raise SystemExit(f'{name}: missing folded indexed read')
                body = asm.read_text().split('bev16:\n', 1)[1].split('.Lfunc_end', 1)[0]
                if body.count('movzbl') != 2 or 'rorw' in body:
                    raise SystemExit('bev16: volatile byte reads were combined')
            if filename == 'vir_x86_nsw_index.c':
                for name in ('clean16', 'clean8'):
                    body = asm.read_text().split(name + ':\n', 1)[1].split('.Lfunc_end', 1)[0]
                    if 'movslq' in body:
                        raise SystemExit(f'{name}: loaded index was sign-extended again')
                for name, scale in [('be16p', 2), ('be16cast', 2), ('be32cast', 4), ('be64cast', 8), ('load4signed', 4), ('store4signed', 4)]:
                    body = asm.read_text().split(name + ':\n', 1)[1].split('.Lfunc_end', 1)[0]
                    if not re.search(r'\(%\w+,%\w+,' + str(scale) + r'\)', body):
                        raise SystemExit(f'{name}: missing exact scaled address')
                body = asm.read_text().split('rawshift:\n', 1)[1].split('.Lfunc_end', 1)[0]
                if 'shll' not in body:
                    raise SystemExit('rawshift: unproven signed shift was folded')
                body = asm.read_text().split('vol:\n', 1)[1].split('.Lfunc_end', 1)[0]
                if body.count('movzbl') != 2 or 'rorw' in body:
                    raise SystemExit('vol: volatile reads were combined')
            if filename == 'vir_x86_index.c':
                for name, scale in [('load8', 8), ('load4', 4), ('load2', 2), ('load1', 1), ('store8', 8), ('store4', 4), ('cast8', 8), ('cast4', 4), ('cast2', 2), ('caststore4', 4), ('castmod', 8)]:
                    body = asm.read_text().split(name + ':\n', 1)[1].split('.Lfunc_end', 1)[0]
                    if not re.search(r'\(%\w+,%\w+,' + str(scale) + r'\)', body):
                        raise SystemExit(f'{name}: missing indexed memory operand with scale {scale}')
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
        if filename == 'vir_x86_nsw_index.c':
            wrapflags = [*flags, '-O2', '-fwrapv', '-DWRAP_MODE']
            run([llvm('clang'), *wrapflags, str(extra), '-no-pie', '-o', str(ref)])
            wrap_expected = run([str(ref)]).stdout
            asm = d / 'wrap.s'
            run([vpp, *wrapflags, '-S', str(extra), '-o', str(asm)], env=env)
            body = asm.read_text().split('be16cast:\n', 1)[1].split('.Lfunc_end', 1)[0]
            if 'shll' not in body:
                raise SystemExit('be16cast: -fwrapv product was moved past sign extension')
            exe = d / 'wrap'
            run([llvm('clang'), '-no-pie', str(asm), '-o', str(exe)])
            if run([str(exe)]).stdout != wrap_expected:
                raise SystemExit('wrapping index differs from clang')
        if filename == 'vir_x86_frame.c':
            asm = d / 'keep-frame.s'
            run([vpp, *flags, '-O2', '-fno-omit-frame-pointer', '-S', str(extra), '-o', str(asm)], env=env)
            body = asm.read_text().split('pressure:\n', 1)[1].split('.Lfunc_end', 1)[0]
            if not re.search(r'pushq\s+%rbp', body):
                raise SystemExit('pressure: -fno-omit-frame-pointer was ignored')
            exe = d / 'keep-frame'
            run([llvm('clang'), '-no-pie', str(asm), '-o', str(exe)])
            if run([str(exe)]).stdout != extra_expected:
                raise SystemExit('pressure: frame-pointer mode failed')
    # Enabling VIR must have no effect below -O2.
    for opt in ('-O0', '-O1'):
        outputs = []
        for mode in ('0', 'bvls'):
            p = run([vpp, *flags, opt, '-S', str(source), '-o', '-'], env=dict(env, VCC_VIR=mode))
            outputs.append(p.stdout)
        if outputs[0] != outputs[1]:
            raise SystemExit(f'{opt}: VIR changed assembly')
print('x86 VIR: leaf/call/vector coverage, NaN comparisons, SSE2 multiply/conversions; assembly and own objects run; O0/O1 unchanged')
