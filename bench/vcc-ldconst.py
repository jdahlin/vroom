#!/usr/bin/env python3
"""vcc-ldconst.py: long double (binary128) constants, bit for bit against clang.

  bench/vcc-ldconst.py [-n N] [--seed S] [--keep DIR]

Generates N long double literals (random decimals over the whole exponent
range, exact halfway cases and their neighbours, hex floats, the extremes,
subnormals, overflow) and N folded expressions (+ - * /, comparisons,
conversions to and from double, float and integers) as initializers of
global arrays for aarch64 Linux, compiles the file with clang and vcc and
compares the arrays' bytes in .data.  Exit 1 on any difference; the first
ones are printed with their source.  The same seed gives the same file.
"""
import os, random, subprocess, sys, tempfile

B = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VPP = os.environ.get('VCC_BIN', f'{B}/build/ccs') + '/vpp'
CL = os.environ.get('CLANG', 'clang')
TGT = '--target=aarch64-linux-gnu'


def exact_dec(m, e):
    """m * 2^e as an exact decimal string"""
    if e >= 0:
        return f'{m << e}.0'
    n = m * 5 ** -e
    s = str(n).rjust(-e + 1, '0')
    return f'{s[:e]}.{s[e:]}'


def literal(r):
    k = r.random()
    if k < 0.3:
        nd = r.randint(1, 40)
        d = str(r.randint(1, 9)) + ''.join(r.choice('0123456789') for _ in range(nd - 1))
        p = r.randint(0, nd)
        return f'{d[:p]}.{d[p:]}e{r.randint(-4975, 4940)}L'
    if k < 0.45:
        nd = r.randint(1, 25)
        d = ''.join(r.choice('0123456789') for _ in range(nd))
        return f'{d}.{r.randint(0, 99)}e{r.randint(-30, 30)}L'
    if k < 0.65:	# a tie (114 significant bits, the last one set) or just beside it
        m = (r.getrandbits(113) | 1 << 113) | 1
        e = r.randint(-180, 40)
        s = exact_dec(m, e)
        t = r.random()
        if t < 0.33:
            s += '0000000000000000000000000000001'
        elif t < 0.66 and s.rstrip('0')[-1] != '.':
            s = s.rstrip('0')
            last = s[-1]
            s = s[:-1] + str(int(last) - 1) + '9999999999999999999999999999'
        return s + 'L'
    if k < 0.8:
        nd = r.randint(1, 36)
        h = ''.join(r.choice('0123456789abcdef') for _ in range(nd))
        p = r.randint(0, nd)
        return f'0x{h[:p] or "0"}.{h[p:]}p{r.randint(-16520, 16400)}L'
    return r.choice([
        '1.18973149535723176508575932662800702e4932L', '1.18973149535723176508575932662800703e4932L',
        '1.19e4932L', '1e4933L', '3.36210314311209350626267781732175260e-4932L',
        '6.47517511943802511092443895822764655e-4966L', '3.2e-4966L', '3.3e-4966L', '1e-4967L', '1e-4970L',
        '0x1.ffffffffffffffffffffffffffffp16383L', '0x1p-16494L', '0x1p-16495L', '0x1.8p-16495L', '0x0.0000000000000000000000000001p-16382L',
        '0.1L', '0.0L', '1e-1L', '123456789012345678901234567890123456789.0L', '4.9406564584124654e-324L',
        '0x1.0000000000000000000000000001p0L', '0x1.00000000000000000000000000008p0L', '0x1.00000000000000000000000000018p0L',
        '1e4000L', '1e-4000L', '2.2250738585072014e-308L', '1.7976931348623157e308L'])


def double_lit(r):
    return f'{r.randint(1, 999999)}.{r.randint(0, 999999)}e{r.randint(-320, 300)}'


def expr(r):
    k = r.random()
    a, b = literal(r), literal(r)
    if k < 0.5:
        return f'({a} {r.choice("+-*/")} {b})', 'ld'
    if k < 0.6:
        return f'(long double){double_lit(r)}', 'ld'
    if k < 0.7:
        v = r.getrandbits(r.randint(1, 127))
        return f'(long double){r.choice(["", "-"])}(__int128)((unsigned __int128){v >> 64}ull << 64 | {v & (1 << 64) - 1}ull)', 'ld'
    if k < 0.78:
        return f'(double){a}', 'd'
    if k < 0.85:
        return f'(float){a}', 'f'
    if k < 0.92:
        return f'({a} {r.choice(["<", "<=", ">", ">=", "==", "!="])} {r.choice([b, a])})', 'i'
    return f'(long long)({a} / {literal(r)})', 'l'


def gen(n, seed):
    r = random.Random(seed)
    lits = [literal(r) for _ in range(n)]
    exs = [expr(r) for _ in range(n)]
    cols = {t: [s for s, k in exs if k == t] for t in ('ld', 'd', 'f', 'i', 'l')}
    ty = {'ld': 'long double', 'd': 'double', 'f': 'float', 'i': 'int', 'l': 'long long'}
    src = '#pragma clang diagnostic ignored "-Wliteral-range"\n'
    src += 'long double lit[] = {\n' + ''.join(f'\t{x},\n' for x in lits) + '};\n'
    for t, xs in cols.items():
        src += f'{ty[t]} ex_{t}[] = {{\n' + ''.join(f'\t{x},\n' for x in xs) + '};\n'
    rows = [('lit', 16, lits)] + [(f'ex_{t}', {'ld': 16, 'd': 8, 'f': 4, 'i': 4, 'l': 8}[t], xs) for t, xs in cols.items()]
    return src, rows


def data(obj):
    """symbol -> bytes, from an ELF relocatable: .data, read with the symbol table"""
    import struct
    b = open(obj, 'rb').read()
    shoff, = struct.unpack_from('<Q', b, 0x28)
    shentsize, shnum, shstrndx = struct.unpack_from('<HHH', b, 0x3a)
    sh = [struct.unpack_from('<IIQQQQIIQQ', b, shoff + i * shentsize) for i in range(shnum)]
    names = sh[shstrndx]
    nm = lambda off, tab: b[tab[4] + off:b.index(b'\0', tab[4] + off)].decode()
    out = {}
    for s in sh:
        if s[1] != 2:	# SHT_SYMTAB
            continue
        strtab = sh[s[6]]
        for i in range(s[5] // 24):
            st_name, info, other, shndx, val, size = struct.unpack_from('<IBBHQQ', b, s[4] + i * 24)
            if shndx and shndx < shnum and info & 0xf == 1:
                sec = sh[shndx]
                if sec[1] == 1:	# PROGBITS
                    out[nm(st_name, strtab)] = b[sec[4] + val:sec[4] + val + size]
    return out


def main():
    av = sys.argv[1:]
    n = int(av[av.index('-n') + 1]) if '-n' in av else 3000
    seed = int(av[av.index('--seed') + 1]) if '--seed' in av else 1
    keep = av[av.index('--keep') + 1] if '--keep' in av else None
    d = keep or tempfile.mkdtemp(prefix='vcc-ldconst-')
    os.makedirs(d, exist_ok=True)
    src, rows = gen(n, seed)
    open(f'{d}/ld.c', 'w').write(src)
    res = {}
    for cc, cmd in (('clang', [CL, TGT, '-ffreestanding', '-w']), ('vcc', [VPP, TGT])):
        p = subprocess.run(cmd + ['-c', f'{d}/ld.c', '-o', f'{d}/ld.{cc}.o'], capture_output=True, text=True)
        if p.returncode:
            print(f'{cc}: {p.stderr[-1500:]}')
            sys.exit(1)
        res[cc] = data(f'{d}/ld.{cc}.o')
    bad = tot = 0
    for name, sz, xs in rows:
        a, b = res['clang'].get(name, b''), res['vcc'].get(name, b'')
        for i, x in enumerate(xs):
            tot += 1
            if a[i * sz:(i + 1) * sz] != b[i * sz:(i + 1) * sz]:
                if bad < 12:
                    print(f'{name}[{i}] {x}: clang {a[i*sz:(i+1)*sz][::-1].hex()} vcc {b[i*sz:(i+1)*sz][::-1].hex()}')
                bad += 1
    print(f'ldconst: {tot - bad}/{tot} identical to clang')
    if not keep:
        subprocess.run(['rm', '-rf', d])
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
