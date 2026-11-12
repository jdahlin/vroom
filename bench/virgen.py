#!/usr/bin/env python3
"""Generate deterministic VIR integer, memory and control-flow programs.

Generated programs avoid immediate undefined behavior for any integer inputs.
"""
import argparse
import random


def generate(seed, size):
    r = random.Random(seed)
    out = ['vir 1', 'target x86_64-linux', 'sig %s = (int, int) -> int',
           'func @main %s {', '  slot $0 size 8 align 8', 'b0:',
           '  v0:i64 = param 0.0', '  v1:i64 = param 1.0', '  store.8 $0, v0']
    values = [0, 1]
    next_value = 2
    next_block = 1
    binary = ('add', 'sub', 'mul', 'and', 'or', 'xor', 'umulhi', 'smulhi')
    unary = ('neg', 'not', 'clz', 'ctz', 'popcnt', 'bswap')
    for _ in range(size):
        op = r.choice(binary + unary + ('shl', 'lshr', 'ashr', 'select',
                                       'udiv', 'urem', 'load', 'store', 'diamond'))
        a = r.choice(values)
        if op == 'store':
            out.append(f'  store.8 $0, v{a}')
            continue
        if op == 'diamond':
            b = next_block
            next_block += 3
            out.extend([f'  brif v{a}, b{b}, b{b+1}',
                        f'b{b}:', f'  br b{b+2}(v{r.choice(values)})',
                        f'b{b+1}:', f'  br b{b+2}(v{r.choice(values)})',
                        f'b{b+2}(v{next_value}:i64):'])
            values.append(next_value)
            next_value += 1
            continue
        if op in binary:
            operand = f'v{r.choice(values)}' if r.randrange(2) else str(r.randrange(-128, 128))
            expr = f'{op} v{a}, {operand}'
        elif op == 'load':
            expr = 'load.8 $0'
        elif op in ('udiv', 'urem'):
            expr = f'{op} v{a}, {r.randrange(1, 129)}'
        elif op in unary:
            expr = f'{op} v{a}'
        elif op == 'select':
            expr = f'select v{r.choice(values)}, v{a}, v{r.choice(values)}'
        else:
            expr = f'{op} v{a}, {r.randrange(64)}'
        out.append(f'  v{next_value}:i64 = {expr}')
        values.append(next_value)
        next_value += 1
    out.extend([f'  ret v{r.choice(values)}', '}'])
    return '\n'.join(out) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seed', type=int, required=True)
    parser.add_argument('--size', type=int, default=50)
    args = parser.parse_args()
    if not 0 <= args.size <= 4096:
        parser.error('--size must be between 0 and 4096')
    print(generate(args.seed, args.size), end='')


if __name__ == '__main__':
    main()
