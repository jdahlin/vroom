#!/usr/bin/env python3
"""
bench/vcc-x86intrinsics.py: every builtin of the generated table
(frontend/x86-instructions.inc) called on pseudo-random and extreme operands,
vpp's result against clang's, on an x86-64 host with the features.

One program per builtin, generated from its row's signature: the builtin is
called on each operand set (controls 0 to 3) and the results' bytes hashed.
A new row needs no new test code.

  bench/vcc-x86intrinsics.py [-j N] [-O 02] [--cxx] [--bin-dir DIR] [-v] [PAT]
"""
import argparse
import concurrent.futures
import os
import platform
import re
import subprocess
import sys
import tempfile

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FLAGS = ['-msse4.2', '-maes', '-mpclmul', '-msha']
ROW = re.compile(r'^X86_INSTRUCTION\("(\w+)", "(\w+)", "(\w+)", (\w+), '
                 r'"([\w.]+)"\)', re.M)
TYPES = {
    'B': 'v16c', 'U': 'v16u', 'H': 'v8s', 'G': 'v8u', 'I': 'v4i', 'Q': 'v2q',
    'F': 'v4f', 'E': 'v2d', 'c': 'char', 'C': 'unsigned char', 'N': 'short',
    'h': 'unsigned short', 'i': 'int', 'u': 'unsigned', 'w': 'long long',
    'W': 'unsigned long long', 'f': 'float', 'S': 'const char *',
    's': 'char *', 'P': 'const void *', 'p': 'void *',
}
PRELUDE = '''#include <stdio.h>
#include <string.h>
typedef char v16c __attribute__((vector_size(16)));
typedef unsigned char v16u __attribute__((vector_size(16)));
typedef short v8s __attribute__((vector_size(16)));
typedef unsigned short v8u __attribute__((vector_size(16)));
typedef int v4i __attribute__((vector_size(16)));
typedef long long v2q __attribute__((vector_size(16)));
typedef float v4f __attribute__((vector_size(16)));
typedef double v2d __attribute__((vector_size(16)));

static unsigned long long hash = 0xcbf29ce484222325ull;
static unsigned long long state = 1;
static unsigned char data[3][16] __attribute__((aligned(16)));

static void add(const void *p, int size)
{
    const unsigned char *b = (const unsigned char *)p;
    for (int i = 0; i < size; i++)
        hash = (hash ^ b[i]) * 0x100000001b3ull;
}

/* random bits; small values, equal operands, extremes and plain floats in
   turn */
static void fill(int round)
{
    for (int o = 0; o < 3; o++) {
        unsigned long long r[2];
        for (int k = 0; k < 2; k++) {
            state = state * 6364136223846793005ull + 1442695040888963407ull;
            r[k] = state ^ (state >> 29);
            if (round % 8 == 1)
                r[k] &= 0x0303030303030303ull;
            if (round % 8 == 2)
                r[k] = 0x8000800080008000ull;
            if (round % 8 == 3)
                r[k] = 0x7fff7fff80008000ull;
        }
        memcpy(data[o], r, 16);
        if (round % 8 == 4) {
            float f[4];
            for (int k = 0; k < 4; k++)
                f[k] = (float)((int)(r[k & 1] >> (8 * k)) % 1000) / 8;
            memcpy(data[o], f, 16);
        }
        if (round % 8 == 5) {
            double d[2] = {(double)(int)r[0] / 16, (double)(int)r[1] / 16};
            memcpy(data[o], d, 16);
        }
    }
    if (round % 8 == 6)
        memcpy(data[1], data[0], 16);
}
'''


def program(name, signature, form):
    controls = [None]
    if form.endswith('CONTROL'):
        controls = [0, 1, 2, 3]
    calls = []
    for control in controls:
        values = []
        declarations = []
        parameters = signature[1:]
        for i, code in enumerate(parameters):
            if control is not None and i == len(parameters) - 1:
                values.append(str(control))
                continue
            t = TYPES[code]
            if code in 'SsPp':
                values.append('(%s)data[%d]' % (t, i))
            else:
                declarations.append('%s a%d; memcpy(&a%d, data[%d], sizeof a%d);'
                                    % (t, i, i, i, i))
                if form == 'SHIFT' and i == 1:
                    declarations.append('a1 &= 31;')
                values.append('a%d' % i)
        result = TYPES[signature[0]]
        calls.append('    { %s\n      %s r = (%s)%s(%s);\n      '
                     'add(&r, sizeof r); }\n'
                     % (' '.join(declarations), result, result, name,
                        ', '.join(values)))
    return (PRELUDE + 'static __attribute__((noinline)) void one(void)\n{\n' +
            ''.join(calls) + '}\n\nint main(void)\n{\n'
            '    for (int round = 0; round < 400; round++) {\n'
            '        fill(round);\n        one();\n    }\n'
            '    printf("%016llx\\n", hash);\n    return 0;\n}\n')


def run(command):
    return subprocess.run(command, capture_output=True, text=True, timeout=120)


def build_and_run(compiler, source, directory, tag, level):
    obj = os.path.join(directory, tag + '.o')
    p = run(compiler + FLAGS + [level, '-c', source, '-o', obj])
    if p.returncode:
        lines = p.stderr.strip().splitlines() or ['status %d' % p.returncode]
        errors = [line for line in lines if 'error' in line] or lines
        return None, errors[0].split('error: ')[-1]
    exe = os.path.join(directory, tag)
    p = run(['clang', '-no-pie', obj, '-o', exe])
    if p.returncode:
        return None, 'link: ' + p.stderr.strip().splitlines()[0]
    p = run([exe])
    if p.returncode:
        return None, 'exit %d' % p.returncode
    return p.stdout.strip(), ''


def check(row, vpp, levels, directory, language):
    name, signature, _, form, _ = row
    short = name[len('__builtin_ia32_'):]
    source = os.path.join(directory, short + '.c')
    with open(source, 'w') as f:
        f.write(program(name, signature, form))
    want, error = build_and_run(['clang'], source, directory, short + '-c',
                                '-O1')
    if want is None:
        return short, 'harness: clang: ' + error
    for level in levels:
        got, error = build_and_run([vpp, '-x', language], source, directory,
                                   short + level, level)
        if got is None:
            return short, '%s: %s' % (level, error)
        if got != want:
            return short, '%s: result differs from clang\'s' % level
    return short, ''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-j', type=int, default=8)
    ap.add_argument('-O', dest='levels', default='02')
    ap.add_argument('--bin-dir', default=os.environ.get('VCC_BIN') or
                    os.path.join(R, 'build', 'linux', 'src'))
    ap.add_argument('--cxx', action='store_true',
                    help="vpp compiles the programs as C++")
    ap.add_argument('-v', action='store_true')
    ap.add_argument('pattern', nargs='?', default='')
    a = ap.parse_args()
    if platform.machine() != 'x86_64':
        print('vcc-x86intrinsics: skipped (not an x86-64 host)')
        return 0
    table = os.path.join(a.bin_dir, 'generated', 'include', 'frontend',
                         'x86-instructions.inc')
    with open(table) as f:
        rows = [r for r in ROW.findall(f.read()) if a.pattern in r[0]]
    with open('/proc/cpuinfo') as f:
        if 'sha_ni' not in f.read():
            rows = [r for r in rows if r[4] != 'sha']
    vpp = os.path.join(a.bin_dir, 'vpp')
    language = 'c'
    if a.cxx:
        language = 'c++'
    failed = {}
    with tempfile.TemporaryDirectory() as directory:
        with concurrent.futures.ThreadPoolExecutor(a.j) as pool:
            for name, error in pool.map(
                    lambda r: check(r, vpp, ['-O' + c for c in a.levels],
                                    directory, language), rows):
                if error:
                    failed.setdefault(error, []).append(name)
                elif a.v:
                    print('ok  ', name)
    count = sum(len(v) for v in failed.values())
    for error, names in sorted(failed.items(), key=lambda x: -len(x[1])):
        print('%3d  %s\n     %s' % (len(names), error[:200], ' '.join(names)))
    print('vcc-x86intrinsics: %d of %d builtins as clang\'s'
          % (len(rows) - count, len(rows)))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
