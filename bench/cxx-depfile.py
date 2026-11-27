#!/usr/bin/env python3
"""cxx-depfile.py: vpp -c -MD/-MMD of C++ against clang++'s depfiles, byte for
byte, on a generated tree, from its directory and from another one (vpp
--batch -C, as vccd runs a client's job): the object must be the same too.

  bench/cxx-depfile.py [--bin-dir DIR]
"""
import filecmp, os, shutil, subprocess, sys, tempfile

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.environ.get('VCC_BIN') or os.path.join(R, 'build/linux/src')
if '--bin-dir' in sys.argv:
    BIN = sys.argv[sys.argv.index('--bin-dir') + 1]
VPP = os.path.abspath(os.path.join(BIN, 'vpp'))

TREE = {
    'inc/a.h': '#ifndef A_H\n#define A_H\n#include "b.h"\nint a();\n#endif\n',
    'inc/b.h': '#pragma once\nint b();\n',
    'inc/has.h': '\n',
    'inc/data.bin': 'xy',
    'inc/asked.bin': 'z',
    'sys/s.h': '#pragma once\nint s();\n',
    'forced/f.h': '#pragma once\nint f();\n',
    'src/local.h': '#pragma once\nint local();\n',
    'src/m.cpp': '''#include "local.h"
#include <a.h>
#include <a.h>
#include <b.h>
#include <s.h>
#if __has_include(<has.h>) && !__has_include(<absent.h>)
#endif
#if __has_embed("../inc/asked.bin")
#endif
const char data[] = {
#embed "../inc/data.bin"
};
const char *name = __FILE__;
int main() { return a() + b() + s() + f() + local() + data[0]; }
''',
}
FLAGS = ['-x', 'c++', '-std=c++17', '-Iinc', '-isystem', 'sys', '-include',
         'forced/f.h']
MODES = [['-MD'], ['-MMD'], ['-MD', '-MP', '-MT', 'target']]


def main():
    T = tempfile.mkdtemp(prefix='cxx-depfile-')
    tree = os.path.join(T, 'tree')
    for name, text in TREE.items():
        os.makedirs(os.path.dirname(os.path.join(tree, name)), exist_ok=True)
        with open(os.path.join(tree, name), 'w') as f:
            f.write(text)
    failed = 0

    def read(p):
        if not os.path.exists(os.path.join(tree, p)):
            return '(none)\n'
        with open(os.path.join(tree, p)) as f:
            return f.read()

    for mode in MODES:
        args = FLAGS + mode + ['-c', 'src/m.cpp', '-o', 'x.o', '-MF', 'x.d']
        results = {}
        for who in ('clang', 'vpp', 'elsewhere'):
            for p in ('x.o', 'x.d'):
                if os.path.exists(os.path.join(tree, p)):
                    os.unlink(os.path.join(tree, p))
            if who == 'clang':
                p = subprocess.run(['clang++', '-w'] + args, cwd=tree)
            elif who == 'vpp':
                p = subprocess.run([VPP] + args, cwd=tree)
            else:
                jobs = os.path.join(T, 'jobs')
                with open(jobs, 'w') as f:
                    f.write('\x1f'.join(['-'] + args) + '\n')
                p = subprocess.run([VPP, '--batch', jobs, '-j', '1', '-C',
                                    tree], cwd=T, capture_output=True,
                                   env=dict(os.environ, VPP_BATCH_ERR='1'))
            results[who] = read('x.d')
            if p.returncode:
                print(' '.join(mode), who, 'failed', p.returncode)
                failed += 1
            elif who == 'vpp':
                shutil.copy(os.path.join(tree, 'x.o'), os.path.join(T, 'one.o'))
            elif who == 'elsewhere' and not filecmp.cmp(
                    os.path.join(T, 'one.o'), os.path.join(tree, 'x.o'),
                    shallow=False):
                print(' '.join(mode), 'object differs with -C')
                failed += 1
        for who in ('vpp', 'elsewhere'):
            if results[who] != results['clang']:
                print('%s %s differs\nclang: %svpp:   %s' %
                      (' '.join(mode), who, results['clang'], results[who]))
                failed += 1
    shutil.rmtree(T)
    print('cxx-depfile: %s' % ('%d failures' % failed if failed else 'ok'))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
