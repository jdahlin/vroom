#!/usr/bin/env python3
"""Check x86 source rows after unreachable/fallthrough record compaction."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

from hostenv import BUILD, llvm


SOURCE = """int debug_flow(int x) {
    goto live;
#line 100
    x += 11111;
#line 200
live:
    x += 22222;
    goto done;
#line 300
done:
    return x + 33333;
}
int debug_after(int x) {
    goto next;
#line 500
next:
    return x + 44444;
}
long debug_copies(long *p, long x) {
#line 700
    p[0] = x;
#line 710
    p[1] = x + 55555;
    p[2] = x + 3;
    return p[0];
}
"""


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--vpp', default=str(Path(os.environ.get('VCC_BIN', BUILD)) / 'vpp'))
    args = ap.parse_args()
    expected = {22222: 201, 33333: 301, 44444: 501, 55555: 710}
    with tempfile.TemporaryDirectory(prefix='vcc-debug-flow-') as td:
        src = Path(td) / 'flow.c'
        src.write_text(SOURCE)
        for level in range(4):
            asm = Path(td) / 'flow.s'
            obj = Path(td) / 'flow.o'
            subprocess.run([args.vpp, '--target=x86_64-linux-gnu', f'-O{level}', '-g', '-S',
                            str(src), '-o', str(asm)], check=True)
            active, seen = None, set()
            for line in asm.read_text().splitlines():
                row = re.match(r'\s*\.loc\s+\d+\s+(\d+)\b', line)
                if row:
                    active = int(row[1])
                for value, want in expected.items():
                    if re.search(r'\$' + str(value) + r'\b', line):
                        if active != want:
                            raise AssertionError(f'-O{level}: {value} has source line {active}, expected {want}')
                        seen.add(value)
            if seen != set(expected):
                raise AssertionError(f'-O{level}: missing code markers {set(expected) - seen}')
            subprocess.run([llvm('clang'), '--target=x86_64-linux-gnu', '-c', str(asm), '-o', str(obj)], check=True)
            subprocess.run([llvm('llvm-dwarfdump'), '--verify', str(obj)], check=True,
                           stdout=subprocess.DEVNULL)
            plain = Path(td) / 'plain.s'
            plain_obj = Path(td) / 'plain.o'
            subprocess.run([args.vpp, '--target=x86_64-linux-gnu', f'-O{level}', '-S',
                            str(src), '-o', str(plain)], check=True)
            subprocess.run([llvm('clang'), '--target=x86_64-linux-gnu', '-c', str(plain),
                            '-o', str(plain_obj)], check=True)
            for stem, input_obj in [('debug', obj), ('plain', plain_obj)]:
                subprocess.run([llvm('llvm-objcopy'), '-O', 'binary', '--only-section=.text',
                                str(input_obj), str(Path(td) / (stem + '.text'))], check=True)
            if (Path(td) / 'debug.text').read_bytes() != (Path(td) / 'plain.text').read_bytes():
                raise AssertionError(f'-O{level}: -g changes .text')
    print('x86 debug rows after control-flow cleanup: O0-O3 passed')


if __name__ == '__main__':
    main()
