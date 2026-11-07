#!/usr/bin/env python3
"""Object-level check of vcc's code generator against clang on kernel TUs.

  vcc-obj.py ref [PAT] [-j N]   clang -c with the TU's own flags -> $OBJ/ref
  vcc-obj.py cmp [PAT] [-j N]   vpp -S, clang -c the assembly -> $OBJ/vcc, compare

The comparison is per object:
  - defined global symbols (name, binding, kind) and the section each is in;
  - undefined symbols: vcc may not reference anything clang does not (that is
    dead code left behind, e.g. __compiletime_assert_*), except the library
    helpers vcc calls where clang inlines (memcpy, __lshrti3, ...);
  - the special sections (everything but .text/.data/.bss/.rodata*/.rela*/
    .note*/.comment/.llvm*) and, per such section, the set of symbols its
    relocations point at, since those carry kernel tables (__ksymtab,
    __ex_table, .altinstructions, __jump_table, .init_array, ...).
Objects go to $OBJ (default: $TMPDIR/vcc-obj), never into the build tree.
"""
import os, re, struct, subprocess, sys, time
from multiprocessing import Pool
import importlib.util
spec = importlib.util.spec_from_file_location('vd', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'vcc-diff.py'))
vd = importlib.util.module_from_spec(spec); spec.loader.exec_module(vd)
B, CL, VPP = vd.B, vd.CL, vd.VPP
OBJ = os.environ.get('OBJ') or os.path.join(os.environ.get('TMPDIR', '/tmp'), 'vcc-obj')
HELPERS = {'memcpy', 'memset', 'memmove', 'strlen', '__udivti3', '__divti3', '__umodti3', '__modti3',
           '__ashlti3', '__lshrti3', '__ashrti3'}
TOLERATED = {'__bug_table'}
PLAIN = re.compile(r'^(\.text|\.data|\.bss|\.rodata.*|\.rela.*|\.note.*|\.comment|\.llvm.*|\.symtab|\.strtab|\.shstrtab|)$')

def name(obj, kind): return f'{OBJ}/{kind}/{obj.replace("/", "__")}'

def c_tus(pat):
    return [(o, a) for o, a in vd.tus(pat) if a[-1].endswith('.c') and not o.startswith('scripts/')]

def mkref(t):
    obj, a = t
    p = subprocess.run([CL] + a + ['-c', '-w', '-o', name(obj, 'ref') + '.o'], cwd=B, capture_output=True, text=True)
    return obj, p.returncode, p.stderr[-300:]

def elf(path):
    """{sections, defined globals, undefined, reloc targets per section}"""
    d = open(path, 'rb').read()
    shoff, = struct.unpack_from('<Q', d, 0x28)
    shentsize, shnum, shstrndx = struct.unpack_from('<HHH', d, 0x3a)
    sh = [struct.unpack_from('<IIQQQQIIQQ', d, shoff + i * shentsize) for i in range(shnum)]
    def cstr(off):
        return d[off:d.index(b'\0', off)].decode()
    sstr = sh[shstrndx][4]
    sname = [cstr(sstr + s[0]) for s in sh]
    syms = []
    for i, s in enumerate(sh):
        if s[1] == 2:		# SHT_SYMTAB
            stro = sh[s[6]][4]
            for k in range(s[5] // 24):
                st_name, info, other, shndx, val, size = struct.unpack_from('<IBBHQQ', d, s[4] + k * 24)
                syms.append((cstr(stro + st_name), info >> 4, info & 15, shndx))
    defs, undef = {}, set()
    for n, bind, typ, shndx in syms:
        if bind == 0 or not n: continue
        if shndx == 0: undef.add(n)
        else: defs[n] = (bind, typ, sname[shndx] if shndx < 0xff00 else 'ABS')
    rel = {}
    for i, s in enumerate(sh):
        if s[1] != 4: continue	# SHT_RELA
        tgt = sname[s[7]]
        if PLAIN.match(tgt) or tgt.startswith('.text.') or tgt.startswith('.debug'): continue
        out = rel.setdefault(tgt, set())
        for k in range(s[5] // 24):
            off, info, add = struct.unpack_from('<QQq', d, s[4] + k * 24)
            n, bind, typ, shndx = syms[info >> 32]
            out.add(n if n and bind else sname[shndx] if typ == 3 and shndx < 0xff00 else n or '?')
    secs = {n for n in sname if not PLAIN.match(n) and not n.startswith('.debug')}
    return secs, defs, undef, rel

def cmpobj(t):
    obj, a = t
    s, o = name(obj, 'vcc') + '.s', name(obj, 'vcc') + '.o'
    t0 = time.time()
    p = subprocess.run([VPP, '-S', '-o', s] + a, cwd=B, capture_output=True, text=True)
    dt = time.time() - t0
    if p.returncode: return obj, 1, 'vpp: ' + p.stderr[-1200:], dt
    tgt = [x for x in a if x.startswith(('--target=', '-march=', '-mcpu='))]
    q = subprocess.run([CL, '-c', '-x', 'assembler', '-w'] + tgt + ['-o', o, s], capture_output=True, text=True)
    if q.returncode: return obj, 1, 'as: ' + q.stderr[-1200:], dt
    ref = name(obj, 'ref') + '.o'
    if not os.path.exists(ref): return obj, 1, 'no reference (run ref first)', dt
    rs, rd, ru, rr = elf(ref)
    vs, vdf, vu, vr = elf(o)
    err = []
    for n in sorted(set(rd) | set(vdf)):
        if rd.get(n) != vdf.get(n):
            err.append(f'def {n}: clang {rd.get(n)} vcc {vdf.get(n)}')
    extra = vu - ru - HELPERS - set(rd)
    if extra: err.append('undef only in vcc: ' + ' '.join(sorted(extra)[:12]))
    miss = ru - vu - set(vdf)
    if miss: err.append('undef only in clang: ' + ' '.join(sorted(miss)[:12]))
    vs -= TOLERATED - rs	# WARN_ON clang proves dead by value ranges
    if rs != vs:
        err.append(f'sections: clang-only {sorted(rs - vs)[:8]} vcc-only {sorted(vs - rs)[:8]}')
    for sec in sorted(set(rr) | set(vr)):
        if sec in TOLERATED and rr.get(sec, set()) <= vr.get(sec, set()): continue
        a1, b1 = rr.get(sec, set()), vr.get(sec, set())
        if a1 != b1:
            err.append(f'relocs {sec}: clang-only {sorted(a1 - b1)[:6]} vcc-only {sorted(b1 - a1)[:6]}')
    return obj, len(err) and 1, '\n'.join(err[:20]), dt

if __name__ == '__main__':
    av = sys.argv[1:]; j = 2
    if '-j' in av: k = av.index('-j'); j = int(av[k+1]); del av[k:k+2]
    mode = av[0]; pat = av[1] if len(av) > 1 else None
    for k in ('ref', 'vcc'): os.makedirs(f'{OBJ}/{k}', exist_ok=True)
    ts = c_tus(pat)
    with Pool(j) as pool:
        res = pool.map(mkref if mode == 'ref' else cmpobj, ts, chunksize=2)
    bad = [r for r in res if r[1]]
    for r in bad[:int(os.environ.get('SHOW', 20))]:
        print(f'--- {r[0]}\n{r[2]}')
    if mode == 'cmp':
        print(f'vpp -S total {sum(r[3] for r in res):.1f} s over {len(res)} TUs')
    print(f'{len(res) - len(bad)}/{len(res)} ok')
