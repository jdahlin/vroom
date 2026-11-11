#!/usr/bin/env python3
"""Bounded unsigned power-of-two lowering checks, at every optimization level.

  bench/vcc-opt-rules.py --target x86_64-linux-gnu --run
  bench/vcc-opt-rules.py --target aarch64-linux-gnu --run
  bench/vcc-opt-rules.py --target wasm64-unknown-emscripten --run

Only --run requires target tools: clang/lld and native execution or qemu-aarch64,
or vcc/vlink and node (Node 24+ for wasm64 table64). No libc or SDK is used.
The wasm path needs neither Emscripten nor LLVM; --node selects the JS engine.
The ordinary C regression suites also run unsigned_pow2.c for correctness.
"""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

from hostenv import BUILD, R, llvm


def run(cmd):
    p = subprocess.run([str(x) for x in cmd], capture_output=True, text=True, timeout=60)
    if p.returncode:
        raise RuntimeError(f'{cmd}: exit {p.returncode}\n{p.stdout}{p.stderr}')
    return p.stdout


def body(assembly, name, wasm):
    m = re.search(r'^' + re.escape(name) + r':[^\n]*\n', assembly, re.M)
    if not m:
        raise RuntimeError(f'missing probe {name}')
    tail = assembly[m.end():]
    end = (re.search(r'^\s*end_function\b', tail, re.M) if wasm else
           re.search(r'^\s*\.size\s+' + re.escape(name) + r'\s*,', tail, re.M))
    if not end:
        raise RuntimeError(f'missing function end for {name}')
    return tail[:end.start()]


def check(assembly, arch):
    wasm = arch.startswith('wasm')
    divide = r'\b(?:i(?:32|64)\.(?:div|rem)_[su]|[su]div|idiv[lq]?|div[lq]?)\b'
    probes = ['opt_u32_div8', 'opt_u32_rem8', 'opt_u32_divhigh',
              'opt_u32_remhigh', 'opt_u32_div1', 'opt_u32_rem1',
              'opt_u64_div8', 'opt_u64_rem8', 'opt_u64_divhigh',
              'opt_u64_remhigh', 'opt_cast_div', 'opt_cast_rem',
              'opt_truncated_div', 'opt_signed_literal_div',
              'opt_compound_div', 'opt_compound_rem',
              'opt_compound_div64', 'opt_compound_rem64']
    for name in probes:
        text = body(assembly, name, wasm)
        if re.search(divide, text):
            raise RuntimeError(f'{name}: unsigned power-of-two retained divide/remainder\n{text}')
    # Both the optimized result and the discarded result must evaluate next().
    # The RHS comma controls additionally prevent treating a folded expression
    # as an effect-free literal and accidentally dropping its left operand.
    for name in ['effect_div', 'effect_rem', 'effect_zero',
                 'effect_denominator', 'effect_remdenominator']:
        text = body(assembly, name, wasm)
        calls = re.findall(r'\b(?:callq?|bl)\s+next\b', text)
        if len(calls) != 1:
            raise RuntimeError(f'{name}: expected exactly one call to next\n{text}')
    # These are outside this rule. Keep a genuine divide in the dynamic
    # control; signed division may legitimately use another correct sequence.
    text = body(assembly, 'dynamic_div', wasm)
    if not re.search(divide, text):
        raise RuntimeError(f'dynamic_div: missing dynamic divide\n{text}')


def execute(args, directory, assembly, arch, source, level):
    obj = directory / 'probe.o'
    exe = directory / ('probe.wasm' if arch.startswith('wasm') else 'probe')
    if arch.startswith('wasm'):
        run([args.vpp, f'--target={args.target}', f'-O{level}',
             '-ffreestanding', '-fno-stack-protector', '-c', source, '-o', obj])
        # vlink selects its wasm implementation through this executable name.
        # For wasm32 the input object's imported memory selects 32-bit layout.
        linker = directory / 'wasm-ld'
        if not linker.exists():
            linker.symlink_to(Path(shutil.which(args.vlink) or args.vlink).resolve())
        run([linker] + (['-mwasm64'] if arch == 'wasm64' else []) +
            ['--no-entry', '--export=main', obj, '-o', exe])
        script = directory / 'run.js'
        script.write_text('const fs=require("fs");\n'
                          'WebAssembly.instantiate(fs.readFileSync(process.argv[2]), {})'
                          '.then(({instance})=>{process.exit(instance.exports.main(0,' +
                          ('0n' if arch == 'wasm64' else '0') + '));})'
                          '.catch(e=>{console.error(e);process.exit(99);});\n')
        run([args.node, *args.node_flags, script, exe])
    else:
        assembler = args.vpp if arch == 'aarch64' else args.clang
        run([assembler, f'--target={args.target}', '-c', assembly, '-o', obj])
        start = directory / 'start.s'
        start.write_text('.text\n.globl _start\n_start:\n' +
                         ('call main\nmov %eax, %edi\nmov $60, %eax\nsyscall\n'
                          if arch == 'x86_64' else
                          'bl main\nmov x8, #93\nsvc #0\n'))
        run([args.clang, f'--target={args.target}', '-fuse-ld=lld',
             '-nostdlib', '-static', '-Wl,-e,_start', start, obj, '-o', exe])
        run(([args.qemu] if arch == 'aarch64' else []) + [exe])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--vpp', default=str(Path(BUILD) / 'vpp'))
    ap.add_argument('--target', default='x86_64-linux-gnu')
    ap.add_argument('--run', action='store_true', help='also execute literal-answer correctness checks')
    ap.add_argument('--clang', default=llvm('clang'))
    ap.add_argument('--vlink', default=str(Path(BUILD) / 'vlink'))
    ap.add_argument('--node', default='node')
    ap.add_argument('--qemu', default='qemu-aarch64')
    args = ap.parse_args()
    arch = args.target.split('-')[0]
    if arch not in ['x86_64', 'aarch64', 'wasm32', 'wasm64']:
        ap.error('target must be x86_64, aarch64, wasm32 or wasm64')
    args.node_flags = []
    if args.run and arch == 'wasm64':
        # Older V8 requires a flag; current Node enables memory64 by default
        # and rejects the removed experimental option. Table64 needs Node 24+.
        if '--experimental-wasm-memory64' in run([args.node, '--v8-options']):
            args.node_flags.append('--experimental-wasm-memory64')
    source = Path(R) / 'ccs/vcc/tests/vcc/unsigned_pow2.c'
    with tempfile.TemporaryDirectory(prefix='vcc-opt-rules-') as tmp:
        directory = Path(tmp)
        for level in range(4):
            assembly = directory / f'probe-O{level}.s'
            run([args.vpp, f'--target={args.target}', f'-O{level}',
                 '-ffreestanding', '-fno-stack-protector', '-S', source, '-o', assembly])
            check(assembly.read_text(), arch)
            if args.run:
                execute(args, directory, assembly, arch, source, level)
            print(f'{args.target} O{level}: codegen' + (' and execution' if args.run else '') + ' OK')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as e:
        print(e, file=sys.stderr)
        sys.exit(1)
