#!/usr/bin/env python3
"""vcc's ar (vccd, ar.c) against llvm-ar.

    bench/var-cmp.py BUILD-DIR      # a kernel tree built with AR=llvm-ar
    bench/var-cmp.py coff [-j N] [-n MUTANTS] [--bin-dir DIR] [-v]
                                    # COFF x86-64 (MinGW) and ELF members, no kernel tree

BUILD-DIR: replays every archive command Kbuild saved (.*.a.cmd: built-in.a,
lib.a, built-in-fixup.a) with `var` (the vcc shim) writing ARCHIVE.v, then
compares ARCHIVE.v with ARCHIVE byte for byte, and `var t` with `llvm-ar t`
for a few path spellings, then edge cases.

coff: builds a corpus under build/tmp/varcmp (cached): libvccrt.a's objects
(src/runtime/build.sh), a few hundred COFF objects from the C test suites
compiled by clang for x86_64-w64-mingw32 at -O0 and -O2, ELF objects for
x86_64-linux-gnu, generated COFF objects (every symbol class and section
number llvm treats differently, weak externals by search kind, common,
absolute, long and 8-byte names, bigobj, the machine types llvm's
identify_magic takes for COFF), short import records (code, data, const,
truncated, unterminated), llvm-dlltool's import library, and odd files
(empty, text, one byte, "\\0\\0", long and duplicate names).  Every case is
a sequence of ar commands run by both tools on a fresh archive: the archive
bytes (U: the symbol tables' dates, "now", masked), the exit status of each
command and the output of t and p must be the same; x extracts into two
directories that must hold the same files.  Then -n MUTANTS (default 300)
corrupted COFF objects and import records, each archived alone with rcs:
same status and same bytes (cases where llvm-ar itself crashes are skipped).
The cases also run with SYM64_THRESHOLD=0 (llvm-ar's switch to "/SYM64/").
Starts private vccds.  llvm-ar is only the reference here.
"""
import argparse
import glob
import os
import random
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from multiprocessing import Pool

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hostenv

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LLVM_AR = hostenv.llvm('llvm-ar')
CLANG = hostenv.llvm('clang')


def vccd_start(bindir, sock, env=None):
    d = subprocess.Popen([f'{bindir}/vccd', '--sock', sock], stderr=subprocess.DEVNULL, env=env)
    for _ in range(100):
        if os.path.exists(sock):
            break
        time.sleep(0.05)
    return d


def kernel_main(b, bindir):
    tmp = tempfile.mkdtemp(prefix='varcmp', dir=os.environ.get('TMPDIR', '/tmp'))
    sock = os.path.join(tmp, 's')
    d = vccd_start(bindir, sock)
    env = dict(os.environ, VCC_SOCK=sock, PATH=f'{bindir}:' + os.environ['PATH'])
    var = f'{bindir}/var'
    cmds = []
    for f in glob.glob(f'{b}/**/.*.a.cmd', recursive=True):
        m = re.match(r'savedcmd_(\S+) := (.*)', open(f).read().split('\n')[0])
        if not m or 'llvm-ar' not in m[2]:
            continue
        a = m[1]
        c = m[2].replace('$$', '$').replace('llvm-ar', var)
        c = re.sub(r'(?<![\w/.-])' + re.escape(a) + r'(?![\w/.-])', a + '.v', c)
        cmds.append((a, c))
    bad = 0
    for a, c in sorted(cmds):
        p = subprocess.run(['/bin/sh', '-c', c], cwd=b, env=env, capture_output=True, text=True)
        ref = open(os.path.join(b, a), 'rb').read()
        got = open(os.path.join(b, a + '.v'), 'rb').read() if os.path.exists(os.path.join(b, a + '.v')) else None
        if p.returncode or got != ref:
            bad += 1
            if bad <= 10:
                print(f'DIFF {a}: rc={p.returncode} {p.stderr.strip()[:200]} '
                      f'size {len(ref)} vs {len(got) if got is not None else None}')
        try:
            os.unlink(os.path.join(b, a + '.v'))
        except FileNotFoundError:
            pass
    print(f'archives: {len(cmds) - bad}/{len(cmds)} identical')
    tbad = 0
    tests = [('.', 'kernel/built-in.a'), ('.', './kernel/built-in.a'), ('.', f'{b}/kernel/built-in.a'),
             ('kernel', '../kernel/sched/built-in.a'), ('kernel', 'sched/../sched/built-in.a'),
             ('.', 'built-in-fixup.a'), ('.', 'lib/lib.a'), ('.', 'usr/built-in.a')]
    tests = [(c, ['t', a]) for c, a in tests]
    # a regular archive (made by llvm-ar): t, p, a member that is not there
    os.makedirs(f'{b}/.varcmp', exist_ok=True)
    reg = f'{b}/.varcmp/r.a'
    if os.path.exists(reg):
        os.unlink(reg)
    subprocess.run([LLVM_AR, 'cDPrs', reg, 'kernel/fork.o', 'init/main.o', '.config'], cwd=b, check=True)
    tests += [('.', ['t', reg]), ('.', ['t', reg, 'main.o']), ('.', ['p', reg, '.config']),
              ('.', ['p', reg, 'nope.o'])]
    for cwd, args in tests:
        a = ' '.join(args)
        r1 = subprocess.run([LLVM_AR] + args, cwd=os.path.join(b, cwd), capture_output=True)
        r2 = subprocess.run([var] + args, cwd=os.path.join(b, cwd), env=env, capture_output=True)
        if (r1.returncode, r1.stdout) != (r2.returncode, r2.stdout):
            tbad += 1
            print(f'DIFF t {cwd} {a}: {r1.stdout[:80]!r} vs {r2.stdout[:80]!r} {r2.stderr[:200]!r}')
    print(f't: {len(tests) - tbad}/{len(tests)} identical')

    # edge cases, each a sequence of ar commands run by both in BUILD/.varcmp
    w = os.path.join(b, '.varcmp')
    os.makedirs(w, exist_ok=True)
    open(f'{w}/e.c', 'w').write('static int x; int *f(void);\n')
    subprocess.run([CLANG, '--target=aarch64-linux-gnu', '-c', f'{w}/e.c', '-o', f'{w}/e.o'], check=True)
    four = 'kernel/fork.o init/main.o kernel/exit.o mm/memory.o'
    cases = [
        ['cDPrsT A kernel/fork.o kernel/fork.o'],
        ['cDPrST A kernel/fork.o init/main.o kernel/built-in.a kernel/fork.o'],
        ['cDPrsT A init/main.o'], ['cDPrsT A kernel/built-in.a lib/lib.a'],
        ['cDPrS --thin A init/built-in.a kernel/exit.o'],
        ['cDPrsT A nonexist.o'], ['cDPrST A usr/built-in.a'], ['cDPrsT A usr/built-in.a'],
        ['cDPrsT A .config'], ['cDPrsT A .config init/main.o'], ['cDPrsT A .varcmp/e.o'],
        ['cDPrST A kernel/fork.o init/main.o', 'cDPrST A kernel/exit.o init/main.o'],
        ['cDPrST A kernel/fork.o init/main.o', 'cDPrsT A kernel/exit.o'],
        [f'cDPrST A {four}', 'mPi init/main.o A mm/memory.o kernel/fork.o'],
        [f'cDPrST A {four}', 'mPa init/main.o A mm/memory.o kernel/fork.o'],
        [f'cDPrST A {four}', 'mPb init/main.o A mm/memory.o kernel/fork.o'],
        [f'cDPrST A {four}', 'mPi init/main.o A init/main.o'],
        [f'cDPrST A {four}', 'mP A kernel/fork.o'],
        [f'cDPrsT A {four}', 'mPi kernel/fork.o A mm/memory.o'],
        [f'cDPrST A {four}', 'mPi nonmember.o A mm/memory.o'],
        # regular archives (created only): basenames, long names, symbol table, odd sizes
        [f'cDPrs A {four} .varcmp/e.o'], [f'cDPrS A {four}'], ['crs A kernel/fork.o .config'],
        ['cDPrs A arch/arm64/kernel/vdso/vdso.so.dbg init/main.o'],
        # updating regular archives: r by basename (not with P), q, d, s, m
        [f'cDrs A {four}', 'cDr A kernel/fork.o init/main.o'], [f'cDrs A {four}', 'cDPr A kernel/fork.o'],
        [f'cDrs A {four}', 'q A kernel/fork.o', 'd A fork.o'], [f'cDrs A {four}', 'd A main.o nope.o'],
        [f'cDrS A {four}', 's A'], [f'cDrs A {four}', 'm A fork.o'], ['q A kernel/fork.o init/main.o'],
        # BSD format (llvm-ar on macOS when the first member is not ELF; GNU on Linux), kept on update
        [f'cDrs A .config {four}'], [f'cDrS A .config {four}'], [f'cDrs A .config {four}', 'd A .config'],
        [f'cDrs A {four} .config', 'd A fork.o main.o exit.o memory.o'], ['cDr A .config', 'r A kernel/fork.o'], ['cDPrsT A arch/arm64/kernel/vdso/vdso.so.dbg'],
    ]
    ebad = 0
    # the same sequences with SYM64_THRESHOLD=0 (llvm-ar's test knob, read by vccd from its own
    # environment): /SYM64/ and __.SYMDEF_64 instead of the 32-bit tables
    sock64 = sock + '64'
    d64 = vccd_start(bindir, sock64, env=dict(os.environ, SYM64_THRESHOLD='0'))
    env64 = dict(env, VCC_SOCK=sock64)
    runs = [(arch, seq, dict(os.environ), env) for arch in ('.varcmp/a.a', 'a.a') for seq in cases]
    runs += [('a.a', seq, dict(os.environ, SYM64_THRESHOLD='0'), env64) for seq in cases]
    for arch, seq, lenv, venv in runs:
        outs = []
        for tool, e in ((LLVM_AR, lenv), (var, venv)):
            p = os.path.join(b, arch)
            if os.path.exists(p):
                os.unlink(p)
            rcs = [subprocess.run([tool] + c.replace(' A ', f' {arch} ', 1).replace(' A', f' {arch}').split(),
                                  cwd=b, env=e, capture_output=True).returncode for c in seq]
            outs.append(([rc != 0 for rc in rcs], open(p, 'rb').read() if os.path.exists(p) else None))
            if os.path.exists(p):
                os.unlink(p)
        if outs[0] != outs[1]:
            ebad += 1
            print(f'DIFF {arch}{" sym64" if venv is env64 else ""}: {seq}: failed {outs[0][0]} vs {outs[1][0]}, '
                  f'sizes {len(outs[0][1] or b"")} vs {len(outs[1][1] or b"")}')
    subprocess.run(['rm', '-rf', w])
    print(f'edge cases: {len(runs) - ebad}/{len(runs)} identical (the last {len(cases)} with SYM64_THRESHOLD=0)')
    d.terminate()
    d64.terminate()
    return 1 if bad or tbad or ebad else 0


# ---------------- coff: generated objects ----------------

def coff_object(syms, nsec=1, machine=0x8664, strtab=None, bigobj=False, opt=0):
    """A COFF object with nsec empty sections and syms: (name, value, section,
    storage class, [aux records]); names over 8 bytes go to the string table."""
    strs, table, n = b'', b'', 0
    esz = 20 if bigobj else 18
    for name, value, sec, sclass, aux in syms:
        nb = name if isinstance(name, bytes) else name.encode()
        if len(nb) <= 8:
            fld = nb.ljust(8, b'\0')
        else:
            fld = struct.pack('<II', 0, 4 + len(strs))
            strs += nb + b'\0'
        table += fld + (struct.pack('<IiHBB', value, sec, 0, sclass, len(aux)) if bigobj else
                        struct.pack('<IhHBB', value, sec, 0, sclass, len(aux)))
        for a in aux:
            table += a.ljust(esz, b'\0')
        n += 1 + len(aux)
    secs = b''.join(b'.text\0\0\0' + struct.pack('<IIIIIIHHI', 0, 0, 0, 0, 0, 0, 0, 0, 0x60000020)
                    for _ in range(nsec))
    if bigobj:
        uuid = bytes([0xc7, 0xa1, 0xba, 0xd1, 0xee, 0xba, 0xa9, 0x4b, 0xaf, 0x20, 0xfa, 0xf6, 0x6a, 0xa4, 0xdc, 0xb8])
        hdr = struct.pack('<HHHHI', 0, 0xffff, 2, machine, 0) + uuid + bytes(16) + struct.pack('<III', nsec, 56 + len(secs), n)
    else:
        hdr = struct.pack('<HHIIIHH', machine, nsec, 0, 20 + opt + len(secs), n, opt, 0) + bytes(opt)
    if strtab is None:
        strtab = struct.pack('<I', 4 + len(strs)) + strs
    return hdr + secs + table + strtab


def weak_aux(tag, characteristics):
    return struct.pack('<II', tag, characteristics)


def import_record(name, typ=0, name_type=1, dll=b'dll.dll\0', machine=0x8664, size=None, version=0):
    body = name + dll
    return struct.pack('<HHHHIIHH', 0, 0xffff, version, machine, 0, len(body) if size is None else size, 0,
                       typ | name_type << 2) + body


def synthetic_files():
    """name -> bytes: generated COFF objects, import records and odd files"""
    classes = [('ext_def', 0, 1, 2, []), ('ext_undef', 0, 0, 2, []), ('ext_common', 8, 0, 2, []),
               ('ext_abs', 5, -1, 2, []), ('ext_dbg', 5, -2, 2, []), ('ext_badsec', 0, 7, 2, []),
               ('static', 0, 1, 3, []), ('wk_nolib', 0, 0, 105, [weak_aux(0, 1)]),
               ('wk_lib', 0, 0, 105, [weak_aux(0, 2)]), ('wk_alias', 0, 0, 105, [weak_aux(0, 3)]),
               ('wk_anti', 0, 0, 105, [weak_aux(0, 4)]), ('wk_noaux', 0, 0, 105, []),
               ('wk_def', 0, 1, 105, [weak_aux(0, 2)]), ('label', 0, 1, 6, []), ('func', 0, 1, 101, []),
               ('.sect', 0, 1, 104, []), ('.file', 0, -2, 103, [b'a.c']), ('extdef5', 0, 1, 5, []),
               ('a_long_external_name', 0, 1, 2, []), ('abcdefgh', 0, 1, 2, []), ('ext_neg3', 0, -3, 2, []),
               ('.text', 0, 1, 3, [b'\0' * 18]), ('__imp_x', 0, 1, 2, []), ('dup', 0, 1, 2, []),
               ('Dup', 0, 1, 2, []), ('dup', 4, 1, 2, []), (b'\xc3\xa9t\xc3\xa9', 0, 1, 2, []),
               ('_Z3foov', 0, 1, 2, []), ('?f@@YAXXZ', 0, 1, 2, [])]
    f = {
        'syn/classes.o': coff_object(classes),
        'syn/bigobj.o': coff_object(classes, bigobj=True),
        'syn/nosym.o': coff_object([('local', 0, 1, 3, [])]),
        'syn/nosymtab.o': coff_object([])[:20 + 40],
        'syn/empty_strtab.o': coff_object([('ext_def', 0, 1, 2, [])], strtab=b'\0\0\0\0'),
        'syn/no_strtab.o': coff_object([('ext_def', 0, 1, 2, [])], strtab=b''),
        'syn/unterminated_strtab.o': coff_object([('a_long_external_name', 0, 1, 2, [])])[:-1] + b'x',
        'syn/opt_header.o': coff_object([('with_opt', 0, 1, 2, [])], opt=8),
        'syn/nsec_ffff.o': coff_object([('ext_def', 0, 1, 2, [])])[:2] + b'\xff\xff' + coff_object([('ext_def', 0, 1, 2, [])])[4:],
        'syn/odd_size.o': coff_object([('odd', 0, 1, 2, [])]) + b'\0',
        'syn/zz': b'\0\0',
        'syn/zero_machine.o': coff_object([('m0', 0, 1, 2, [])], machine=0),
        'syn/zero_short': b'\0\0\0\0hello world',
        'syn/emb_nul.o': (lambda b: b[:20 + 40 + 3] + b'\0' + b[20 + 40 + 4:])(coff_object([('abcdefgh', 0, 1, 2, [])])),
        'syn/long_name_strtab_short.o': coff_object([('a_long_external_name', 0, 1, 2, [])], strtab=b'\x0a\0\0\0abcde\0'),
        'syn/bad_long_name.o': coff_object([('a_long_external_name', 0, 1, 2, [])], strtab=b'\4\0\0\0'),
        'syn/sections_past_end.o': coff_object([('x', 0, 1, 2, [])], nsec=1)[:2] + b'\x2c\x01' + coff_object([('x', 0, 1, 2, [])])[4:],
        'syn/truncated.o': coff_object(classes)[:15],
        'imp/code.o': import_record(b'fn_code\0'), 'imp/data.o': import_record(b'var_data\0', 1),
        'imp/const.o': import_record(b'k_const\0', 2), 'imp/type3.o': import_record(b'type3\0', 3),
        'imp/noname_nul.o': import_record(b'unterminated', dll=b''), 'imp/header_only.o': import_record(b'', dll=b''),
        'imp/short8.o': import_record(b'')[:8], 'imp/short4.o': b'\0\0\xff\xff', 'imp/short3.o': b'\0\0\xff',
        'imp/version1.o': import_record(b'v1\0', version=1), 'imp/i386.o': import_record(b'_fn386\0', machine=0x14c),
        'imp/arm64.o': import_record(b'fn_a64\0', machine=0xaa64), 'imp/ordinal.o': import_record(b'ord\0', name_type=0),
        'imp/undecorate.o': import_record(b'?f@@YAXXZ\0', name_type=3), 'imp/empty_name.o': import_record(b'\0'),
        'imp/size_big.o': import_record(b'sizebig\0', size=1000),
        'odd/empty': b'', 'odd/one': b'x', 'odd/three': b'abc', 'odd/text.txt': b'hello\n',
        'odd/a_very_long_member_name_number_one.txt': b'long 1\n', 'odd/fifteen_chars.o': b'15 chars name\n',
        'odd/sixteen_chars.oo': b'16 chars name!\n', 'odd/dir1/same.o': coff_object([('same_one', 0, 1, 2, [])]),
        'odd/dir2/same.o': coff_object([('same_two', 0, 1, 2, [])]),
    }
    # the first two bytes llvm's identify_magic takes for COFF, and some it does not
    for m in (0x8664, 0xaa64, 0x14c, 0x1c4, 0x166, 0x150, 0x1f0, 0x183, 0x184, 0x268, 0x290, 0x168, 0x284, 0x1a6):
        f[f'mach/m{m:04x}.o'] = coff_object([(f'sym_{m:04x}', 0, 1, 2, [])], machine=m)
    for m in (0xa641, 0xa64e):	# ARM64EC/ARM64X: llvm-ar writes "/<ECSYMBOLS>/", var refuses
        f[f'ec/m{m:04x}.o'] = coff_object([(f'sym_{m:04x}', 0, 1, 2, [])], machine=m)
    f['ec/import.o'] = import_record(b'ec_fn\0', machine=0xa641)
    return f


def compile_one(job):
    src, out, flags = job
    if os.path.exists(out) and os.path.getmtime(out) >= os.path.getmtime(src):
        return out
    os.makedirs(os.path.dirname(out), exist_ok=True)
    p = subprocess.run([CLANG] + flags + ['-w', '-c', src, '-o', out + '.tmp'], capture_output=True)
    if p.returncode:
        return None
    os.replace(out + '.tmp', out)
    return out


def corpus_build(W, jobs):
    """the files of the corpus under W/corpus, relative names by group"""
    C = f'{W}/corpus'
    os.makedirs(C, exist_ok=True)
    for name, data in synthetic_files().items():
        p = f'{C}/{name}'
        os.makedirs(os.path.dirname(p), exist_ok=True)
        if not os.path.exists(p) or open(p, 'rb').read() != data:
            open(p, 'wb').write(data)
    # libvccrt.a's objects
    rt = f'{W}/vccrt'
    if not glob.glob(f'{rt}/obj/*.o'):
        subprocess.run(['sh', f'{R}/src/runtime/build.sh', rt], check=True, env=dict(os.environ, AR=LLVM_AR, CC=hostenv.llvm('clang') + ' --target=x86_64-w64-mingw32'))
    os.makedirs(f'{C}/rt', exist_ok=True)
    for o in glob.glob(f'{rt}/obj/*.o'):
        shutil.copy2(o, f'{C}/rt/')
    # the C suites through clang for the target, and some for ELF
    T = f'{R}/src/test'
    srcs = sorted(glob.glob(f'{T}/gcc-torture/execute/*.c'))[::6] + sorted(glob.glob(f'{T}/tcc-tests2/*.c'))[::2] + \
        sorted(glob.glob(f'{T}/vcc/*.c'))[::8]
    res = subprocess.run([CLANG, '-print-resource-dir'], capture_output=True, text=True).stdout.strip() + '/include'
    win = ['--target=x86_64-w64-mingw32', '-nostdinc', '-I', f'{T}/rt/include-win64', '-I', f'{T}/rt/include', '-isystem', res]
    elf = ['--target=x86_64-linux-gnu', '-nostdinc', '-I', f'{T}/rt/include', '-isystem', res]
    js = []
    for i, s in enumerate(srcs):
        base = os.path.basename(s)[:-2]
        js.append((s, f'{C}/c/{base}.o', win + ['-O2' if i % 2 else '-O0']))
        if i % 7 == 0:
            js.append((s, f'{C}/elf/{base}.o', elf + ['-O1']))
    with Pool(jobs) as p:
        outs = [o for o in p.map(compile_one, js) if o]
    # an import library from llvm-dlltool (all members "foo.dll")
    d = f'{C}/implib'
    os.makedirs(d, exist_ok=True)
    open(f'{d}/k.def', 'w').write('LIBRARY foo.dll\nEXPORTS\n  fn_a\n  data_b DATA\n  const_c CONSTANT\n'
                                  '  fn_d @5 NONAME\n  verylongfunctionname_e\n')
    subprocess.run([hostenv.llvm('llvm-dlltool'), '-m', 'i386:x86-64', '-d', f'{d}/k.def', '-l', f'{d}/k.lib'], check=True)
    rel = lambda ps: sorted(os.path.relpath(p, C) for p in ps)
    return C, {
        'rt': rel(glob.glob(f'{C}/rt/*.o')),
        'c': rel(o for o in outs if '/c/' in o),
        'elf': rel(o for o in outs if '/elf/' in o),
    }


# ---------------- coff: running both tools ----------------

def mask_dates(data):
    """the archive with the dates of its symbol tables blanked (U: llvm-ar and var date them now)"""
    if not data or data[:8] != b'!<arch>\n':
        return data
    b = bytearray(data)
    pos = 8
    while pos + 60 <= len(b):
        name = bytes(b[pos:pos + 16])
        try:
            size = int(b[pos + 48:pos + 58])
        except ValueError:
            break
        if name.startswith((b'/ ', b'/SYM64/ ', b'#1/12 ')):
            b[pos + 16:pos + 28] = b'X' * 12
        pos += 60 + size + (size & 1)
    return bytes(b)


class Runner:
    def __init__(self, C, bindir, sock):
        self.C = C
        self.var = f'{bindir}/var'
        self.venv = dict(os.environ, VCC_SOCK=sock)
        self.lenv = dict(os.environ)

    def run(self, seq, archive='out.a'):
        """seq: commands ("A" is the archive; "x ..." runs in a fresh directory);
        per tool: [(failed, stdout)], archive bytes, extracted files"""
        outs = []
        for tool, env in ((LLVM_AR, self.lenv), (self.var, self.venv)):
            p = os.path.join(self.C, archive)
            xd = os.path.join(self.C, 'xdir')
            for q in (p, os.path.join(self.C, 'B.a')):	# B.a: a second archive some cases make first
                if os.path.exists(q):
                    os.unlink(q)
            shutil.rmtree(xd, ignore_errors=True)
            res = []
            for c in seq:
                args = [p if a == 'A' else a for a in c.split()]
                cwd = self.C
                if args[0].startswith('x'):
                    os.makedirs(xd, exist_ok=True)
                    cwd = xd
                r = subprocess.run([tool] + args, cwd=cwd, env=env, capture_output=True)
                if r.returncode < 0:
                    return None	# llvm-ar (or var) crashed: reported by the caller
                res.append((r.returncode != 0, r.stdout))
            xfiles = {}
            if os.path.isdir(xd):
                for f in sorted(os.listdir(xd)):
                    xfiles[f] = open(os.path.join(xd, f), 'rb').read()
            data = open(p, 'rb').read() if os.path.exists(p) else None
            if any('U' in c.split()[0] for c in seq):
                data = mask_dates(data)
            outs.append((res, data, xfiles))
            if os.path.exists(p):
                os.unlink(p)
            shutil.rmtree(xd, ignore_errors=True)
        return outs


def coff_cases(g):
    rt, c, elf = ' '.join(g['rt']), g['c'], g['elf']
    allc = ' '.join(c)
    some = ' '.join(c[:12])
    a, b2, c3 = c[0], c[1], c[2]
    syn = 'syn/classes.o syn/bigobj.o syn/nosym.o syn/nosymtab.o syn/empty_strtab.o syn/no_strtab.o ' \
          'syn/unterminated_strtab.o syn/opt_header.o syn/nsec_ffff.o syn/odd_size.o syn/zero_machine.o ' \
          'syn/emb_nul.o syn/long_name_strtab_short.o'
    imp = 'imp/code.o imp/data.o imp/const.o imp/type3.o imp/noname_nul.o imp/version1.o imp/i386.o ' \
          'imp/ordinal.o imp/undecorate.o imp/empty_name.o imp/size_big.o'
    cases = [
        # the runtime library as src/runtime/build.sh makes it, and the other spellings
        [f'rcsD A {rt}'], [f'rcs A {rt}'], [f'crs A {rt}'], [f'crsD A {rt}'], [f'rc A {rt}'], [f'qc A {rt}'],
        [f'rcS A {rt}'], [f'rcsU A {rt}'], [f'rcU A {rt}'], [f'qcU A {rt}'], [f'rcsDU A {rt}'], [f'rcsUD A {rt}'],
        [f'cru A {rt}'], [f'-rcs A {rt}'], [f'rcsT A {rt}'], [f'rcsTU A {rt}'], [f'rcs --thin A {rt}'],
        # the suites' objects: all at once, and some
        [f'rcs A {allc}'], [f'rcS A {allc}'], [f'rcs A {some} {" ".join(elf[:3])}'],
        [f'rcs A {" ".join(elf[:3])} {some}'],
        # generated objects, import records, odd members
        [f'rcs A {syn}'], [f'rcs A {imp}'], ['rcs A imp/header_only.o imp/short8.o imp/short4.o'],
        ['rcs A imp/short3.o'], ['rcs A syn/zz'], ['rcs A syn/zz syn/classes.o'],
        [f'rcs A {imp} {syn} {some}'], ['rcs A syn/classes.o syn/classes.o'],
        ['rcs A odd/text.txt syn/classes.o'], ['rcs A odd/text.txt'], ['rcs A odd/empty'],
        ['rcs A odd/empty syn/classes.o odd/one odd/three'], ['rcs A syn/nosym.o odd/text.txt'],
        [f'rcs A syn/classes.o odd/a_very_long_member_name_number_one.txt odd/fifteen_chars.o odd/sixteen_chars.oo {a}'],
        ['rcs A odd/dir1/same.o odd/dir2/same.o'], ['rcs A odd/dir1/same.o', 'rcs A odd/dir2/same.o'],
        [f'rcs A {elf[0]} syn/classes.o imp/code.o'], [f'rcs A syn/classes.o {elf[0]} imp/code.o'],
        ['rcs A implib/k.lib'], ['rcs A'], ['rc A'], ['rcS A syn/classes.o'],
        ['rcs A ' + ' '.join(f'mach/m{m:04x}.o' for m in (0x8664, 0x14c, 0x1c4, 0x166, 0x150, 0x1f0,
                                                               0x183, 0x184, 0x268, 0x290, 0x168, 0x284, 0x1a6))],
        ['rcs A mach/m0168.o syn/classes.o'], ['rcs A mach/m01a6.o'], ['rcs A mach/maa64.o imp/arm64.o'],
        # malformed members: both fail (and with S, where no symbol is read, both succeed)
        ['rcs A syn/bad_long_name.o'], ['rcs A syn/sections_past_end.o'], ['rcs A syn/truncated.o'],
        ['rcs A syn/zero_short'], ['rcS A syn/truncated.o syn/zero_short'], ['rcs A nonexistent.o'],
        # updates: r replaces by basename, q appends, d, s, m, x, t, p
        [f'rcs A {a} {b2} {c3}', f'rcs A {b2}'], [f'rcs A {a} {b2}', f'rcs A {c3}'],
        [f'rcs A {a} {b2} {c3}', f'd A {os.path.basename(b2)}'],
        [f'rcs A {a} {b2} {c3}', f'd A {os.path.basename(a)} {os.path.basename(b2)} {os.path.basename(c3)}'],
        [f'rcs A {a} {b2}', f'qc A {a}'], [f'qc A {a}', f'qc A {b2}'], [f'rcS A {a} {b2}', 's A'],
        [f'rcS A {a} {b2}', f'rcs A {c3}'], ['rcS A odd/text.txt syn/classes.o', 's A'],
        ['rcS A odd/text.txt', 'rcs A syn/classes.o'], ['rcs A odd/text.txt', 'd A text.txt', 'rcs A syn/classes.o'],
        [f'rcs A {elf[0]}', f'rcs A {a}'], [f'rcs A {a}', f'rcs A {elf[0]}'],
        [f'rcs A {a} {b2} {c3}', f'm A {os.path.basename(a)}'], [f'rcs A {a} {b2} {c3}', f'mb {os.path.basename(a)} A {os.path.basename(c3)}'],
        [f'rcsU A {a} {b2}', f'rcs A {c3}'], [f'rcs A {a} {b2}', f'rcsU A {c3}'], [f'rcsU A {a} {b2}', f'rcsD A {c3}'],
        [f'rcs A {rt}', 't A'], [f'rcs A {rt}', f't A {os.path.basename(g["rt"][0])}'],
        [f'rcs A {rt}', f'p A {os.path.basename(g["rt"][0])}'], [f'rcs A {rt}', 'x A'],
        [f'rcs A {a} {b2}', f'x A {os.path.basename(b2)}'], ['rcs A implib/k.lib', 't A'],
        [f'rcs A {rt}', 'd A nope.o'], ['rcs A odd/a_very_long_member_name_number_one.txt', 't A', 'x A'],
        # thin archives of COFF members (GNU's table, every symbol), an archive as a member
        [f'cDPrsT A {some}'], [f'cDPrST A {some}'], [f'rcsT A {a} {b2}', f'rcsT A {c3}'],
        ['rcs B.a syn/classes.o', 'rcs A B.a syn/nosym.o'], ['rcsT B.a syn/classes.o', 'rcs A B.a'],
        ['rcsT B.a syn/classes.o', 'rcsT A B.a imp/code.o'],
    ]
    # ELF only: the kernel mode's edge cases (Kbuild's thin archives, m, P, q, d, s) without a kernel tree
    e1, e2, e3, e4 = elf[:4]
    four = f'{e1} {e2} {e3} {e4}'
    b1, b3 = os.path.basename(e1), os.path.basename(e3)
    cases += [
        [f'cDPrsT A {e1} {e1}'], [f'cDPrsT A {e2}'], [f'cDPrsT A nonexist.o'], ['cDPrsT A odd/text.txt'],
        [f'cDPrsT A odd/text.txt {e2}'], [f'cDPrST A {e1} {e2}', f'cDPrST A {e3} {e2}'],
        [f'cDPrST A {e1} {e2}', f'cDPrsT A {e3}'], [f'cDPrST A {four}', f'mPi {e2} A {e4} {e1}'],
        [f'cDPrST A {four}', f'mPa {e2} A {e4} {e1}'], [f'cDPrST A {four}', f'mPb {e2} A {e4} {e1}'],
        [f'cDPrST A {four}', f'mP A {e1}'], [f'cDPrST A {four}', f'mPi nonmember.o A {e4}'],
        ['cDPrsT B.a ' + four, f'cDPrST A B.a {e1}'], ['cDPrsT B.a ' + four, f'cDPrS --thin A B.a {e1}'],
        [f'cDPrs A {four}'], [f'cDPrS A {four}'], [f'crs A {e1} odd/text.txt'],
        [f'cDrs A {four}', f'cDr A {e1} {e2}'], [f'cDrs A {four}', f'cDPr A {e1}'],
        [f'cDrs A {four}', f'q A {e1}', f'd A {b1}'], [f'cDrs A {four}', f'd A {b3} nope.o'],
        [f'cDrS A {four}', 's A'], [f'cDrs A {four}', f'm A {b1}'], [f'q A {e1} {e2}'],
        [f'cDrs A odd/text.txt {four}'], [f'cDrS A odd/text.txt {four}'], [f'cDrs A odd/text.txt {four}', 'd A text.txt'],
        ['cDr A odd/text.txt', f'r A {e1}'], [f'rcsU A {four}'], [f'rcsTU A {four}'],
    ]
    return cases


def mutants(C, g, n, seed):
    """n corrupted COFF objects and import records: (name, bytes)"""
    rnd = random.Random(seed)
    pool = [open(f'{C}/{p}', 'rb').read() for p in g['rt'][:10] + g['c'][:30]]
    pool += [v for k, v in synthetic_files().items() if k.startswith(('syn/', 'imp/')) and len(v) >= 4]
    out = []
    for i in range(n):
        b = bytearray(rnd.choice(pool))
        for _ in range(rnd.randint(1, 3)):
            k = rnd.random()
            if k < 0.15 and len(b) > 4:
                del b[rnd.randrange(4, len(b)):]	# truncate
            elif k < 0.55 and len(b) >= 20:
                # a header field: sections, symbol table offset, count, optional header size
                off, fmt = rnd.choice([(2, '<H'), (8, '<I'), (12, '<I'), (16, '<H'), (6, '<H'), (18, '<H'), (12, '<I')])
                v = rnd.choice([0, 1, 0xffff if fmt == '<H' else 0xffffffff, rnd.randrange(0, len(b) + 64),
                                len(b), len(b) - 4, rnd.getrandbits(16 if fmt == '<H' else 32)])
                struct.pack_into(fmt, b, off, v)
            else:
                # bytes in the symbol or string table
                if len(b) >= 20 and b[:2] != b'\0\0':
                    so = struct.unpack_from('<I', b, 8)[0]
                    lo = so if 0 < so < len(b) else 20
                else:
                    lo = 0
                for _ in range(rnd.randint(1, 4)):
                    if len(b) > lo:
                        b[rnd.randrange(lo, len(b))] = rnd.choice([0, 1, 2, 3, 0x69, 0xff, rnd.getrandbits(8)])
        out.append((f'mut/{i}.o', bytes(b)))
    return out


def coff_main(args):
    W = f'{R}/build/tmp/varcmp'
    C, g = corpus_build(W, args.j)
    print(f'corpus: {len(g["rt"])} runtime objects, {len(g["c"])} COFF objects from the suites, '
          f'{len(g["elf"])} ELF, {len(synthetic_files())} generated files')
    tmp = tempfile.mkdtemp(prefix='varcmp', dir=f'{R}/build/tmp')
    sock, sock64 = f'{tmp}/s', f'{tmp}/s64'
    d = vccd_start(args.bin_dir, sock)
    d64 = vccd_start(args.bin_dir, sock64, env=dict(os.environ, SYM64_THRESHOLD='0'))
    bad = 0
    total = 0
    try:
        cases = coff_cases(g)
        runs = [(seq, None) for seq in cases] + [(seq, '0') for seq in cases]
        crashed = 0
        for seq, thr in runs:
            r = Runner(C, args.bin_dir, sock64 if thr else sock)
            if thr:
                r.lenv['SYM64_THRESHOLD'] = thr
            o = r.run(seq)
            total += 1
            if o is None:
                crashed += 1
                print(f'CRASH {seq[:2]}{" sym64" if thr else ""}')
                bad += 1
                continue
            if o[0] != o[1]:
                bad += 1
                desc = ' | '.join(s[:120] for s in seq)
                print(f'DIFF{" sym64" if thr else ""}: {desc}: failed {[x[0] for x in o[0][0]]} vs {[x[0] for x in o[1][0]]}, '
                      f'sizes {len(o[0][1] or b"")} vs {len(o[1][1] or b"")}, stdout same {[x[1] for x in o[0][0]] == [x[1] for x in o[1][0]]}, '
                      f'x same {o[0][2] == o[1][2]}')
        print(f'cases: {total - bad}/{total} identical (the last {len(cases)} with SYM64_THRESHOLD=0)')
        # ARM64EC/ARM64X: llvm-ar adds "/<ECSYMBOLS>/", var refuses (not a difference counted above)
        r = Runner(C, args.bin_dir, sock)
        ecs = ['ec/ma641.o', 'ec/ma64e.o', 'ec/import.o', 'mach/maa64.o mach/m8664.o', 'imp/arm64.o imp/code.o',
               'syn/classes.o imp/arm64.o']
        ec = [r.run([f'rcs A {p}']) for p in ecs]
        refused = sum(1 for o in ec if o and o[1][0][0][0] and not o[1][1])
        print(f'ARM64EC/ARM64X archives: var refuses {refused}/{len(ecs)} (llvm-ar writes /<ECSYMBOLS>/)')
        bad += refused != len(ecs)
        mbad = mskip = 0
        md = f'{C}/mut'
        shutil.rmtree(md, ignore_errors=True)
        os.makedirs(md)
        ms = mutants(C, g, args.n, 1)
        for name, data in ms:
            open(f'{C}/{name}', 'wb').write(data)
        fails = 0
        for name, data in ms:
            o = r.run([f'rcs A {name}'])
            if o is None:
                mskip += 1
                continue
            fails += o[0][0][0][0]
            if o[0] != o[1]:
                mbad += 1
                if mbad <= 10 or args.v:
                    print(f'DIFF mutant {name}: failed {o[0][0][0][0]} vs {o[1][0][0][0]}, sizes '
                          f'{len(o[0][1] or b"")} vs {len(o[1][1] or b"")}')
        print(f'mutants: {len(ms) - mskip - mbad}/{len(ms) - mskip} identical ({fails} rejected by llvm-ar, '
              f'{mskip} skipped: a tool crashed)')
        bad += mbad
    finally:
        d.terminate()
        d64.terminate()
        shutil.rmtree(tmp, ignore_errors=True)
    return 1 if bad else 0


def main():
    if len(sys.argv) > 1 and sys.argv[1] == 'coff':
        ap = argparse.ArgumentParser(prog='var-cmp.py coff')
        ap.add_argument('-j', type=int, default=4)
        ap.add_argument('-n', type=int, default=300, help='mutants')
        ap.add_argument('-v', action='store_true')
        ap.add_argument('--bin-dir', default=os.environ.get('VCC_BIN', hostenv.BUILD))
        args = ap.parse_args(sys.argv[2:])
        args.bin_dir = os.path.abspath(args.bin_dir)
        sys.exit(coff_main(args))
    ap = argparse.ArgumentParser()
    ap.add_argument('build_dir')
    ap.add_argument('--bin-dir', default=os.environ.get('VCC_BIN', hostenv.BUILD))
    args = ap.parse_args()
    sys.exit(kernel_main(os.path.abspath(args.build_dir), os.path.abspath(args.bin_dir)))


if __name__ == '__main__':
    main()
