#!/usr/bin/env python3
"""Shared single-operation multiplication recipes against a modular oracle."""
import argparse
from pathlib import Path
import subprocess
import tempfile

from hostenv import R, llvm

SOURCE = r'''
#include <assert.h>
#include "emit/opt/mulconst.c"
static uint64_t state = UINT64_C(0x948e612f6c010757);
static uint64_t next(void) { state ^= state << 13; state ^= state >> 7; state ^= state << 17; return state; }
int main(void)
{
    for (unsigned bits=32;bits<=64;bits+=32) {
        for (unsigned k=0;k<32;k++) {
            unsigned shift=em_mulconst(bits,k);
            assert(!!shift == (k==3 || k==5 || k==9));
            if (!shift) continue;
            for (unsigned n=0;n<4000;n++) {
                uint64_t x=next(), got=x+(x<<shift), expected=x*k;
                if (bits==32) got=(uint32_t)got, expected=(uint32_t)expected;
                assert(got==expected);
            }
        }
    }
    assert(!em_mulconst(0,3) && !em_mulconst(8,3) && !em_mulconst(128,3));
    assert(em_mulconst(32,((i128)1<<32)+3)==1);
    assert(em_mulconst(64,((i128)1<<64)+5)==2);
    assert(em_mulconst(32,-((i128)1<<32)+9)==3);
    assert(!em_mulconst(64,-1));
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=llvm('clang'))
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--check-mutation', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='vcc-rec-mulconst-') as tmp:
        source, binary = Path(tmp) / 'recipe.c', Path(tmp) / 'recipe'
        flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if args.sanitize else []
        def compile_run(text, mutation=False):
            source.write_text(text)
            subprocess.run([args.cc, '-std=gnu11', '-O1', '-g', '-I', str(Path(R) / 'src'),
                            *flags, str(source), '-o', str(binary)], check=True, timeout=30)
            result = subprocess.run([str(binary)], capture_output=True, cwd=tmp, timeout=10)
            if mutation:
                if result.returncode == 0 or b'Assertion' not in result.stderr:
                    raise RuntimeError('incorrect shifted-add recipe mutation survived')
            elif result.returncode:
                raise RuntimeError(result.stderr.decode())
        compile_run(SOURCE)
        if args.check_mutation:
            implementation = (Path(R) / 'src/emit/opt/mulconst.c').read_text()
            implementation = implementation.replace('#include "../emitimpl.h"',
                '#include "' + str(Path(R) / 'src/emit/emitimpl.h') + '"')
            changed = implementation.replace('case 3: return 1;', 'case 3: return 2;')
            assert changed != implementation
            compile_run(SOURCE.replace('#include "emit/opt/mulconst.c"', changed), True)
    print('shared constant-multiply recipe contracts: PASS')


if __name__ == '__main__':
    main()
