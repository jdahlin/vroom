#!/usr/bin/env python3
"""Bounded switch membership: guarded shifts, unchanged bodies and fallbacks."""
import argparse
import importlib.util
import os
from pathlib import Path
import re
import tempfile

from hostenv import BUILD, R, llvm

spec = importlib.util.spec_from_file_location('opt', Path(R) / 'bench/vcc-opt-rules.py')
opt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(opt)
POSITIVE = ['bs32', 'bs64', 'bs_negative', 'bs_min', 'bs_umax', 'bs_u32',
            'bs_ranges', 'bs_reverse', 'bs_first', 'bs_no_default', 'bs_selector', 'bs_fallthrough']
NEGATIVE = ['bs_few', 'bs_nonstacked', 'bs_dense', 'bs_wide', 'bs_shared_default']


def check(text, arch, enabled):
    wasm = arch.startswith('wasm')
    shift = r'\bi(?:32|64)\.shr_u\b' if wasm else r'\b(?:lsr|shrq?|shrl)\s'
    guard = r'\bi(?:32|64)\.gt_u\b' if wasm else r'\bb\.hi\b' if arch == 'aarch64' else r'\bja\s'
    for name in POSITIVE + NEGATIVE:
        body = opt.body(text, name, wasm)
        shifts = list(re.finditer(shift, body))
        expected = enabled and name in POSITIVE
        if bool(shifts) != expected:
            raise RuntimeError(f'{name}: expected bitset={expected}\n{body}')
        if expected:
            if len(shifts) != 1 or not re.search(guard, body[:shifts[0].start()]):
                raise RuntimeError(f'{name}: missing unsigned guard before single shift\n{body}')
            if wasm and 'br_table' in body:
                raise RuntimeError(f'{name}: table retained beside membership test\n{body}')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--vpp', default=str(Path(BUILD) / 'vpp'))
    ap.add_argument('--target', default='x86_64-linux-gnu', choices=[
        'x86_64-linux-gnu', 'aarch64-linux-gnu', 'wasm32-unknown-emscripten', 'wasm64-unknown-emscripten'])
    ap.add_argument('--run', action='store_true')
    ap.add_argument('--clang', default=llvm('clang'))
    ap.add_argument('--vlink', default=str(Path(BUILD) / 'vlink'))
    ap.add_argument('--node', default='node')
    ap.add_argument('--qemu', default='qemu-aarch64')
    ap.add_argument('--qemu-x86')
    args = ap.parse_args()
    args.rule, args.tail_obj, args.node_flags = 'all', False, []
    arch = args.target.split('-')[0]
    if args.run and arch == 'wasm64' and '--experimental-wasm-memory64' in opt.run([args.node, '--v8-options']):
        args.node_flags.append('--experimental-wasm-memory64')
    source = Path(R) / 'src/tests/vcc/switch_bitsets.c'
    with tempfile.TemporaryDirectory(prefix='vcc-switch-bitsets-') as temp:
        directory = Path(temp)
        for name, level, disabled in [('O0', 0, False), ('O1', 1, False), ('O2', 2, False),
                                      ('O3', 3, False), ('noopt', 2, True)]:
            assembly = directory / (name+'.s')
            command = [args.vpp, '--target='+args.target, '-O'+str(level), '-ffreestanding',
                       '-fno-stack-protector', '-S', str(source), '-o', str(assembly)]
            # The shared runner also compiles wasm objects; keep the explicit
            # disable in its environment as well as the assembly-producing job.
            previous = os.environ.get('VCC_NOOPT')
            try:
                if disabled:
                    os.environ['VCC_NOOPT'] = '1'
                opt.run(command)
                check(assembly.read_text(), arch, level > 0 and not disabled)
                if args.run:
                    opt.execute(args, directory, assembly, arch, source, level)
            finally:
                if previous is None:
                    os.environ.pop('VCC_NOOPT', None)
                else:
                    os.environ['VCC_NOOPT'] = previous
            print(f'{args.target} bitsets {name}: guarded codegen'+(' and execution' if args.run else '')+' PASS')


if __name__ == '__main__':
    main()
