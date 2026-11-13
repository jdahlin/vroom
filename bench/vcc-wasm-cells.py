#!/usr/bin/env python3
"""Typed wasm frame-cell forwarding/DSE: code shape and literal answers, no SDK."""
import argparse
import importlib.util
from pathlib import Path
import re
import sys
import tempfile

from hostenv import BUILD, R

spec = importlib.util.spec_from_file_location('opt_rules', Path(__file__).with_name('vcc-opt-rules.py'))
opt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(opt)


def check(text):
    for name, op in [('wc_forward', 'i32.load'), ('wc_overwrite', 'i32.load'),
                     ('wc_wide', 'i64.load'), ('wc_float', 'f64.load'), ('wc_float32', 'f32.load')]:
        function = opt.body(text, name, True)
        if re.search(r'\b' + re.escape(op) + r'\b', function):
            raise RuntimeError(f'{name}: exact-cell load retained\n{function}')
    for name, op, count in [('wc_overwrite', 'i32.store', 1),
                            ('wc_load', 'i32.load', 1),
                            ('wc_volatile', 'i32.store', 3),
                            ('wc_volatile', 'i32.load', 1),
                            ('wc_atomic', 'i32.store', 2),
                            ('wc_atomic', 'i32.load', 1)]:
        function = opt.body(text, name, True)
        actual = len(re.findall(r'\b' + re.escape(op) + r'\b', function))
        if actual != count:
            raise RuntimeError(f'{name}: expected {count} {op}, found {actual}\n{function}')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--vpp', default=str(Path(BUILD) / 'vpp'))
    ap.add_argument('--vlink', default=str(Path(BUILD) / 'vlink'))
    ap.add_argument('--node', default='node')
    ap.add_argument('--target', choices=['wasm32', 'wasm64', 'all'], default='all')
    ap.add_argument('--compile-only', action='store_true')
    args = ap.parse_args()
    args.node_flags = []
    if '--experimental-wasm-memory64' in opt.run([args.node, '--v8-options']):
        args.node_flags = ['--experimental-wasm-memory64']
    for arch in (['wasm32', 'wasm64'] if args.target == 'all' else [args.target]):
        args.target = arch + '-unknown-emscripten'
        with tempfile.TemporaryDirectory(prefix='vcc-wasm-cells-') as tmp:
            directory = Path(tmp)
            for filename in ['wasm_frame_cells.c', 'stack_cells.c']:
                source = Path(R) / 'src/test/vcc' / filename
                for level in range(4):
                    assembly = directory / f'{filename}-O{level}.s'
                    opt.run([args.vpp, f'--target={args.target}', f'-O{level}', '-ffreestanding',
                             '-fno-stack-protector', '-S', source, '-o', assembly])
                    if filename == 'wasm_frame_cells.c' and level:
                        check(assembly.read_text())
                    if not args.compile_only:
                        opt.execute(args, directory, assembly, arch, source, level)
                    print(f'{arch} {filename} O{level}: OK')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, RuntimeError) as e:
        print(e, file=sys.stderr)
        sys.exit(1)
