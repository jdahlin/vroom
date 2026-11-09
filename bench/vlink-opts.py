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
]
MSG = {'-mbad': 'unknown emulation: foo',
       '-l missing': 'unable to find library -lnosuch', '-l no -L': 'unable to find library -lx'}

SRC = 'int d = 3; int x_fn(void); int _start(void) { return d + x_fn(); }\n'
LIB = 'int x_fn(void) { return 4; }\n'

def first_err(s):
    for l in s.splitlines():
        if 'error: ' in l: return l.split('error: ', 1)[1]
    return ''

def main():
    v = '-v' in sys.argv
    bad = 0
    with tempfile.TemporaryDirectory() as t:
        os.mkdir(f'{t}/lib')
        for name, src in (('a', SRC), ('x', LIB)):
            open(f'{t}/{name}.c', 'w').write(src)
            subprocess.run([VPP, '--target=aarch64-linux-gnu', '-c', f'{t}/{name}.c', '-o', f'{t}/{name}.o'], check=True)
        subprocess.run([llvm('llvm-ar'), 'rcs', f'{t}/lib/libx.a', f'{t}/x.o'], check=True)
        for name, args in CASES:
            a = args.format(o=f'{t}/a.o {t}/x.o', m=f'{t}/a.o', a=f'{t}/lib').split()
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
