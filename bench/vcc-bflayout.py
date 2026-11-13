#!/usr/bin/env python3
"""vcc-bflayout.py: record layout with bit-fields, generated, checked by clang.

  bench/vcc-bflayout.py [-n N] [--seed S] [--keep DIR]

Writes N random structs and unions (named, unnamed and zero-width
bit-fields of every integer type, plain members between them, aligned()
and packed on fields and records, #pragma pack, nesting) to one file, has
vpp --vcc-layout-checks claim each record's size, alignment and member
offsets for Linux and for Apple arm64, and has clang -fsyntax-only verify
the claims for the same target.  Bit-field positions show in the offset
of the plain member after them.  Exit 1 on any claim clang rejects; the
failing records are printed.  The same seed gives the same file.

x86-64 targets also get ms_struct and gcc_struct on some records; Windows x64
(x86_64-w64-mingw32, LLP64) runs with its default Microsoft layout
(-mms-bitfields) and with -mno-ms-bitfields, and x86-64 Linux once more with
-mms-bitfields (PLAN-win64.md WIN1).
"""
import os, random, re, subprocess, sys, tempfile

B = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hostenv import BUILD, has_wasm
VPP = os.environ.get('VCC_BIN', BUILD) + '/vpp'
CL = os.environ.get('CLANG', 'clang')
TARGETS = ('aarch64-linux-gnu', 'arm64-apple-darwin', 'wasm64-unknown-emscripten', 'wasm32-unknown-emscripten',
           'x86_64-linux-gnu', 'x86_64-linux-gnu -mms-bitfields', 'x86_64-w64-mingw32', 'x86_64-w64-mingw32 -mno-ms-bitfields')
ILP32 = ('wasm32-unknown-emscripten', 'x86_64-w64-mingw32')	# long is 32 bits: its own records
TYPES = [('char', 8), ('signed char', 8), ('unsigned char', 8), ('_Bool', 1), ('short', 16), ('unsigned short', 16),
         ('int', 32), ('unsigned', 32), ('long', 64), ('unsigned long', 64), ('long long', 64)]
PLAIN = ['char', 'short', 'int', 'long', 'char[3]', 'double']


def member(r, i, depth):
    k = r.random()
    if k < 0.3:
        t = r.choice(PLAIN)
        if t.endswith(']'):
            return f'char m{i}[3];'
        return f'{t} m{i};'
    if k < 0.36 and depth < 2:
        return f'{record(r, depth + 1, None)} m{i};'
    t, w = r.choice(TYPES)
    if LONG32 and t in ('long', 'unsigned long'):
        w = 32
    width = r.choice([0, 0, 1, w // 2 or 1, w - 1 or 1, w]) if r.random() < 0.6 else r.randint(0, w)
    attr = ''
    if r.random() < 0.08:
        attr = f'__attribute__((aligned({r.choice([1, 2, 4, 8, 16])}))) '
    elif r.random() < 0.08:
        attr = '__attribute__((packed)) '
    named = width and r.random() < 0.55
    return f'{attr}{t} {"b%d" % i if named else ""}:{width};'


def record(r, depth, name):
    kw = 'union' if r.random() < 0.15 else 'struct'
    attr = ' __attribute__((packed))' if r.random() < 0.12 else ''
    if MSATTR and r.random() < 0.3:	# x86-64: the layout chosen per record
        attr += r.choice([' __attribute__((ms_struct))', ' __attribute__((gcc_struct))'])
    body = ' '.join(member(r, i, depth) for i in range(r.randint(1, 6)))
    return f'{kw}{attr} {name or ""} {{ {body} char z; }}'.replace('  ', ' ')


def gen(n, seed):
    r = random.Random(seed)
    out = []
    for i in range(n):
        pack = r.random() < 0.1
        if pack:
            out.append(f'#pragma pack({r.choice([1, 2, 4])})')
        out.append(record(r, 0, f'r{i}') + ';')
        if pack:
            out.append('#pragma pack()')
    return '\n'.join(out) + '\n'


LONG32 = False
MSATTR = False


def main():
    global LONG32, MSATTR
    av = sys.argv[1:]
    n = int(av[av.index('-n') + 1]) if '-n' in av else 2000
    seed = int(av[av.index('--seed') + 1]) if '--seed' in av else 1
    keep = av[av.index('--keep') + 1] if '--keep' in av else None
    d = keep or tempfile.mkdtemp(prefix='vcc-bflayout-')
    os.makedirs(d, exist_ok=True)
    bad = 0
    targets = TARGETS
    if not has_wasm(VPP):
        targets = [t for t in TARGETS if not t.startswith('wasm')]
        print('wasm targets left out: vpp is built without WebAssembly (make VCC_WASM=1)')
    for tf in targets:
        t, *fl = tf.split()
        LONG32 = t in ILP32
        MSATTR = t.startswith('x86_64')
        src = f'{d}/bf{"32" if LONG32 else ""}{"ms" if MSATTR else ""}.c'
        open(src, 'w').write(gen(n, seed))
        lines = open(src).read().split('\n')
        chk = f'{d}/{t}{"".join(fl)}.chk'
        p = subprocess.run([VPP, f'--target={t}'] + fl + ['--vcc-layout-checks', '-o', chk, src], capture_output=True, text=True)
        if p.returncode:
            print(f'{t}: vpp: {p.stderr[-1500:]}')
            bad += 1
            continue
        claims = open(chk).read()
        both = f'{d}/{t}{"".join(fl)}.c'
        open(both, 'w').write(open(src).read() + claims)
        p = subprocess.run([CL, f'--target={t}'] + fl + ['-fsyntax-only', '-ferror-limit=0', '-w', both], capture_output=True, text=True)
        errs = [l for l in p.stderr.split('\n') if 'static assertion failed' in l or (': error:' in l)]
        recs = sorted({m for l in errs for m in re.findall(r'\b(?:struct|union) (r\d+)', l)}, key=lambda x: int(x[1:]))
        print(f'{tf}: {claims.count("_Static_assert")} claims, {len(errs)} rejected')
        for e in errs[:10]:
            print('  ' + e.split(': error: ', 1)[-1])
        for rn in recs[:5]:
            print('  ' + next(l for l in lines if re.search(rf'\b{rn} {{', l)))
        bad += bool(errs or p.returncode)
    if not keep:
        subprocess.run(['rm', '-rf', d])
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
