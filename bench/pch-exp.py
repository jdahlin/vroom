#!/usr/bin/env python3
"""Experiment: build a PCH of common kernel headers and measure per-file time with/without."""
import os, shlex, subprocess, sys, time
B = '/Users/johandahlin/dev/vroom/work/build-vroomd'
C = '/opt/homebrew/opt/llvm/bin/clang'
PREFIX = '/Users/johandahlin/dev/vroom/contrib/phase1/pch/vroom-prefix.h'
PCH = '/tmp/claude-vroom.pch'
os.chdir(B)

def cmd_of(obj):
    d, f = os.path.split(obj)
    t = open(os.path.join(d, '.' + f + '.cmd')).read()
    return shlex.split(t.split(':=', 1)[1].strip().split('\n')[0])

def base_args(a):
    out, skip = [], False
    for x in a[1:]:
        if skip: skip = False; continue
        if x.startswith('-Wp,-MMD') or x.startswith('-DKBUILD_') or x.startswith('-D__KBUILD_MODNAME'): continue
        if x == '-c': continue
        if x == '-o': skip = True; continue
        if x.endswith('.c'): continue
        out.append(x)
    return out

def run(args):
    t = time.time(); r = subprocess.run(args, capture_output=True, text=True); return time.time() - t, r

a = cmd_of('kernel/fork.o')
base = [x for x in base_args(a) if x not in ('-I/Users/johandahlin/dev/vroom/work/linux/kernel', '-Ikernel')]
dt, r = run([C] + base + ['-DKBUILD_MODNAME="vroom_pch"', '-x', 'c-header', PREFIX, '-o', PCH])
print(f'PCH: rc={r.returncode} {dt:.2f}s {os.path.getsize(PCH) / 1e6 if r.returncode == 0 else 0:.1f} MB')
if r.returncode: print(r.stderr[-2000:]); sys.exit(1)

PCHFLAGS = ['-include-pch', PCH, '-Xclang', '-fno-validate-pch', '-Wno-macro-redefined']
for obj in sys.argv[1:]:
    a = cmd_of(obj)
    a[0] = C
    a = [x for x in a if not x.startswith('-Wp,-MMD')]
    a[a.index('-o') + 1] = '/tmp/claude-pch-test.o'
    best = {}
    for name, extra in (('PCH+O2', PCHFLAGS), ('PCH+O1', PCHFLAGS + ['-O1']), ('PCH+O1-noinl', PCHFLAGS + ['-O1', '-fno-inline-functions']), ('PCH+O0', PCHFLAGS + ['-O0'])):
        ts = []
        for _ in range(3):
            dt, r = run(a + extra)
            ts.append(dt)
            if r.returncode: print(obj, name, 'ERROR:', r.stderr[-1500:]); break
        best[name] = min(ts)
    print(obj.ljust(32), '  '.join(f'{k}={v:.3f}s' for k, v in best.items()))
