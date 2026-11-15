#!/usr/bin/env python3
"""Differential fuzzing of header replay. Repeat one family with --seed N --cases 1."""
import argparse
from itertools import permutations
import os
from pathlib import Path
import random
import re
import shutil
import subprocess
import tempfile

from hostenv import BUILD, llvm

FLAGS = ['--target=aarch64-linux-gnu', '-std=gnu11', '-nostdinc', '-E', '-P']
PRELUDE = '#define JOIN_(a,b) a##b\n#define JOIN(a,b) JOIN_(a,b)\n'
NAMES = ('X', 'Y', 'Z', 'K0', 'K1', 'K2', 'K9', 'K10')
OBSERVE = 'observe X Y Z K0 K1 K2 K9 K10 JOIN(K,__COUNTER__) __INCLUDE_LEVEL__\n'


def generate(directory, seed):
    rng = random.Random(seed)
    count = rng.randint(3, 6)

    def change():
        name = rng.choice(NAMES)
        # Also exercise lazy aliases: their value depends on later writes.
        value = rng.choice(['0', '1', '37', rng.choice(NAMES)])
        return f'#undef {name}\n' + (f'#define {name} {value}\n' if rng.randrange(4) else '')

    def observe():
        if seed % 2 == 0:
            return OBSERVE
        # Selective reads keep an earlier all-names probe from accidentally
        # recording a dependency that only a later child should discover.
        expr = rng.choice([rng.choice(NAMES), 'STR(JOIN(K,__COUNTER__))',
                           'JOIN(JOIN(K,__COUNTER__),__COUNTER__)',
                           'PICK(X, JOIN(K,__COUNTER__))', '__INCLUDE_LEVEL__'])
        return f'observe {expr}\n'

    def block():
        name = rng.choice(NAMES)
        condition = rng.choice([f'defined({name})', name, f'MODE == {rng.randrange(3)}'])
        return f'#if {condition}\n' + change() + observe() + '#else\n' + change() + '#endif\n'

    def include(i):
        return f'#include "h{i}.h"\n'

    for i in range(count):
        body = change() + block()
        if i:
            body += include(i - 1) + change()
            other = include(rng.randrange(i))
            body += rng.choice([other, '#if MODE\n' + other + '#endif\n'])
        body += observe() + change()
        guard = rng.randrange(4)
        if guard == 1:
            body = f'#ifndef GUARD{i}\n#define GUARD{i}\n' + body + '#endif\n'
        elif guard == 2:
            body = '#pragma once\n' + body
        (directory / f'h{i}.h').write_text(body)

    for i in range(3):
        body = PRELUDE
        if seed % 2:
            body += '#define STR_(x) #x\n#define STR(x) STR_(x)\n#define PICK(a,b) b a b\n'
        body += f'#define MODE {i}\n' + change()
        body += 'advance __COUNTER__\n' * rng.choice([0, 1, 2, 9, 10])
        if i:
            body += include(rng.randrange(count - 1))
        body += include(count - 1) + OBSERVE + block() + include(count - 1) + OBSERVE
        (directory / f't{i}.c').write_text(body)


def run_case(directory, seed, vpp, clang):
    generate(directory, seed)
    for i in range(3):
        subprocess.run([clang, *FLAGS, f't{i}.c', '-o', f't{i}.i'], cwd=directory,
                       check=True, timeout=10)

    env = dict(os.environ, VPP_BATCH_ERR='1', VPP_CMP_NOCACHE='1')
    for key in ('VPP_NOHC', 'VPP_WARM'):
        env.pop(key, None)
    # Start a fresh process for every permutation, then revisit each TU with
    # all three histories in the cache. One worker fixes the execution order.
    hits = 0
    for index, prefix in enumerate(permutations(range(3))):
        order = prefix + prefix[::-1]
        manifest = ''.join('\x1f'.join([f't{i}.i', *FLAGS, f't{i}.c']) + '\n' for i in order)
        jobs = f'order{index}.jobs'
        (directory / jobs).write_text(manifest)
        for cached in (False, True):
            result = subprocess.run([vpp, '--batch', jobs, '--cmp', '-j', '1'], cwd=directory,
                                    env=env if cached else dict(env, VPP_NOHC='1'),
                                    capture_output=True, text=True, timeout=15)
            log = result.stdout + result.stderr
            if result.returncode or f'{len(order)}/{len(order)} ok;' not in log:
                mode = 'warm' if cached else 'uncached'
                (directory / 'failure.log').write_text(log)
                raise RuntimeError(f'{mode} vcc differs from Clang, seed {seed}, order {order}\n{log}')
            match = re.search(r'header cache: (\d+) hits', log)
            if not match or (not cached and int(match[1])):
                raise RuntimeError(f'unexpected cache statistics\n{log}')
            hits += int(match[1])
    return hits


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seed', type=int, default=0, help='first independent case seed')
    parser.add_argument('--cases', type=int, default=64)
    parser.add_argument('--vpp', type=Path, default=Path(os.environ.get('VCC_BIN', BUILD)) / 'vpp')
    parser.add_argument('--clang', default=llvm('clang'))
    parser.add_argument('--fail-dir', type=Path, default=Path(BUILD) / 'header-fuzz')
    args = parser.parse_args()
    if args.cases < 1:
        parser.error('--cases must be positive')
    clang = shutil.which(args.clang)
    if not clang and args.clang == llvm('clang'):
        clang = shutil.which('clang-23')
    if not clang:
        parser.error('reference Clang unavailable; set LLVM or pass --clang')
    vpp = args.vpp.resolve()
    if not os.access(vpp, os.X_OK):
        parser.error(f'vpp unavailable: {vpp}')
    hits = 0
    for seed in range(args.seed, args.seed + args.cases):
        with tempfile.TemporaryDirectory(prefix='vcc-header-') as tmp:
            directory = Path(tmp)
            try:
                hits += run_case(directory, seed, vpp, Path(clang).resolve())
            except (RuntimeError, subprocess.SubprocessError) as error:
                args.fail_dir.mkdir(parents=True, exist_ok=True)
                saved = Path(tempfile.mkdtemp(prefix=f'seed-{seed}-', dir=args.fail_dir))
                shutil.copytree(directory, saved, dirs_exist_ok=True)
                raise SystemExit(f'{error}\nReproducer: {saved.resolve()}') from error
    if not hits:
        raise SystemExit('no header replay observed')
    print(f'seeds {args.seed}..{args.seed + args.cases - 1}: {args.cases * 36} warm and '
          f'{args.cases * 36} uncached TU comparisons passed, {hits} warm header hits')


if __name__ == '__main__':
    main()
