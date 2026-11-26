#!/usr/bin/env python3
"""
bench/vcc-x86intrinsics.py: every builtin of the generated tables
(frontend/x86-instructions.inc, frontend/x86-stencil-names.inc) called on
pseudo-random and extreme operands, vpp's result against clang's, on an
x86-64 host.  A builtin whose clang program does not run here (the CPU lacks
the instruction, it is privileged) or gives another result each time is only
compiled; the count says how many.

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

# ---- the stencil table's builtins

ELEMENTS = {
    'c': 'char', 'C': 'unsigned char', 'N': 'short', 'h': 'unsigned short',
    'i': 'int', 'u': 'unsigned', 'l': 'long', 'L': 'unsigned long',
    'w': 'long long', 'W': 'unsigned long long', 'f': 'float', 'd': 'double',
    'x': '_Float16', 'g': '__bf16', 'v': 'void',
}


def vector_name(element, size):
    return 'v%d_%s' % (size, element.replace(' ', '_'))


def vector_typedefs():
    return ''.join(
        'typedef %s %s __attribute__((vector_size(%d)));\n'
        % (element, vector_name(element, size), size)
        for element in ELEMENTS.values() if element != 'void'
        for size in (8, 16, 32, 64))


def signature_types(signature):
    """[(kind, C type)]: kind 'v' vector, 'p' pointer, 's' scalar, '0' void"""
    types = []
    at = 0

    def next_type():
        nonlocal at
        code = signature[at]
        at += 1
        if code in '*^':
            _, pointee = next_type()
            if code == '^':
                pointee += ' const'
            return 'p', pointee + ' *'
        if code in '3456':
            _, element = next_type()
            return 'v', vector_name(element, 1 << int(code))
        if code == 'v':
            return '0', 'void'
        return 's', ELEMENTS[code]

    while at < len(signature):
        types.append(next_type())
    return types


def stencil_rows(bin_dir):
    """(name, signature, features, constant mask, keys) per stencil"""
    names = os.path.join(R, 'src', 'include', 'frontend',
                         'x86-stencil-names.inc')
    code = os.path.join(R, 'src', 'arch', 'x64', 'codegen', 'x86-stencils.inc')
    with open(names) as f:
        listed = re.findall(r'^X86_STENCIL\("([^"]+)", "([^"]*)"\)', f.read(),
                            re.M)
    with open(code) as f:
        text = f.read()
    rows = re.findall(r'^X86_STENCIL_ROW\("([^"]*)", (\d+), (\d+), (\d+), '
                      r'-?\d+\)', text, re.M)
    variants = [(int(k), int(n)) for k, n in
                re.findall(r'^\{(\d+)u,(\d+),\d+,\d+,\d+\},', text, re.M)]
    made = []
    for (name, signature), (features, mask, first, count) in zip(listed, rows):
        keys = []
        for key, many in variants[int(first):int(first) + int(count)]:
            keys.extend(range(key, key + many))
        picked = sorted({keys[i * (len(keys) - 1) // 5] for i in range(6)})
        made.append((name, signature, features, int(mask), picked))
    return made


def feature_flags(features):
    text = features
    while True:
        inner = re.search(r'\(([^()]*)\)', text)
        if not inner:
            break
        text = (text[:inner.start()] + inner[1].split('|')[0] +
                text[inner.end():])
    text = ','.join(part.split('|')[0] for part in text.split(','))
    return ['-m' + f for f in dict.fromkeys(text.split(',')) if f]


def stencil_program(name, signature, mask, keys):
    result, *parameters = signature_types(signature)
    indexed = ('gather' in name or 'scatter' in name)
    calls = []
    for key in keys:
        values = []
        declarations = []
        constants = 0
        for i, (kind, t) in enumerate(parameters):
            if mask & (1 << i):
                values.append(str((key >> (8 * constants)) & 0xff))
                constants += 1
                continue
            if kind == 'p':
                values.append('(%s)(memory + 4096 + 64 * %d)' % (t, i))
                continue
            declarations.append('%s a%d; memcpy(&a%d, data[%d], sizeof a%d);'
                                % (t, i, i, i % 6, i))
            # (a scalar clzg or ctzg without a fallback: undefined for 0)
            if ('zg/0' in name) and len(parameters) == 1:
                declarations.append('a0 |= 1;')
            if indexed and i == 2 and kind == 'v':
                declarations.append(
                    'for (unsigned k = 0; k < sizeof a2 / sizeof a2[0]; k++) '
                    'a2[k] &= 31;')
            values.append('a%d' % i)
        # (a generic builtin's row is NAME/SHAPE/OPERANDS)
        call = '%s(%s)' % (name.split('/')[0], ', '.join(values))
        if result[0] == '0':
            calls.append('    { %s\n      %s; }\n'
                         % (' '.join(declarations), call))
        else:
            calls.append('    { %s\n      %s r = %s;\n      '
                         'add(&r, sizeof r); }\n'
                         % (' '.join(declarations), result[1], call))
    return (STENCIL_PRELUDE + vector_typedefs() + 'static __attribute__((noinline)) void one(void)'
            '\n{\n' + ''.join(calls) + '    add(memory, sizeof memory);\n}\n\n'
            'int main(void)\n{\n'
            '    for (int round = 0; round < 48; round++) {\n'
            '        fill(round);\n        one();\n    }\n'
            '    printf("%016llx\\n", hash);\n    return 0;\n}\n')


STENCIL_PRELUDE = '''#include <stdio.h>
#include <string.h>

static unsigned long long hash = 0xcbf29ce484222325ull;
static unsigned long long state = 1;
static unsigned char data[6][64] __attribute__((aligned(64)));
static unsigned char memory[8192] __attribute__((aligned(64)));

static void add(const void *p, int size)
{
    const unsigned char *b = (const unsigned char *)p;
    for (int i = 0; i < size; i++)
        hash = (hash ^ b[i]) * 0x100000001b3ull;
}

static void fill(int round)
{
    for (int i = 0; i < 6 * 64 + 8192; i += 8) {
        unsigned long long r;
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        r = state ^ (state >> 29);
        if (round % 8 == 1)
            r &= 0x0303030303030303ull;
        if (round % 8 == 2)
            r = 0x8000800080008000ull;
        if (round % 8 == 3)
            r = 0x7fff7fff80008000ull;
        if (round % 8 == 4) {
            float f[2] = {(float)((int)r % 1000) / 8,
                          (float)((int)(r >> 32) % 1000) / 8};
            memcpy(&r, f, 8);
        }
        if (round % 8 == 5) {
            double d = (double)(int)r / 16;
            memcpy(&r, &d, 8);
        }
        if (i < 6 * 64)
            memcpy((unsigned char *)data + i, &r, 8);
        else
            memcpy(memory + i - 6 * 64, &r, 8);
    }
    if (round % 8 == 6)
        memcpy(data[1], data[0], 64);
}
'''


# processor state is in their results: only compiled
STATE = re.compile(r'^__builtin_ia32_(readeflags|rdpid|xsave)')


def check_stencil(row, vpp, levels, directory, language):
    name, signature, features, mask, keys = row
    short = re.sub(r'\W', '_', name.replace('__builtin_ia32_', ''))
    source = os.path.join(directory, short + '.c')
    with open(source, 'w') as f:
        f.write(stencil_program(name, signature, mask, keys))
    flags = feature_flags(features)
    want, error = build_and_run(['clang'], source, directory, short + '-c',
                                '-O1', flags)
    if want is None and 'Cannot select' in error:
        # (as the generator: AVX-512's usual set)
        flags += ['-mavx512f', '-mavx512vl', '-mavx512bw', '-mavx512dq']
        want, error = build_and_run(['clang'], source, directory, short + '-c',
                                    '-O1', flags)
    runs = want is not None
    if not runs and not error.startswith('exit'):
        return short, 'harness: clang: ' + error, False
    if STATE.match(name):
        runs = False
    if runs:
        again = run([os.path.join(directory, short + '-c')])
        runs = again.stdout.strip() == want
    for level in levels:
        command = [vpp, '-x', language]
        if not runs:
            obj = os.path.join(directory, short + level + '.o')
            p = run(command + flags + [level, '-c', source, '-o', obj])
            if p.returncode:
                lines = p.stderr.strip().splitlines() or ['status']
                errors = [x for x in lines if 'error' in x] or lines
                return short, '%s: %s' % (level,
                                          errors[0].split('error: ')[-1]), False
            continue
        got, error = build_and_run(command, source, directory, short + level,
                                   level, flags)
        if got is None:
            return short, '%s: %s' % (level, error), True
        if got != want:
            return short, '%s: result differs from clang\'s' % level, True
    return short, '', runs


def run(command):
    return subprocess.run(command, capture_output=True, text=True, timeout=120)


def build_and_run(compiler, source, directory, tag, level, flags=FLAGS):
    obj = os.path.join(directory, tag + '.o')
    p = run(compiler + flags + [level, '-c', source, '-o', obj])
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
        compiled = []
        stencils = [r for r in stencil_rows(a.bin_dir) if a.pattern in r[0]]
        with concurrent.futures.ThreadPoolExecutor(a.j) as pool:
            for name, error, runs in pool.map(
                    lambda r: check_stencil(r, vpp,
                                            ['-O' + c for c in a.levels],
                                            directory, language), stencils):
                if error:
                    failed.setdefault(error, []).append(name)
                else:
                    if not runs:
                        compiled.append(name)
                    if a.v:
                        print('ok  ', name)
    count = sum(len(v) for v in failed.values())
    for error, names in sorted(failed.items(), key=lambda x: -len(x[1])):
        print('%3d  %s\n     %s' % (len(names), error[:200], ' '.join(names)))
    total = len(rows) + len(stencils)
    print('vcc-x86intrinsics: %d of %d builtins as clang\'s (%d of them only '
          'compiled: not runnable here)'
          % (total - count, total, len(compiled)))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
