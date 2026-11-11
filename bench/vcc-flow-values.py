#!/usr/bin/env python3
"""Contracts for the bounded local expression identity table."""
import argparse
from pathlib import Path
import subprocess
import tempfile
from hostenv import R, llvm

SOURCE = r'''
#include <assert.h>
#include "vcc/emit/flow/values.c"
int main(void)
{
 em_value_table t = {0};
 em_value_key a = {1, 2, 3, 4, 5}, b = a;
 assert(!em_values_lookup(&t, a));
 em_values_reset(&t, 100);
 assert(!em_values_lookup(&t, a));
 assert(!em_values_insert(&t, a, 0));
 assert(em_values_insert(&t, a, 11));
 assert(em_values_lookup(&t, a) == 11);
 b.type++;
 assert(!em_values_lookup(&t, b));
 b = a; b.left++;
 assert(!em_values_lookup(&t, b));
 b = a; b.right++;
 assert(!em_values_lookup(&t, b));
 b = a; b.immediate++;
 assert(!em_values_lookup(&t, b));
 b = a; b.operation++;
 assert(!em_values_lookup(&t, b));
 /* Find a real collision; eviction must never return the wrong identity. */
 b = a;
 do { b.immediate++; } while (em_values_hash(a) != em_values_hash(b));
 assert(em_values_insert(&t, b, 12));
 assert(!em_values_lookup(&t, a));
 assert(em_values_lookup(&t, b) == 12);
 /* A block boundary discards all previous identities. */
 em_values_reset(&t, 2);
 assert(!em_values_lookup(&t, b));
 assert(em_values_insert(&t, a, 13));
 assert(!em_values_lookup(&t, a)); /* budget exhausted: no stale facts */
 assert(!em_values_insert(&t, a, 14));
 em_values_reset(&t, 1);
 assert(!em_values_lookup(&t, a));
 em_values_reset(&t, 0);
 assert(!em_values_insert(&t, a, 15));
 assert(!em_values_lookup(&t, a));
 return 0;
}
'''

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=llvm('clang'))
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='vcc-flow-values-') as temp:
        source, binary = Path(temp) / 'test.c', Path(temp) / 'test'
        source.write_text(SOURCE)
        flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if args.sanitize else []
        subprocess.run([args.cc, '-std=gnu11', '-O1', '-g', '-I', str(Path(R) / 'ccs'),
                        *flags, str(source), '-o', str(binary)], check=True, timeout=30)
        subprocess.run([str(binary)], check=True, timeout=10)
    print('bounded local value table contracts: PASS')

if __name__ == '__main__':
    main()
