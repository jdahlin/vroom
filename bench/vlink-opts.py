#!/usr/bin/env python3
"""vlink-opts.py: vlink's command-line options against ld.lld (TODO.md vlink Options).

  bench/vlink-opts.py [-v]

Each line of CASES links a small object (and an archive in lib/) with vlink
and with ld.lld.  Both must succeed or both fail; when lld's first error is
expected (MSG) vlink must print the same text; when both succeed, the loaded
images (objcopy -O binary) must be byte-identical.  NOSUP lists options lld
takes that vlink rejects on purpose with a clear error (no dynamic linking).
"""
import os, shutil, subprocess, sys, tempfile
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
    'cmw': '.data\n.weak cv\n.p2align 3\ncv: .quad 7\n',
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
            subprocess.run([f'{BUILD}/vasm', '--target=aarch64-linux-gnu', '-c', f'{t}/{name}.s', '-o', f'{t}/{name}.o'], check=True)
        for name, src in list({'a': SRC, 'x': LIB}.items()) + list(LAZY.items()):
            open(f'{t}/{name}.c', 'w').write(src)
            subprocess.run([VPP, '--target=aarch64-linux-gnu', '-c', f'{t}/{name}.c', '-o', f'{t}/{name}.o'], check=True)
        for lib, ms, fl in (('x', 'x', 'rcs'), ('ch', 'b1 b2 b3 unused', 'rcs'), ('chr', 'unused b3 b2 b1', 'rcs'),
                            ('p', 'p1 p2', 'rcs'), ('cm', 'cm2', 'rcs'), ('cmd', 'cmd', 'rcs'), ('q', 'q1', 'rcs'), ('thin', 'b1 b2 b3', 'rcsT')):
            subprocess.run([llvm('llvm-ar'), fl, f'{t}/lib/lib{lib}.a'] + [f'{t}/{m}.o' for m in ms.split()], check=True)
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
                r[tool] = (p.returncode, first_err(p.stderr), img)
            (vr, ve, vi), (lr, le, li) = r['vlink'], r['lld']
            why = None
            if (vr == 0) != (lr == 0):
                why = f'vlink rc={vr} ({ve}), lld rc={lr} ({le})'
            elif vr == 0 and vi != li:
                why = 'loaded image differs from lld'
            elif name in MSG and (ve != MSG[name] or le != MSG[name]):
                why = f'message: vlink "{ve}", lld "{le}"'
            if why:
                bad += 1
                print(f'FAIL {name}: {why}')
            elif v:
                print(f'ok   {name}: {ve or "linked"}')
        # --version: the word after LLD is a version (scripts/ld-version.sh)
        p = subprocess.run([VLINK, '--version'], capture_output=True, text=True)
        w = p.stdout.split()
        if p.returncode or 'LLD' not in w or not w[w.index('LLD') + 1][:1].isdigit():
            bad += 1
            print(f'FAIL --version: {p.stdout!r}')
        n = len(CASES) + 1
        print(f'vlink-opts: {n - bad}/{n} ok')
    sys.exit(1 if bad else 0)

if __name__ == '__main__': main()
