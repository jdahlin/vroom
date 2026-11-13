#!/usr/bin/env python3
"""Bounded constant-control codegen/effect contracts for all three outputs."""
import argparse
import importlib.util
from pathlib import Path
import re
import sys
import tempfile
from hostenv import BUILD, R, llvm
spec = importlib.util.spec_from_file_location('opt_rules', Path(__file__).with_name('vcc-opt-rules.py'))
opt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(opt)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--vpp', default=str(Path(BUILD)/'vpp'))
    ap.add_argument('--vlink', default=str(Path(BUILD)/'vlink'))
    ap.add_argument('--target', default='wasm64-unknown-emscripten')
    ap.add_argument('--clang', default=llvm('clang'))
    ap.add_argument('--node', default='node')
    ap.add_argument('--qemu', default='qemu-aarch64')
    ap.add_argument('--qemu-x86')
    ap.add_argument('--compile-only', action='store_true')
    args = ap.parse_args()
    args.rule = 'constant-control'
    args.node_flags = []
    arch = args.target.split('-')[0]
    if arch == 'wasm64' and '--experimental-wasm-memory64' in opt.run([args.node, '--v8-options']):
        args.node_flags = ['--experimental-wasm-memory64']
    source = Path(R)/'src/tests/vcc/constant_control.c'
    with tempfile.TemporaryDirectory(prefix='vcc-constant-control-') as tmp:
        directory = Path(tmp)
        for level in range(4):
            assembly = directory/f'O{level}.s'
            opt.run([args.vpp, '--target='+args.target, f'-O{level}', '-ffreestanding',
                     '-fno-stack-protector', '-S', source, '-o', assembly])
            text = assembly.read_text()
            if level and re.search(r'\b(?:callq?|bl)\s+never_control\b', text):
                raise RuntimeError(f'O{level}: retained unreachable never_control')
            for name, symbol in [('control_volatile','observed'), ('control_atomic','atom')]:
                function = opt.body(text, name, arch.startswith('wasm'))
                if arch.startswith('wasm'):
                    memory_read = (re.search(r'\bi(?:32|64).const\s+'+symbol+r'\b',function) and
                                   re.search(r'\bi32.load\b',function))
                elif arch == 'x86_64':
                    direct = re.search(r'\bmovl\s+'+symbol+r'\(%rip\)',function)
                    addr = re.search(r'\bleaq\s+'+symbol+r'\(%rip\),\s*%(\w+)',function)
                    indirect = addr and re.search(r'\bmovl\s+\(%'+addr[1]+r'\)',function)
                    memory_read = direct or indirect
                else:
                    address = re.search(r'\badrp\s+x(\d+),\s*'+symbol+r'\b',function)
                    direct = re.search(r'\bldr\s+w\d+,\s*\[x\d+,\s*:lo12:'+symbol+r'\]',function)
                    memory_read = bool(address and direct)
                    if address and re.search(r'\badd\s+x'+address[1]+r',\s*x'+address[1]+r',\s*:lo12:'+symbol+r'\b',function):
                        register = int(address[1])
                        memory_read = memory_read or any(
                            int(word,16) & 0xfffffc00 == 0x88dffc00 and
                            (int(word,16) >> 5) & 31 == register
                            for word in re.findall(r'\.inst\s+0x([0-9a-f]+)',function))
                if not memory_read:
                    raise RuntimeError(f'{name}: lost {symbol} memory read\n{function}')
            named = opt.body(text, 'control_named_call', arch.startswith('wasm'))
            if not re.search(r'\b(?:callq?|bl)\s+strcmp\b', named):
                raise RuntimeError('folded an ordinary user-defined strcmp call')
            if arch.startswith('wasm'):
                if re.search(r'\bcall\s+step\b', opt.body(text, 'control_not_evaluated', True)):
                    raise RuntimeError('evaluated a dead/unevaluated operand')
            if not args.compile_only:
                opt.execute(args, directory, assembly, arch, source, level)
            print(f'{args.target} O{level}: constant-control OK')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, RuntimeError) as e:
        print(e, file=sys.stderr)
        sys.exit(1)
