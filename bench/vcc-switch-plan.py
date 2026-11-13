#!/usr/bin/env python3
"""Check shared switch normalization, sorting and bounded table selection."""
import argparse
from pathlib import Path
import subprocess
import tempfile

from hostenv import R, llvm

SOURCE = r'''
#include <assert.h>
#include "codegen/opt/switch.c"

/* Independently walk the public sorted intervals for representative values.
 * Targets retain source identity even when the normalization changes order. */
static void exercise(unsigned bits, bool sg)
{
    em_switch_case cases[1024];
    for (unsigned i = 0; i < 1024; i++) {
        unsigned j = (i * 621) & 1023;
        i128 lo = (i128)j * 4096 - 2097152;
        if (!sg) lo += (i128)1 << (bits - 1);
        cases[i] = (em_switch_case){lo, lo + 3, j + 1};
    }
    assert(em_switch_prepare(cases, 1024, bits, sg));
    for (unsigned i = 0; i < 1024; i++) {
        assert(cases[i].target == i + 1);
        assert(cases[i].hi - cases[i].lo == 3);
        if (i) assert(cases[i-1].hi < cases[i].lo);
    }
}
int main(void)
{
    assert(em_switch_strategy(0, 0, 0, false, true) == EM_SWITCH_LINEAR);
    assert(em_switch_strategy(7, 0, 10000, false, false) == EM_SWITCH_LINEAR);
    assert(em_switch_strategy(8, 0, 10000, false, false) == EM_SWITCH_TREE);
    assert(em_switch_strategy(4, -2, 2, false, true) == EM_SWITCH_TABLE);
    assert(em_switch_strategy(8, -2, 2, true, true) == EM_SWITCH_TREE);
    assert(em_switch_strategy(8, 0, UINT64_MAX, false, true) == EM_SWITCH_TREE);
    assert(em_switch_strategy(8, -((i128)1<<63), ((i128)1<<63)-1, false, true) == EM_SWITCH_TREE);
    assert(em_switch_strategy(UINT32_MAX, 0, UINT64_MAX, false, true) == EM_SWITCH_TREE);
    assert(em_switch_strategy(20000, 0, 65535, false, true) == EM_SWITCH_TABLE);
    assert(em_switch_strategy(20000, 0, 65536, false, true) == EM_SWITCH_TREE);
    for (unsigned bits = 32; bits <= 64; bits += 32)
        for (unsigned sg = 0; sg < 2; sg++) exercise(bits, sg);
    em_switch_case a[] = {{-1, -1, 71}, {0, 0, 72}, {(i128)1<<63,(i128)1<<63,73}};
    assert(em_switch_prepare(a, 3, 64, false));
    assert(a[0].target == 72 && a[1].target == 73 && a[2].target == 71);
    assert(a[2].lo == (i128)UINT64_MAX);
    em_switch_case b[] = {{2147483648LL,2147483648LL,4}, {-1,-1,5}, {0,0,6}};
    assert(em_switch_prepare(b, 3, 32, true));
    assert(b[0].lo == -2147483648LL && b[0].target == 4);
    em_switch_case wrap[] = {{0x7fffffff,0x80000000,1}};
    assert(!em_switch_prepare(wrap, 1, 32, true));
    em_switch_case overlap[] = {{1,4,1},{3,7,2}};
    assert(!em_switch_prepare(overlap, 2, 64, false));
    em_switch_case duplicate[] = {{-1,-1,1},{UINT32_MAX,UINT32_MAX,2}};
    assert(!em_switch_prepare(duplicate, 2, 32, false));
    return 0;
}
'''



def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=llvm('clang'))
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='vcc-switch-plan-') as tmp:
        source = Path(tmp) / 'effects.c'
        binary = Path(tmp) / 'effects'
        source.write_text(SOURCE)
        flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if args.sanitize else []
        subprocess.run([args.cc, '-std=gnu11', '-O1', '-g', '-I', str(Path(R) / 'src'), '-I', str(Path(R) / 'src' / 'include'),
                        *flags, str(source), '-o', str(binary)], check=True, timeout=30)
        subprocess.run([str(binary)], check=True, timeout=10)
    print('shared switch planning contracts: PASS')


if __name__ == '__main__':
    main()
