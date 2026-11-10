#!/usr/bin/env python3
"""vcc's ar (vccd, ar.c) against llvm-ar on a kernel build tree.

    bench/var-cmp.py BUILD-DIR      # a tree built with AR=llvm-ar

Replays every archive command Kbuild saved (.*.a.cmd: built-in.a, lib.a,
built-in-fixup.a) with `var` (the vcc shim) writing ARCHIVE.v, then
compares ARCHIVE.v with ARCHIVE byte for byte, and `var t` with `llvm-ar t`
for a few path spellings.  Starts a private vccd.  llvm-ar is only the
reference here; the build itself never runs it.
"""
import glob
import os
import re
import subprocess
import sys
import tempfile
import time

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LLVM_AR = '/opt/homebrew/opt/llvm/bin/llvm-ar'


def main():
    b = os.path.abspath(sys.argv[1])
    tmp = tempfile.mkdtemp(prefix='varcmp', dir=os.environ.get('TMPDIR', '/tmp'))
    sock = os.path.join(tmp, 's')
    d = subprocess.Popen([f'{R}/build/ccs/vccd', '--sock', sock, '--backend-sock', sock + 'x'],
                         stderr=subprocess.DEVNULL)
    time.sleep(0.3)
    env = dict(os.environ, VCC_SOCK=sock, PATH=f'{R}/build/ccs:' + os.environ['PATH'])
    var = f'{R}/build/ccs/var'
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
    subprocess.run(['clang', '--target=aarch64-linux-gnu', '-c', f'{w}/e.c', '-o', f'{w}/e.o'], check=True)
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
        [f'cDrS A {four}', 's A'], [f'cDrs A {four}', 'm A fork.o'], ['q A kernel/fork.o init/main.o'], ['cDPrsT A arch/arm64/kernel/vdso/vdso.so.dbg'],
    ]
    ebad = 0
    for arch in ('.varcmp/a.a', 'a.a'):
        for seq in cases:
            outs = []
            for tool, e in ((LLVM_AR, os.environ), (var, env)):
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
                print(f'DIFF {arch}: {seq}: failed {outs[0][0]} vs {outs[1][0]}, '
                      f'sizes {len(outs[0][1] or b"")} vs {len(outs[1][1] or b"")}')
    subprocess.run(['rm', '-rf', w])
    print(f'edge cases: {2 * len(cases) - ebad}/{2 * len(cases)} identical')
    d.terminate()
    sys.exit(1 if bad or tbad or ebad else 0)


if __name__ == '__main__':
    main()
