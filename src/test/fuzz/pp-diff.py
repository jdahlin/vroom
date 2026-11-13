#!/usr/bin/env python3
"""pp-diff.py: differential check of fuzz-pp inputs, vpp against clang -E -P.

  src/test/fuzz/pp-diff.py [-j N] [--out DIR] [--show N] [--text] PATH...

PATH is a file or directory of fuzz-pp inputs (corpus, crash-*, regress/pp).
Each input is unpacked as fuzz-pp sees it (main \\x01 a.h \\x01 b.h \\x01
sys/c.h, a leading \\x02 for assembler-with-cpp) into a temporary directory,
preprocessed by clang (-E -P, the kernel's target, -nostdinc -isystem sys)
and compared by vpp --cmp, which lexes both with vcc's lexer and compares
kind and spelling, as bench/vcc-diff.py does.  --text compares the bytes of
vpp -E -P instead (spacing, indentation, blank lines).

Inputs that clang rejects (non-zero status) are skipped, as are inputs that
use __DATE__, __TIME__ or __TIMESTAMP__.  Mismatches (vpp differs, fails, or
crashes where clang succeeds) are listed and copied to OUT (default
build/src/fuzz/diff/) with the vpp message next to them.
"""
import argparse, hashlib, os, shutil, subprocess, sys, tempfile
from multiprocessing import Pool

H = os.path.dirname(os.path.abspath(__file__))
R = os.path.dirname(os.path.dirname(os.path.dirname(H)))	# src/test/fuzz/ -> the checkout
CL = '/opt/homebrew/opt/llvm/bin/clang'
VPP = f'{R}/build/src/vpp'
NAMES = ['main', 'a.h', 'b.h', 'sys/c.h']
SKIP = (b'__DATE__', b'__TIME__', b'__TIMESTAMP__')
TEXT = '--text' in sys.argv


def unpack(data, d):
    asm = data[:1] == b'\x02'
    if asm:
        data = data[1:]
    parts = data.split(b'\x01', 3)
    os.makedirs(f'{d}/sys')
    main = f'{d}/main.S' if asm else f'{d}/main.c'
    for i, p in enumerate(parts):
        with open(main if i == 0 else f'{d}/{NAMES[i]}', 'wb') as f:
            f.write(p)
    args = ['--target=aarch64-linux-gnu', '-nostdinc', '-isystem', f'{d}/sys']
    args += ['-x', 'assembler-with-cpp', '-D__ASSEMBLY__'] if asm else ['-std=gnu11']
    return args + [main]


def check(path):
    data = open(path, 'rb').read()
    if any(s in data for s in SKIP):
        return path, 'skip', ''
    d = tempfile.mkdtemp(prefix='ppdiff-')
    try:
        args = unpack(data, d)
        ref = f'{d}/ref.i'
        try:
            c = subprocess.run([CL] + args + ['-E', '-P', '-o', ref], cwd=d, capture_output=True, timeout=20)
        except subprocess.TimeoutExpired:
            return path, 'skip', 'clang timeout'
        if c.returncode:
            return path, 'skip', 'clang rejects'
        try:
            if TEXT:
                v = subprocess.run([VPP, '-E', '-P'] + args, cwd=d, capture_output=True, timeout=20)
                r = open(ref, 'rb').read()
                if v.returncode == 0 and v.stdout != r:
                    import difflib
                    dl = difflib.unified_diff(r.decode('latin-1').splitlines(), v.stdout.decode('latin-1').splitlines(),
                                              'clang', 'vpp', n=0, lineterm='')
                    return path, 'diff', '\n'.join(list(dl)[:16])
            else:
                v = subprocess.run([VPP, '--cmp', ref] + args, cwd=d, capture_output=True, timeout=20)
        except subprocess.TimeoutExpired:
            return path, 'diff', 'vpp timeout (clang finished)'
        if v.returncode == 0:
            return path, 'ok', ''
        why = 'vpp crashed (signal %d)' % -v.returncode if v.returncode < 0 else 'vpp status %d' % v.returncode
        msg = (v.stdout + v.stderr).decode('latin-1').replace(d, '$T')
        return path, 'diff', f'{why}\n{msg[-1500:]}'
    finally:
        shutil.rmtree(d, ignore_errors=True)


def inputs(paths):
    for p in paths:
        if os.path.isdir(p):
            for f in sorted(os.listdir(p)):
                if not f.startswith('.') and os.path.isfile(f'{p}/{f}'):
                    yield f'{p}/{f}'
        else:
            yield p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-j', type=int, default=4)
    ap.add_argument('--out', default=f'{R}/build/src/fuzz/diff')
    ap.add_argument('--show', type=int, default=10)
    ap.add_argument('--text', action='store_true')
    ap.add_argument('paths', nargs='+')
    a = ap.parse_args()
    if not os.access(VPP, os.X_OK):
        sys.exit(f'pp-diff: {VPP} missing (make -C src vcc)')
    ins = list(inputs(a.paths))
    with Pool(a.j) as pool:
        res = pool.map(check, ins, chunksize=8)
    n = {'ok': 0, 'skip': 0, 'diff': 0}
    bad = []
    for path, st, msg in res:
        n[st] += 1
        if st == 'diff':
            bad.append((path, msg))
    if bad:
        os.makedirs(a.out, exist_ok=True)
    for path, msg in bad:
        data = open(path, 'rb').read()
        h = hashlib.sha1(data).hexdigest()[:16]
        shutil.copyfile(path, f'{a.out}/{h}')
        with open(f'{a.out}/{h}.txt', 'w') as f:
            f.write(f'{path}\n{msg}\n')
    for path, msg in bad[:a.show]:
        print(f'--- {path}\n{msg}')
    print(f'{n["ok"]} same, {n["diff"]} differ, {n["skip"]} skipped (clang rejects or time macros)'
          + (f'; mismatches in {a.out}' if bad else ''))
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
