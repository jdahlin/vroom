#!/usr/bin/env python3
# Symbolic comparison of two ld -r outputs: sections, symbols, relocations.
import struct, sys, collections
def load(p):
    d = open(p, 'rb').read()
    shoff, = struct.unpack_from('<Q', d, 0x28); shnum, shstrndx = struct.unpack_from('<HH', d, 0x3c)
    sh = [struct.unpack_from('<IIQQQQIIQQ', d, shoff + i*64) for i in range(shnum)]
    so = sh[shstrndx][4]
    nm = lambda o, base: d[base+o:d.index(b'\0', base+o)].decode()
    names = [nm(s[0], so) for s in sh]
    symsec = next(i for i,s in enumerate(sh) if s[1] == 2)
    st = sh[sh[symsec][6]][4]
    syms = []
    for j in range(sh[symsec][5] // 24):
        n, info, other, shndx, val, size = struct.unpack_from('<IBBHQQ', d, sh[symsec][4] + j*24)
        syms.append((nm(n, st), info, other, shndx, val, size))
    return d, sh, names, syms
def secname(names, shndx):
    return {0:'UND', 0xfff1:'ABS'}.get(shndx, names[shndx] if shndx < len(names) else '?')
def strat(d, sh, i, off):
    b = sh[i][4] + off
    if not sh[i][2] & 0x20: return d[b:b+sh[i][9]]
    return d[b:d.index(b'\0', b)]
def analyse(p):
    d, sh, names, syms = load(p)
    merge = {i for i,s in enumerate(sh) if s[2] & 0x10}
    secs = {names[i]: (s[1], s[2], s[5] if i not in merge else None, s[8]) for i,s in enumerate(sh) if s[1] not in (2,3,4,0x6fff4c03) and i}
    symset = collections.Counter()
    for (n, info, other, shndx, val, size) in syms[1:]:
        if info & 15 == 3: continue
        v = strat(d, sh, shndx, val) if shndx in merge else val
        symset[(n, info, other, secname(names, shndx), v, size)] += 1
    rel = {}
    for i, s in enumerate(sh):
        if s[1] != 4: continue
        tgt = s[7]; out = []
        for j in range(s[5] // 24):
            off, info, add = struct.unpack_from('<QQq', d, s[4] + j*24)
            sym = syms[info >> 32]; typ = info & 0xffffffff
            if sym[1] & 15 == 3 or (sym[0].startswith('.L') and sym[3] in merge):
                sec = sym[3]; a = add + (sym[4] if sym[1] & 15 != 3 else 0)
                t = ('S', names[sec], strat(d, sh, sec, a) if sec in merge else a)
            else:
                t = ('Y', sym[0], add)
            out.append((off, typ, t))
        rel[names[tgt]] = sorted(out)
    return secs, symset, rel
a = analyse(sys.argv[1]); b = analyse(sys.argv[2])
ok = True
for k in sorted(set(a[0]) | set(b[0])):
    if a[0].get(k) != b[0].get(k): print('section', k, a[0].get(k), b[0].get(k)); ok = False
da = a[1] - b[1]; db = b[1] - a[1]
for x in list(da)[:10]: print('only in A sym', x)
for x in list(db)[:10]: print('only in B sym', x)
ok &= not da and not db
for k in sorted(set(a[2]) | set(b[2])):
    if a[2].get(k) != b[2].get(k):
        ok = False; ra, rb = a[2].get(k, []), b[2].get(k, [])
        print('relocs differ', k, len(ra), len(rb))
        for x, y in zip(ra, rb):
            if x != y: print('  ', x, y); break
print('symbols', sum(a[1].values()), 'relocs', sum(len(v) for v in a[2].values()), 'OK' if ok else 'DIFF')
