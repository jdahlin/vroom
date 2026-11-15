#!/usr/bin/env python3
"""Compare ordered pairs of TUs against Clang and uncached vcc (no kernel tree)."""
import os
from pathlib import Path
import subprocess
import tempfile

from hostenv import BUILD, llvm

VPP = Path(os.environ.get('VCC_BIN', BUILD)).resolve() / 'vpp'
FLAGS = ['--target=aarch64-linux-gnu', '-std=c11', '-nostdinc',
         '-Werror=macro-redefined', '-fsyntax-only']
INCLUDE = '#include "both.h"\n'
CHILDREN = '#include "write.h"\n#include "read.h"\n'


def check_value(prefix, expected):
    return prefix + INCLUDE + f'_Static_assert(selected == {expected}, "wrong selected value");\n'


enum = 'enum { FLAG0 = 7, FLAG1 = 8 };\n'
cases = {
    'counter-paste': (
        {'both.h': CHILDREN, 'write.h': '#define FLAG1 99\n',
         'read.h': '#define P_(a,b) a##b\n#define P(a,b) P_(a,b)\n'
                   'enum { selected = P(FLAG,__COUNTER__) };\n'},
        [check_value(enum, 7), check_value(enum + 'enum { advance = __COUNTER__ };\n', 99)]),
    'transient-counter-binding': (
        {'both.h': '#define FLAG1 99\n#include "read.h"\n#undef FLAG1\n',
         'read.h': '#define P_(a,b) a##b\n#define P(a,b) P_(a,b)\n'
                   'enum { selected = P(FLAG,__COUNTER__) };\n'},
        [check_value(enum + advance, expected) + '#ifdef FLAG1\n#error leaked FLAG1\n#endif\n'
         for advance, expected in (('', 7), ('enum { advance = __COUNTER__ };\n', 99))]),
    'redefinition': (
        {'both.h': CHILDREN, 'write.h': '#define FLAG 9\n',
         'read.h': 'enum { selected = FLAG };\n'},
        [check_value('', 9), check_value('#define FLAG 2\n', 9)]),
    'conditional-write': (
        {'both.h': CHILDREN, 'write.h': '#ifdef DEBUG\n#undef FLAG\n#define FLAG 9\n#endif\n',
         'read.h': 'enum { selected = FLAG };\n'},
        [check_value('#define FLAG 2\n', 2), check_value('#define FLAG 3\n#define DEBUG\n', 9)]),
    'defined-then-expanded': (
        {'both.h': '#ifdef FLAG\n#include "read.h"\n#endif\n',
         'read.h': 'enum { selected = FLAG };\n'},
        [check_value('#define FLAG 2\n', 2), check_value('#define FLAG 3\n', 3)]),
    'once-before-parent': (
        {'both.h': '#include "once.h"\nenum { selected = FLAG };\n',
         'once.h': '#pragma once\n#undef FLAG\n#define FLAG 9\n'},
        [check_value('', 9), check_value('#include "once.h"\n#undef FLAG\n#define FLAG 2\n', 2)]),
    'push-pop-across-child': (
        {'both.h': '#pragma push_macro("FLAG")\n#include "write.h"\n#pragma pop_macro("FLAG")\n',
         'write.h': '#undef FLAG\n#define FLAG 9\nenum { inside = FLAG };\n'},
        [f'#define FLAG {n}\n' + INCLUDE + f'_Static_assert(inside == 9 && FLAG == {n}, "restore");\n'
         for n in (2, 3)]),
    'child-final-write': (
        {'both.h': CHILDREN + '#undef FLAG\n#define FLAG 17\n',
         'write.h': '#if MODE\n#define FLAG 3\n#else\n#define FLAG 4\n#endif\n',
         'read.h': 'enum { selected = FLAG };\n#undef FLAG\n'},
        [check_value(f'#define MODE {mode}\n', value) + '_Static_assert(FLAG == 17, "final write");\n'
         for mode, value in ((1, 3), (0, 4))]),
    'macro-include': (
        {'both.h': '#include WHICH\n#include "read.h"\n',
         'x.h': '#define FLAG 2\n', 'y.h': '#define FLAG 3\n',
         'read.h': 'enum { selected = FLAG };\n'},
        [check_value('#define WHICH "x.h"\n', 2), check_value('#define WHICH "y.h"\n', 3)]),
}


def run_case(directory, name, headers, mains):
    for file, body in headers.items():
        (directory / file).write_text(body)
    sources = []
    expected = []
    for i, body in enumerate(mains):
        source = directory / f'{i}.c'
        source.write_text(body)
        sources.append(str(source))
        ref = subprocess.run([llvm('clang'), *FLAGS, source], capture_output=True, text=True)
        conflict = name == 'redefinition' and i == 1
        if ref.returncode != int(conflict) or (conflict and 'macro redefined' not in ref.stderr):
            raise RuntimeError(f'{name}: unexpected Clang result {ref.returncode}\n{ref.stderr}')
        expected.append(ref.returncode)

    for order in ((0, 1), (1, 0)):
        jobs = directory / 'jobs'
        jobs.write_text(''.join('\x1f'.join(['-', *FLAGS, sources[i]]) + '\n' for i in order))
        for cached in (False, True):
            env = dict(os.environ, VPP_BATCH_ERR='1', VCC_NODC='1')
            for key in ('VCC_DR', 'VCC_DRREC', 'VPP_WARM'):
                env.pop(key, None)
            env.pop('VPP_NOHC', None)
            if not cached:
                env['VPP_NOHC'] = '1'
            stats = directory / 'stats'
            stats.unlink(missing_ok=True)
            result = subprocess.run([VPP, '--batch', jobs, '-j', '1', '--stats', stats],
                                    env=env, capture_output=True, text=True)
            actual = [int(row.split('\t')[0]) for row in stats.read_text().splitlines()]
            wanted = [expected[i] for i in order]
            if result.returncode != int(any(actual)):
                raise RuntimeError(result.stdout + result.stderr)
            if actual != wanted or (name == 'redefinition' and 1 in wanted
                                    and 'macro redefined' not in result.stderr):
                raise RuntimeError(f'{name} {order} cached={cached}: {actual} vs {wanted}\n'
                                   + result.stderr)


def check_cache_warning(directory):
    warning = 'vcc-uncacheable-header'
    source = directory / 'main.c'
    source.write_text(INCLUDE)
    (directory / 'both.h').write_text('#include "read.h"\n')
    leaf = directory / 'read.h'
    for body, reason in (
        ('#define P_(a,b) a##b\n#define P(a,b) P_(a,b)\nint P(x,__COUNTER__);\n',
         'macro name depends on __COUNTER__'),
        ('#pragma push_macro("X")\n#pragma pop_macro("X")\n', 'macro stack operation'),
        ('enum { x = __COUNTER__ };\n', None),
    ):
        leaf.write_text(body)
        for flags, severity in (([], None), ([f'-W{warning}'], 'warning'),
                                ([f'-Werror={warning}'], 'error'),
                                ([f'-W{warning}', f'-Wno-{warning}'], None)):
            result = subprocess.run([VPP, *FLAGS, '-Werror=unknown-warning-option', *flags, source],
                                    capture_output=True, text=True)
            expected = bool(reason and severity)
            if result.returncode != int(expected and severity == 'error'):
                raise RuntimeError(result.stderr)
            messages = [line for line in result.stderr.splitlines() if 'header cannot be cached' in line]
            if len(messages) != int(expected) or (expected and
                    (str(leaf) not in messages[0] or f'{severity}: header cannot be cached: {reason}'
                     not in messages[0])):
                raise RuntimeError(f'{flags}: unexpected cache diagnostics\n{result.stderr}')


with tempfile.TemporaryDirectory(prefix='vcc-header-deps-') as tmp:
    for name, (headers, mains) in cases.items():
        directory = Path(tmp) / name
        directory.mkdir()
        run_case(directory, name, headers, mains)
    check_cache_warning(Path(tmp))
print(f'{len(cases)} header families, both orders, cache on/off')
