#!/usr/bin/env python3
"""Compare O0/VCC_NOOPT and overflow-builtin controls against a frozen compiler."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

from hostenv import BUILD, R


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--vpp', default=str(Path(BUILD) / 'vpp'))
    parser.add_argument('--baseline', required=True)
    parser.add_argument('--source', default='multiply_constants.c',
                        help='C fixture under tests/vcc for disabled optimization parity')
    parser.add_argument('--factors', default='3,5,9', help='overflow control factors')
    args = parser.parse_args()
    total = 0
    with tempfile.TemporaryDirectory(prefix='vcc-mulconst-parity-') as tmp:
        directory = Path(tmp)
        overflow = directory / 'overflow.c'
        functions = []
        for tag, ty in [('s32', 'int'), ('u32', 'unsigned'),
                        ('s64', 'long long'), ('u64', 'unsigned long long')]:
            for factor in map(int, args.factors.split(',')):
                functions.append(f'int ov_{tag}_{factor}({ty} x,{ty} *p)'
                                 f'{{return __builtin_mul_overflow(x,({ty}){factor},p);}}')
        overflow.write_text('\n'.join(functions) + '\n')
        for target in ['x86_64-linux-gnu', 'aarch64-linux-gnu',
                       'wasm32-unknown-emscripten', 'wasm64-unknown-emscripten']:
            for level in range(4):
                for source in [Path(R) / 'ccs/vcc/tests/vcc' / args.source, overflow]:
                    # Real noopt policy is the VCC_NOOPT environment, not an
                    # optnone attribute (which this compiler currently ignores).
                    for noopt in ([False, True] if source != overflow else [False]):
                        texts = []
                        env = os.environ.copy()
                        env.pop('VCC_NOOPT', None)
                        if noopt:
                            env['VCC_NOOPT'] = '1'
                        for label, binary in [('base', args.baseline), ('new', args.vpp)]:
                            dest = directory / (label + '.s')
                            command = [binary, '--target=' + target, '-O' + str(level),
                                       '-ffreestanding', '-fno-stack-protector', '-S', str(source), '-o', str(dest)]
                            subprocess.run(command, check=True, capture_output=True, timeout=30, env=env)
                            texts.append(dest.read_text())
                        if source == overflow or level == 0 or noopt:
                            if texts[0] != texts[1]:
                                raise RuntimeError(f'{target} O{level} noopt={noopt} {source.name}: changed assembly')
                            total += 1

    print(f'multiply O0/VCC_NOOPT/overflow parity: {total} comparisons PASS')


if __name__ == '__main__':
    main()
