#!/usr/bin/env python3
"""Object-level check of vcc's code generator against clang on kernel TUs.

  vcc-obj.py ref [PAT] [-j N]   clang -c with the TU's own flags -> $OBJ/ref
  vcc-obj.py cmp [PAT] [-j N]   vpp -S, clang -c the assembly -> $OBJ/vcc, compare

The comparison is per object.  Errors:
  - defined global symbols differ in binding, kind, section or (objects) size;
  - vcc references a must-be-dead function (__compiletime_assert_*, ...);
  - a data section's relocations point at different named globals.
Notes (differences that follow from clang -O2 inlining more, so not errors):
  - other undefined symbols only one side references;
  - sections only one side has; .text vs .text.unlikely.;
  - relocations of code and of asm-made tables (.altinstructions, __bug_table,
    __jump_table, __ex_table, ...).
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
MUSTDIE = re.compile(r'^__compiletime_assert|^__bad_|_called_with_bad_|^__build_bug|^__unknown_|^__invalid_|^undefined_function')
TABLES = re.compile(r'^(\.altinstructions|__bug_table|__jump_table|__ex_table|\.data\.\.once|runtime_shift_|\.discard|__patchable)')
PLAIN = re.compile(r'^(\.text|\.text\.unlikely\.|\.data|\.bss|\.rodata.*|\.rela.*|\.note.*|\.comment|\.llvm.*|\.symtab|\.strtab|\.shstrtab|)$')

def name(obj, kind): return f'{OBJ}/{kind}/{obj.replace("/", "__")}'

def c_tus(pat):
    return [(o, a) for o, a in vd.tus(pat) if a[-1].endswith('.c') and not o.startswith('scripts/')]

def mkref(t):
    obj, a = t
    p = subprocess.run([CL] + a + ['-c', '-w', '-o', name(obj, 'ref') + '.o'], cwd=B, capture_output=True, text=True)
    return obj, p.returncode, p.stderr[-300:]

def elf(path):
    """sections, {global: (bind, type, section, size)}, undefined, {section: reloc targets}, exec sections"""
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
                syms.append((cstr(stro + st_name), info >> 4, info & 15, shndx, size, val))
    defs, undef = {}, set()
    gobj = {}	# section -> [(start, end, name)] of global objects
    for n, bind, typ, shndx, size, val in syms:
        if bind and n and typ == 1 and 0 < shndx < 0xff00:
            gobj.setdefault(shndx, []).append((val, val + size, n))
    for n, bind, typ, shndx, size, val in syms:
        if bind == 0 or not n: continue
        if shndx == 0: undef.add(n)
        else: defs[n] = (bind, typ, sname[shndx] if shndx < 0xff00 else 'ABS', size if typ == 1 else 0)
    rel = {}
    for i, s in enumerate(sh):
        if s[1] != 4: continue	# SHT_RELA
        tgt = sname[s[7]]
        if tgt.startswith('.debug'): continue
        out = rel.setdefault(tgt, set())
        objs = gobj.get(s[7], [])
        for k in range(s[5] // 24):
            off, info, add = struct.unpack_from('<QQq', d, s[4] + k * 24)
            n, bind, typ, shndx, size, val = syms[info >> 32]
            t = n if n and bind else '.' + sname[shndx] if typ == 3 and shndx < 0xff00 else '?'
            if not (s[2] & 4) and not (sh[s[7]][2] & 4):	# data: only what global objects hold
                held = [o for o in objs if o[0] <= off < o[1]]
                if not held: continue
                t = held[0][2] + '->' + t
            out.add(t)
    exe = {sname[i] for i, s in enumerate(sh) if s[2] & 4}	# SHF_EXECINSTR
    secs = {n for n in sname if not PLAIN.match(n) and not n.startswith('.debug')}
    return secs, defs, undef, rel, exe

def cmpobj(t):
    obj, a = t
    s, o = name(obj, 'vcc') + '.s', name(obj, 'vcc') + '.o'
    t0 = time.time()
    try:
        p = subprocess.run([VPP, '-S', '-o', s] + a, cwd=B, capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired:
        return obj, 1, 'vpp: timeout', 60, False
    dt = time.time() - t0
    if p.returncode: return obj, 1, 'vpp: ' + p.stderr[-1200:], dt, False
    tgt = [x for x in a if x.startswith(('--target=', '-march=', '-mcpu='))]
    q = subprocess.run([CL, '-c', '-x', 'assembler', '-w'] + tgt + ['-o', o, s], capture_output=True, text=True)
    if q.returncode: return obj, 1, 'as: ' + q.stderr[-1200:], dt, False
    ref = name(obj, 'ref') + '.o'
    if not os.path.exists(ref): return obj, 1, 'no reference (run ref first)', dt, False
    rs, rd, ru, rr, rx = elf(ref)
    vs, vdf, vu, vr, vx = elf(o)
    err, note = [], []
    for n in sorted(set(rd) | set(vdf)):
        a1, b1 = rd.get(n), vdf.get(n)
        if a1 != b1:
            # clang -O2 places cold functions in .text.unlikely.
            soft = a1 and b1 and a1[:2] == b1[:2] and {a1[2], b1[2]} == {'.text', '.text.unlikely.'}
            (note if soft else err).append(f'def {n}: clang {a1} vcc {b1}')
    extra = vu - ru - HELPERS - set(rd)
    dead = {n for n in extra if MUSTDIE.search(n)}
    if dead: err.append('dead code left: ' + ' '.join(sorted(dead)[:12]))
    if extra - dead: note.append('undef only in vcc: ' + ' '.join(sorted(extra - dead)[:12]))
    miss = ru - vu - set(vdf)
    if miss: note.append('undef only in clang: ' + ' '.join(sorted(miss)[:12]))
    if rs != vs:	# with different inlining, code-only and table sections come and go
        note.append(f'sections: clang-only {sorted(rs - vs)[:8]} vcc-only {sorted(vs - rs)[:8]}')
    # data: the named globals each data section points at must agree (code differs with inlining;
    # asm-made tables follow the code; section-relative targets name local data)
    for sec in sorted((set(rr) | set(vr)) - rx - vx):
        a1 = {n for n in rr.get(sec, ()) if '->.' not in n and not n.startswith('.')}
        b1 = {n for n in vr.get(sec, ()) if '->.' not in n and not n.startswith('.')}
        if a1 != b1:
            msg = f'relocs {sec}: clang-only {sorted(a1 - b1)[:6]} vcc-only {sorted(b1 - a1)[:6]}'
            (note if TABLES.match(sec) else err).append(msg)
    return obj, len(err) and 1, '\n'.join(err[:20] + (['(notes)'] + note[:6] if note and os.environ.get('NOTES') else [])), dt, len(note) > 0, sorted(extra - dead)

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
    if mode == 'cmp':
        print(f'{sum(1 for r in res if not r[1] and r[4])} of the ok ones with notes (NOTES=1 shows them)')
    if mode == 'cmp' and os.path.exists(f'{B}/System.map'):
        # what vcc leaves undefined that clang folded away must exist in vmlinux, or the link fails
        have = {l.split()[2] for l in open(f'{B}/System.map')}
        st = f'{B}/vmlinux.unstripped.vroom-stubs'	# -O0: vroom-ld stubbed these, they do not exist
        if os.path.exists(st): have -= set(open(st).read().split())
        lost = {}
        for r in res:
            # objcopy prefixes the early-boot and EFI stub objects' symbols
            pre = '__pi_' if r[0].startswith('arch/arm64/kernel/pi/') else \
                  '__efistub_' if r[0].startswith('drivers/firmware/efi/libstub/') else ''
            for n in r[5] if len(r) > 5 else ():
                if pre + n not in have and n not in have:
                    lost.setdefault(n, r[0])
        for n, o in sorted(lost.items())[:40]:
            print(f'undefined in vmlinux: {n} (from {o})')
        if lost:
            print(f'{len(lost)} symbols vcc leaves undefined do not exist in vmlinux (vroom-ld stubs them, as for -O0)')
    print(f'{len(res) - len(bad)}/{len(res)} ok')
