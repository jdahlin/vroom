#!/usr/bin/env python3
"""vlink-opts.py: vlink's command-line options against ld.lld (TODO.md vlink Options).

  bench/vlink-opts.py [-v]

Each line of CASES links a small object (and an archive in lib/) with vlink
and with ld.lld.  Both must succeed or both fail; when lld's first error is
expected (MSG) vlink must print the same text; when both succeed, the loaded
images (objcopy -O binary) must be byte-identical.  NOSUP lists options lld
takes that vlink rejects on purpose with a clear error (no dynamic linking).
"""
import re, os, shutil, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hostenv import llvm, BUILD

VLINK, VPP = f'{BUILD}/vlink', f'{BUILD}/vpp'
LLD = shutil.which('ld.lld') or llvm('ld.lld')
OC = llvm('llvm-objcopy')
# as the C suites: vlink's layout without a script is another TODO.md item
LDS = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'ccs/vcc/tests/rt/link.ld')

# (name, args); {o}: the objects, {m}: the main object alone, {a}: the archive directory
CASES = [
    ('plain', '{o}'),
    ('-m aarch64elf', '-m aarch64elf {o}'),
    ('-maarch64linux', '-maarch64linux {o}'),
    ('-mbad', '-mfoo {o}'),
    ('-znotext', '-znotext {o}'),
    ('-z noexecstack', '-z noexecstack -z norelro {o}'),
    ('-zmax-page-size', '-zmax-page-size=4096 {o}'),
    ('-static', '-static {o}'),
    ('-Bstatic -Bdynamic', '-Bstatic -Bdynamic {o}'),
    ('-no-pie', '-no-pie {o}'),
    ('-L -l whole', '-L{a} --whole-archive -lx --no-whole-archive {m}'),
    ('-L sep -l sep', '-L {a} --whole-archive -l x --no-whole-archive {m}'),
    ('-l:file', '-L{a} --whole-archive -l:libx.a --no-whole-archive {m}'),
    ('-l missing', '-L{a} -lnosuch {m}'),
    ('-l no -L', '-lx {m}'),
    ('--start-group', '--start-group {o} --end-group'),
    ('unknown', '--frobnicate {o}'),
    # lazy archives: members extracted as lld (which, and in which order)
    ('lazy -l', '-L{a} {m} -lx'),
    ('lazy before object', '-L{a} -lx {m}'),
    ('lazy chain', '{t}/c.o {a}/libch.a'),
    ('lazy chain reversed members', '{t}/c.o {a}/libchr.a'),
    ('lazy unused member', '{t}/c.o {a}/libch.a {a}/libx.a'),
    ('lazy weak ref', '{t}/w.o {a}/libch.a'),
    ('lazy weak then strong', '{t}/w.o {a}/libch.a {t}/s.o'),
    ('lazy two archives', '{t}/c.o {a}/libp.a {a}/libq.a'),
    ('lazy two archives group', '{t}/c.o --start-group {a}/libq.a {a}/libp.a --end-group'),
    ('lazy already defined', '{t}/c.o {t}/b1.o {a}/libch.a'),
    ('lazy thin', '{t}/c.o {a}/libthin.a'),
    # COMMON: largest size and alignment, strong beats common beats weak
    ('common', '{t}/cm.o {t}/cm1.o'),
    ('common sizes', '{t}/cm.o {t}/cm1.o {t}/cm2.o'),
    ('common sizes reversed', '{t}/cm.o {t}/cm2.o {t}/cm1.o'),
    ('common and strong', '{t}/cm.o {t}/cm1.o {t}/cmd.o {t}/cm2.o'),
    ('common and weak', '{t}/cm.o {t}/cmw.o {t}/cm1.o'),
    ('common in archive', '{t}/cm.o {a}/libcm.a'),
    ('common, archive def not taken', '{t}/cm.o {t}/cm1.o {a}/libcmd.a'),
    # COMDAT: the first group of a signature wins, later members are dropped
    ('comdat', '{t}/g1.o {t}/g2.o'),
    ('comdat reversed', '{t}/g2.o {t}/g1.o'),
    ('comdat three', '{t}/g1.o {t}/g2.o {t}/g3.o'),
    ('comdat in archive', '{t}/g1.o {a}/libg.a'),
    ('comdat any group', '{t}/g1.o {t}/g4.o'),
    # --gc-sections: entry, relocations, init_array (a root), groups
    ('gc', '--gc-sections {t}/gc.o'),
    ('gc -e', '--gc-sections -e alt {t}/gc.o'),
    ('gc comdat', '--gc-sections {t}/g1.o {t}/g2.o'),
    ('gc archive', '--gc-sections {t}/c.o {a}/libch.a'),
    ('no gc', '--gc-sections --no-gc-sections {t}/gc.o'),
    # __start_X / __stop_X: output section X; under gc they keep the X sections
    ('start-stop', '{t}/ss.o {t}/ss2.o'),
    ('start-stop gc', '--gc-sections {t}/ss.o {t}/ss2.o'),
    # GOT slots for local symbols: after the globals, by file (lld postScanRelocations)
    ('got local', '{t}/gl.o {t}/cm.o'),
    ('got local no-relax', '--no-relax {t}/gl.o'),
    # .eh_frame: CIEs merged, FDEs of dead or discarded sections dropped (lld's EhFrameSection)
    ('eh_frame', '{t}/e1.o {t}/e2.o'),
    ('eh_frame comdat', '{t}/e1.o {t}/e2.o {t}/e3.o'),
    ('eh_frame gc', '--gc-sections {t}/e1.o {t}/e2.o {t}/e3.o'),
    ('eh_frame personality', '{t}/e1.o {t}/e2.o {t}/ep.o {t}/pers.o'),
    ('eh_frame personality gc', '--gc-sections {t}/e1.o {t}/e2.o {t}/ep.o {t}/pers.o'),
    ('--eh-frame-hdr', '--eh-frame-hdr {t}/e1.o {t}/e2.o {t}/e3.o {t}/ep.o {t}/pers.o'),
    ('--eh-frame-hdr gc', '--eh-frame-hdr --gc-sections {t}/e1.o {t}/e2.o {t}/e3.o {t}/ep.o {t}/pers.o'),
    ('--eh-frame-hdr without .eh_frame', '--eh-frame-hdr {t}/cm.o'),
    ('--no-eh-frame-hdr', '--eh-frame-hdr --no-eh-frame-hdr {t}/e1.o {t}/e2.o {t}/ep.o {t}/pers.o'),
    # response files (kbuild: ld -r -o m.o @m.mod): quotes, a nested @file, one that does not exist
    ('@file', '@{t}/objs.rsp'),
    ('@file nested quoted', '@{t}/outer.rsp {t}/x.o'),
    ('@missing', '@{t}/nosuch.rsp {t}/a.o {t}/x.o'),
]
MSG = {'-mbad': 'unknown emulation: foo',
       '-l missing': 'unable to find library -lnosuch', '-l no -L': 'unable to find library -lx'}

SRC = 'int d = 3; int x_fn(void); int _start(void) { return d + x_fn(); }\n'
LIB = 'int x_fn(void) { return 4; }\n'
ASM = {	# cm: _start uses cv and cw; cm1/cm2 common of other sizes, cmd defines cv, cmw weakly
    'cm': '.text\n.globl _start\n_start: adrp x0, cv\n ldr x0, [x0, :lo12:cv]\n adrp x1, cw\n'
          ' ldr x1, [x1, :lo12:cw]\n ret\n.comm cv,8,8\n.comm cw,4,4\n',
    'cm1': '.comm cv,16,4\n.comm cz,2,2\n',
    'cm2': '.comm cv,32,32\n.comm cw,64,8\n.globl f2\n.text\nf2: ret\n',
    'cmd': '.data\n.globl cv\n.p2align 3\ncv: .quad 7\n',
    # g1: _start calls f and uses tbl; g2/g3 have the same comdat f (other bodies),
    # g2 calls f too; g4 a non-COMDAT group of the same name (kept)
    'g1': '.text\n.globl _start\n_start: bl f\n bl g2f\n adrp x0, tbl\n ldr x0, [x0, :lo12:tbl]\n ret\n'
          '.section .text.f,"axG",@progbits,f,comdat\n.weak f\nf: mov w0, #1\nlf1: ret\n'
          '.section .data.tbl,"awG",@progbits,f,comdat\n.weak tbl\n.p2align 3\ntbl: .quad lf1\n'
          '.text\n.weak g2f\n',
    'g2': '.text\n.globl g2f\ng2f: b f\n'
          '.section .text.f,"axG",@progbits,f,comdat\n.weak f\nf: mov w0, #2\n nop\nlf2: ret\n'
          '.section .data.tbl,"awG",@progbits,f,comdat\n.weak tbl\n.p2align 3\ntbl: .quad lf2\n',
    'g3': '.section .text.f,"axG",@progbits,f,comdat\n.globl f\nf: mov w0, #3\n ret\n',
    'g4': '.section .text.h,"axG",@progbits,f\n.globl h\nh: ret\n',
    'gl': '.text\n.globl gfun\ngfun: adrp x0, :got:loc\n ldr x0, [x0, :got_lo12:loc]\n adrp x1, :got:cv\n'
          ' ldr x1, [x1, :got_lo12:cv]\n adrp x2, :got:loc2\n ldr x2, [x2, :got_lo12:loc2]\n'
          ' adrp x3, :got:loc\n ldr x3, [x3, :got_lo12:loc]\n ret\n'
          '.data\n.p2align 3\nloc: .quad 1\nloc2: .quad 2\n',
    'cmw': '.data\n.weak cv\n.p2align 3\ncv: .quad 7\n',
}
GC = {	# with -ffunction-sections -fdata-sections
    'ss': 'extern const int __start_my_sec[], __stop_my_sec[];\n'
          'extern const int __start_none[] __attribute__((weak));\n'
          '__attribute__((section("my_sec"), used)) static const int a = 1;\n'
          'int _start(void) { int n = 0; for (const int *p = __start_my_sec; p < __stop_my_sec; p++) n += *p;'
          ' return n + (__start_none != 0); }\n',
    'ss2': '__attribute__((section("my_sec"))) const int b = 2, c = 3;\n',
    'gc': 'int used_d = 1, unused_d = 2; static int sd = 3;\n'
          'int leaf(void) { return used_d; }\n'
          'int dead(void) { return unused_d + leaf(); }\n'
          'static int s(void) { return sd; }\n'
          'int alt(void) { return 7; }\n'
          '__attribute__((constructor)) void ctor(void) { used_d += s(); }\n'
          'int _start(void) { return leaf(); }\n',
}
EH = {	# with -fasynchronous-unwind-tables -ffunction-sections; e3's inline f is a COMDAT
    'e1': 'int e2a(int); int f(int); int ep(int); int _start(void) { return e2a(1) + f(2) + ep(3); }\n'
          'int unused1(int x) { return x * 3; }\n',
    'e2': 'int e2a(int x) { return x + 1; }\nint e2dead(int x) { return x - 1; }\n'
          'inline __attribute__((weak)) int f(int x) { return x * 2; }\n',
    'e3': 'inline __attribute__((weak)) int f(int x) { return x * 4; }\nint e3g(int x) { return f(x); }\n',
}
LAZY = {	# c -> b1 -> b2 -> b3; w refers to b1 weakly; p1 -> q1 -> p2
    'c': 'int b1(void); int _start(void) { return b1(); }\n',
    'w': '__attribute__((weak)) int b1(void); int _start(void) { return b1 ? b1() : 0; }\n',
    's': 'int b1(void); int s(void) { return b1(); }\n',
    'b1': 'int b2(void); int b1(void) { return b2() + 1; }\n',
    'b2': 'int b3(void); int b2(void) { return b3() + 2; }\n',
    'b3': 'int b3(void) { return 3; }\n',
    'unused': 'int unused(void) { return 9; }\n',
    'p1': 'int q1(void); int b1(void) { return q1(); }\n',
    'p2': 'int p2(void) { return 5; }\n',
    'q1': 'int p2(void); int q1(void) { return p2(); }\n',
}

def first_err(s):
    for l in s.splitlines():
        if 'error: ' in l: return l.split('error: ', 1)[1]
    return ''

def main():
    v = '-v' in sys.argv
    bad = 0
    with tempfile.TemporaryDirectory() as t:
        os.mkdir(f'{t}/lib')
        for name, src in ASM.items():
            open(f'{t}/{name}.s', 'w').write(src)
            subprocess.run([VPP, '--target=aarch64-linux-gnu', '-c', f'{t}/{name}.s', '-o', f'{t}/{name}.o'], check=True)
        for name, src in GC.items():
            open(f'{t}/{name}.c', 'w').write(src)
            subprocess.run([VPP, '--target=aarch64-linux-gnu', '-ffunction-sections', '-fdata-sections',
                            '-c', f'{t}/{name}.c', '-o', f'{t}/{name}.o'], check=True)
        for name, src in EH.items():
            open(f'{t}/{name}.c', 'w').write(src)
            subprocess.run([VPP, '--target=aarch64-linux-gnu', '-w', '-fasynchronous-unwind-tables', '-ffunction-sections',
                            '-c', f'{t}/{name}.c', '-o', f'{t}/{name}.o'], check=True)
        # ep: clang -fexceptions, a personality and an LSDA (a cleanup); pers defines the personality
        open(f'{t}/ep.c', 'w').write('void g(int *);\nstatic void cl(int *p) { g(p); }\n'
                                     'int ep(int x) { int __attribute__((cleanup(cl))) v = x; g(&v); return v; }\n'
                                     'void g(int *p) { ++*p; }\n')
        subprocess.run([llvm('clang'), '--target=aarch64-linux-gnu', '-O1', '-fexceptions', '-ffunction-sections',
                        '-c', f'{t}/ep.c', '-o', f'{t}/ep.o'], check=True)
        open(f'{t}/pers.s', 'w').write('.text\n.globl __gcc_personality_v0\n__gcc_personality_v0: ret\n'
                                       '.globl _Unwind_Resume\n_Unwind_Resume: ret\n')
        subprocess.run([VPP, '--target=aarch64-linux-gnu', '-c', f'{t}/pers.s', '-o', f'{t}/pers.o'], check=True)
        for name, src in list({'a': SRC, 'x': LIB}.items()) + list(LAZY.items()):
            open(f'{t}/{name}.c', 'w').write(src)
            subprocess.run([VPP, '--target=aarch64-linux-gnu', '-c', f'{t}/{name}.c', '-o', f'{t}/{name}.o'], check=True)
        for lib, ms, fl in (('x', 'x', 'rcs'), ('ch', 'b1 b2 b3 unused', 'rcs'), ('chr', 'unused b3 b2 b1', 'rcs'),
                            ('p', 'p1 p2', 'rcs'), ('cm', 'cm2', 'rcs'), ('g', 'g2', 'rcs'), ('cmd', 'cmd', 'rcs'), ('q', 'q1', 'rcs'), ('thin', 'b1 b2 b3', 'rcsT')):
            subprocess.run([llvm('llvm-ar'), fl, f'{t}/lib/lib{lib}.a'] + [f'{t}/{m}.o' for m in ms.split()], check=True)
        shutil.copy(f'{t}/a.o', f'{t}/a b.o')
        open(f'{t}/objs.rsp', 'w').write(f'{t}/a.o\n{t}/x.o\n')
        open(f'{t}/outer.rsp', 'w').write(f'"{t}/a b.o" @{t}/inner.rsp\n')
        open(f'{t}/inner.rsp', 'w').write('-e _start\n')
        for name, args in CASES:
            a = args.format(o=f'{t}/a.o {t}/x.o', m=f'{t}/a.o', a=f'{t}/lib', t=t).split()
            r = {}
            for tool, exe in (('vlink', VLINK), ('lld', LLD)):
                out = f'{t}/{tool}.out'
                if os.path.exists(out): os.unlink(out)
                p = subprocess.run([exe, '-T', LDS] + a + ['-o', out], capture_output=True, text=True)
                img = None
                if p.returncode == 0:
                    subprocess.run([OC, '-O', 'binary', out, f'{out}.bin'], check=True)
                    img = open(f'{out}.bin', 'rb').read()
                    # and the program headers (type, offsets, sizes, flags, alignment)
                    ph = subprocess.run([llvm('llvm-readelf'), '-lW', out], capture_output=True, text=True).stdout
                    img = (img, [l.split() for l in ph.splitlines()
                                 if l.strip().split(' ')[0] in ('LOAD', 'TLS', 'GNU_RELRO', 'GNU_EH_FRAME', 'GNU_STACK', 'PHDR', 'NOTE')])
                r[tool] = (p.returncode, first_err(p.stderr), img)
            (vr, ve, vi), (lr, le, li) = r['vlink'], r['lld']
            why = None
            if (vr == 0) != (lr == 0):
                why = f'vlink rc={vr} ({ve}), lld rc={lr} ({le})'
            elif vr == 0 and vi[0] != li[0]:
                why = 'loaded image differs from lld'
            elif vr == 0 and vi[1] != li[1]:
                why = f'program headers differ:\n    vlink {vi[1]}\n    lld   {li[1]}'
            elif name in MSG and (ve != MSG[name] or le != MSG[name]):
                why = f'message: vlink "{ve}", lld "{le}"'
            if why:
                bad += 1
                print(f'FAIL {name}: {why}')
            elif v:
                print(f'ok   {name}: {ve or "linked"}')
        # --print-gc-sections: the same sections removed, in the same order
        pr = []
        for exe in (VLINK, LLD):
            p = subprocess.run([exe, '-T', LDS, '--gc-sections', '--print-gc-sections', f'{t}/gc.o', f'{t}/g4.o',
                                f'{t}/g2.o', '-e', '_start', '-o', f'{t}/pg.out'], capture_output=True, text=True)
            pr.append([l for l in p.stdout.splitlines() if 'removing' in l])
        if pr[0] != pr[1] or not pr[0]:
            bad += 1
            print(f'FAIL --print-gc-sections:\n  vlink {pr[0]}\n  lld   {pr[1]}')
        elif v:
            print(f'ok   --print-gc-sections: {len(pr[0])} removed')
        # -r stays lazy: of two members defining the same symbol only the needed one is
        # read (the kernel's vmlinux.o and libstub's zboot.o vs efi-stub-entry.o)
        shutil.copy(f'{t}/b1.o', f'{t}/b1dup.o')
        subprocess.run([llvm('llvm-ar'), 'rcs', f'{t}/lib/libdup.a', f'{t}/b1.o', f'{t}/b1dup.o'], check=True)
        syms = []
        for exe in (VLINK, LLD):
            p = subprocess.run([exe, '-r', f'{t}/c.o', '--start-group', f'{t}/lib/libdup.a', '--end-group',
                                '-o', f'{t}/r.o'], capture_output=True, text=True)
            nm = subprocess.run([llvm('llvm-nm'), f'{t}/r.o'], capture_output=True, text=True).stdout if p.returncode == 0 else ''
            syms.append((p.returncode, first_err(p.stderr), sorted(nm.split('\n'))))
        if syms[0] != syms[1] or syms[0][0]:
            bad += 1
            print(f'FAIL -r lazy archive:\n  vlink {syms[0][:2]}\n  lld   {syms[1][:2]}')
        elif v:
            print('ok   -r lazy archive')
        # undefined symbols: lld's whole stderr (every reference with its source file and function,
        # 3 then "referenced N more times", "did you mean" for the first two, archive members,
        # blank lines after multi-line errors), the program name aside
        UND = {'un1': 'extern int u1(void); int f(void){return u1();}\nint g(void){return u1()+1;}\n',
               'un2': 'extern int u1(void); int h(void){return u1();}\n',
               'un3': 'extern int u1(void); int a1(void){return u1();}int a2(void){return u1();}'
                      'int a3(void){return u1();}int a4(void){return u1();}\n'
                      'static int (*p)(void)=u1; int *q(void){return (int*)&p;}\n',
               'st': 'int h(void); int _start(void){return h();}\n',
               'sp': 'static int foq(void){return 1;}\nint fooo(void){return 0;} int foo(void); int x_(void);'
                     ' int _start(void){return x_()+foo()+foq();}\nint ffo(void);int g(void){return ffo();}\n'
                     'int foqq(void); int k(void){return foqq();}\n'}
        for name, src in UND.items():
            open(f'{t}/{name}.c', 'w').write(src)
            subprocess.run([llvm('clang'), '--target=aarch64-linux-gnu', '-w', '-c', f'{name}.c', '-o', f'{name}.o'],
                           cwd=t, check=True)
        subprocess.run([llvm('llvm-ar'), 'rc', f'{t}/lu.a', f'{t}/un2.o'], check=True)
        for args in ('un1.o un2.o -e f', 'st.o lu.a', 'un3.o un1.o -e a1', 'sp.o', 'sp.o un3.o', 'un3.o sp.o',
                     'nope1.o nope2.o'):
            errs = []
            # lld scans relocations in parallel: with threads the order of its errors (and so
            # which two get "did you mean") varies run to run; vlink's is lld's sequential one
            for exe in ([VLINK], [LLD, '--threads=1']):
                p = subprocess.run(exe + args.split() + ['-o', 'u.out'], cwd=t, capture_output=True, text=True)
                errs.append(re.sub(r'^\S+: (error|warning): ', r'\1: ', p.stderr, flags=re.M))
            if errs[0] != errs[1]:
                bad += 1
                print(f'FAIL undefined {args}:\n--- vlink\n{errs[0]}--- lld\n{errs[1]}')
            elif v:
                print(f'ok   undefined {args}')
        n0 = 1
        # --version: the word after LLD is a version (scripts/ld-version.sh)
        p = subprocess.run([VLINK, '--version'], capture_output=True, text=True)
        w = p.stdout.split()
        if p.returncode or 'LLD' not in w or not w[w.index('LLD') + 1][:1].isdigit():
            bad += 1
            print(f'FAIL --version: {p.stdout!r}')
        # -r: vlink -r then a final link (ld.lld) gives what lld -r then the final
        # link gives (image, or the error), and the section groups are lld -r's
        def groups(o):
            out = subprocess.run([llvm('llvm-readelf'), '-gSW', o], capture_output=True, text=True).stdout
            names = {m.group(1): m.group(2) for m in re.finditer(r'\[\s*(\d+)\] (\S+)', out)}
            gl = []
            for l in out.splitlines():
                m = re.match(r'(\S+) group section \[\s*\d+\] `\.group\' \[(.*)\] contains', l)
                if m:
                    gl.append([m.group(1), m.group(2)])
                m = re.match(r'\s+\[\s*(\d+)\]\s+(\S+)$', l)
                if m and gl:
                    gl[-1].append(m.group(2))
            return gl
        RCASES = [('-r groups', '{t}/g1.o {t}/g2.o {t}/g4.o'), ('-r comdat three', '{t}/g1.o {t}/g2.o {t}/g3.o'),
                  ('-r eh_frame comdat', '{t}/e1.o {t}/e2.o {t}/e3.o {t}/ep.o {t}/pers.o')]
        for name, args in RCASES:
            a = args.format(t=t).split()
            sh = [subprocess.run([exe, '-r'] + a + ['-o', f'{t}/{tag}.r.o'], capture_output=True, text=True)
                  for exe, tag in ((VLINK, 'v'), (LLD, 'l'))]
            why = None
            if sh[0].returncode or sh[1].returncode:
                why = f'-r: vlink rc={sh[0].returncode} {first_err(sh[0].stderr)}, lld rc={sh[1].returncode}'
            else:
                imgs = []
                for ins in ([f'{t}/v.r.o'], [f'{t}/l.r.o']):
                    q = subprocess.run([LLD, '-T', LDS] + ins + ['-o', f'{t}/r.out'], capture_output=True, text=True)
                    if q.returncode:
                        imgs.append(first_err(q.stderr))
                        continue
                    subprocess.run([OC, '-O', 'binary', f'{t}/r.out', f'{t}/r.bin'], check=True)
                    imgs.append(open(f'{t}/r.bin', 'rb').read())
                if imgs[0] != imgs[1]:
                    why = f'linking the -r output differs from linking lld -r\'s ({imgs[0] if isinstance(imgs[0], str) else "image"})'
                elif groups(f'{t}/v.r.o') != groups(f'{t}/l.r.o'):
                    why = f'groups:\n    vlink {groups(f"{t}/v.r.o")}\n    lld   {groups(f"{t}/l.r.o")}'
            if why:
                bad += 1
                print(f'FAIL {name}: {why}')
            elif v:
                print(f'ok   {name}')
        n0 += len(RCASES)
        n = len(CASES) + 1 + n0
        print(f'vlink-opts: {n - bad}/{n} ok')
    sys.exit(1 if bad else 0)

if __name__ == '__main__': main()
