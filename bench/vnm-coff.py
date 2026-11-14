#!/usr/bin/env python3
"""vnm-coff.py: vnm on COFF objects and archives against llvm-nm (PLAN-win64.md WIN8).

  bench/vnm-coff.py [-j N] [--bin-dir DIR] [-v]

vnm reads COFF x86-64 objects and short import records (FFmpeg's configure
asks `nm test.o` for the symbol prefix, so vcc's side of a Windows build needs
an nm of its own).  Inputs: every archive and object of the MinGW-w64 sysroot
(CRT objects, libmingwex, the import libraries: thousands of members), the
objects of libvccrt.a, and a C file compiled by vpp and by clang for the target.
Each with no option, -g, -u, -U, -n, -p, -S and -A: stdout byte-identical to
llvm-nm 23's and the same success.  Skips without the sysroot.
"""
import glob, os, subprocess, sys
from multiprocessing import Pool
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hostenv

OPTS = ([], ['-g'], ['-u'], ['-U'], ['-n'], ['-p'], ['-S'], ['-A'])
SRC = r'''
int g_data = 1; int g_bss; static int s_data = 2; static int s_bss; const int g_ro = 3;
extern int ext_var; __attribute__((weak)) int weak_fn(void) { return 1; } extern int undef_fn(void);
static int s_fn(int x) { return x + s_data + s_bss; } __attribute__((selectany)) int sel = 5; __thread int tls_var = 7;
int g_fn(void) { return s_fn(g_data) + undef_fn() + ext_var + weak_fn() + sel + tls_var + g_ro; }
'''


def one(job):
    vnm, nm, path, opt = job
    a = subprocess.run([vnm] + opt + [path], capture_output=True)
    b = subprocess.run([nm] + opt + [path], capture_output=True)
    if a.stdout == b.stdout and (a.returncode == 0) == (b.returncode == 0):
        return None
    return f'{path} {" ".join(opt)}: rc {a.returncode} vs {b.returncode}\n  vnm:    {a.stdout[:300]!r} {a.stderr[:200]!r}\n  llvm-nm: {b.stdout[:300]!r}'


def main():
    av = sys.argv[1:]
    j = int(av[av.index('-j') + 1]) if '-j' in av else 6
    bindir = av[av.index('--bin-dir') + 1] if '--bin-dir' in av else os.environ.get('VCC_BIN', hostenv.BUILD)
    verbose = '-v' in av
    lib = f'{hostenv.MINGW_SYSROOT}/x86_64-w64-mingw32/lib'
    if not os.path.isdir(lib):
        print(f'vnm-coff: skip (no MinGW sysroot: {lib})')
        return 0
    out = os.path.join(os.path.dirname(hostenv.BUILD), 'tmp', 'vnm-coff')
    os.makedirs(out, exist_ok=True)
    open(f'{out}/t.c', 'w').write(SRC)
    files = sorted(glob.glob(f'{lib}/*.a') + glob.glob(f'{lib}/*.o') + glob.glob(f'{bindir}/vccrt/obj/*.o'))
    for cc, o in ((f'{bindir}/vpp', 'vcc.o'), (hostenv.llvm('clang'), 'clang.o')):
        if subprocess.run([cc, '--target=x86_64-w64-mingw32', '-O1', '-ffunction-sections', '-c', f'{out}/t.c', '-o', f'{out}/{o}']).returncode == 0:
            files.append(f'{out}/{o}')
    jobs = [(f'{bindir}/vnm', hostenv.llvm('llvm-nm'), f, o) for f in files for o in OPTS]
    with Pool(j) as p:
        bad = [r for r in p.imap_unordered(one, jobs, chunksize=16) if r]
    for r in bad[:20] if not verbose else bad:
        print('DIFF', r)
    print(f'vnm-coff: {len(jobs) - len(bad)}/{len(jobs)} identical to llvm-nm ({len(files)} files x {len(OPTS)} option sets)')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
