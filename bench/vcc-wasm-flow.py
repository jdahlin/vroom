#!/usr/bin/env python3
"""Structured wasm frame-store DSE: cross-join code shape and SDK-free runtime."""
import argparse
import importlib.util
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from hostenv import BUILD, R

spec = importlib.util.spec_from_file_location('opt_rules', Path(__file__).with_name('vcc-opt-rules.py'))
opt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(opt)


def count(text, name, instruction):
    function = opt.body(text, name, True)
    return len(re.findall(r'\b' + re.escape(instruction) + r'\b', function))


def check(text, optimized):
    positives = [('wf_diamond', 'i32.store'), ('wf_nested', 'i32.store'),
                 ('wf_block', 'i32.store'), ('wf_wide', 'i64.store'),
                 ('wf_float', 'f32.store'), ('wf_double', 'f64.store')]
    for name, instruction in positives:
        actual = count(text, name, instruction)
        if (optimized and actual != 1) or (not optimized and actual <= 1):
            expected = 'one final store' if optimized else 'multiple original stores'
            raise RuntimeError(f'{name}: expected {expected}, found {actual}\n' + opt.body(text, name, True))
    for name in ['wf_volatile', 'wf_atomic']:
        actual = count(text, name, 'i32.store')
        if actual != 4:
            raise RuntimeError(f'{name}: lost observable stores, found {actual}\n' + opt.body(text, name, True))
    function = opt.body(text, 'wf_rhs', True)
    if len(re.findall(r'\bcall\s+wf_effect\b', function)) != 3:
        raise RuntimeError(f'wf_rhs: lost effectful RHS evaluations\n{function}')
    if count(text, 'wf_call', 'i32.store') < 2:
        raise RuntimeError('wf_call: incorrectly crossed an unknown-call memory barrier')
    for name in ['wf_epoch_empty_else', 'wf_epoch_empty_then']:
        function = opt.body(text, name, True)
        if count(text, name, 'i32.store') != 3:
            raise RuntimeError(f'{name}: moved call barrier across an empty arm/join\n{function}')
        calls = list(re.finditer(r'\bcall\s+wf_epoch_barrier\b', function))
        if len(calls) != 1:
            raise RuntimeError(f'{name}: expected exactly one post-join barrier call\n{function}')
        joins = list(re.finditer(r'\bend_if\b', function))
        if not joins or calls[0].start() < joins[-1].end():
            raise RuntimeError(f'{name}: barrier call must follow the branch join\n{function}')
    # Reads, exits, escapes, overlapping cells and rejected control forms have
    # literal-answer runtime checks; their exact storage shape is not prescribed.


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--vpp', default=str(Path(BUILD) / 'vpp'))
    ap.add_argument('--vlink', default=str(Path(BUILD) / 'vlink'))
    ap.add_argument('--node', default='node')
    ap.add_argument('--target', choices=['wasm32', 'wasm64', 'all'], default='all')
    ap.add_argument('--compile-only', action='store_true')
    args = ap.parse_args()
    args.node_flags = []
    if not args.compile_only and '--experimental-wasm-memory64' in opt.run([args.node, '--v8-options']):
        args.node_flags = ['--experimental-wasm-memory64']
    source = Path(R) / 'src/test/vcc/wasm_frame_flow.c'
    for arch in (['wasm32', 'wasm64'] if args.target == 'all' else [args.target]):
        args.target = arch + '-unknown-emscripten'
        with tempfile.TemporaryDirectory(prefix='vcc-wasm-flow-') as tmp:
            directory = Path(tmp)
            for level in range(4):
                assembly = directory / f'flow-O{level}.s'
                command = [args.vpp, f'--target={args.target}', f'-O{level}', '-ffreestanding',
                           '-fno-stack-protector', '-S', source, '-o', assembly]
                opt.run(command)
                check(assembly.read_text(), optimized=level >= 1)
                if not args.compile_only:
                    opt.execute(args, directory, assembly, arch, source, level)
                print(f'{arch} structured frame DSE O{level}: OK')
            assembly = directory / 'flow-noopt.s'
            command = [args.vpp, f'--target={args.target}', '-O2', '-ffreestanding',
                       '-fno-stack-protector', '-S', source, '-o', assembly]
            result = subprocess.run([str(x) for x in command], capture_output=True, text=True,
                                    timeout=60, env={**os.environ, 'VCC_NOOPT': '1'})
            if result.returncode:
                raise RuntimeError(f'VCC_NOOPT compile failed:\n{result.stdout}{result.stderr}')
            check(assembly.read_text(), optimized=False)
            print(f'{arch} structured frame DSE O2 VCC_NOOPT: OK')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as e:
        print(e, file=sys.stderr)
        sys.exit(1)
