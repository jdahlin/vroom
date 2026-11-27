#!/usr/bin/env python3
"""Proof of concept: skip a C++ compile whose command, compiler and input
files are unchanged (a result cache in "direct mode", as vccd could keep in
memory).  Stockfish cold, warm, and with one header edited, in a copy.

The dependencies are the compile's own -MD, hashed after it: a file edited
while it is compiled is not noticed.  Not covered: a new header that now shadows
one found later on the search path, and __DATE__/__TIME__.
"""
import hashlib, os, shutil, subprocess, time
from concurrent.futures import ThreadPoolExecutor

R = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.environ.get('STOCKFISH_SRC') or os.path.join(R, 'work/stockfish/src')
OUT = os.path.join(R, 'build/stockfish/cache')
VPP = os.path.join(R, 'build/linux/src/vpp')
FLAGS = ['-x', 'c++', '-std=c++17', '-fPIE', '-nostdinc++', '-isystem',
         '/usr/lib/llvm-23/include/c++/v1', '-O2', '-DNDEBUG', '-DIS_64BIT',
         '-DUSE_PTHREADS', '-DNNUE_EMBEDDING_OFF']


def digest(path):
    with open(path, 'rb') as f:
        return hashlib.blake2b(f.read()).digest()


def build(tree, cache):
    seen = {}  # each input hashed once per build

    def current(p):
        if p not in seen:
            seen[p] = digest(os.path.join(tree, p))
        return seen[p]

    def one(s):
        key = (digest(VPP), tuple(FLAGS), s)
        entry = cache.get(key)
        if entry and all(current(p) == h for p, h in entry[0]):
            return 0
        obj = os.path.join(OUT, s.replace('/', '_') + '.o')
        subprocess.run([VPP] + FLAGS + ['-c', s, '-o', obj, '-MD', '-MF',
                                        obj + '.d'], cwd=tree, check=True)
        with open(obj + '.d') as f:
            deps = [w for w in f.read().split(':', 1)[1].split() if w != '\\']
        inputs = [(p, digest(os.path.join(tree, p))) for p in deps]
        with open(obj, 'rb') as f:
            cache[key] = (inputs, f.read())
        return 1

    tus = [os.path.relpath(os.path.join(d, f), tree)
           for d, _, fs in os.walk(tree) for f in fs if f.endswith('.cpp')]
    t = time.time()
    with ThreadPoolExecutor(8) as pool:
        compiled = sum(pool.map(one, tus))
    return '%2d of %d compiled, %.2fs' % (compiled, len(tus), time.time() - t)


def main():
    tree = os.path.join(OUT, 'src')
    shutil.rmtree(OUT, ignore_errors=True)
    shutil.copytree(SRC, tree, ignore=shutil.ignore_patterns('*.nnue', '*.o'))
    cache = {}
    print('cold:          ', build(tree, cache))
    print('unchanged:     ', build(tree, cache))
    with open(os.path.join(tree, 'bitboard.h'), 'a') as f:
        f.write('// edited\n')
    print('bitboard.h:    ', build(tree, cache))


if __name__ == '__main__':
    main()
