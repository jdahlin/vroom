#!/usr/bin/env python3
"""Build the kernel with vcc's code generator and link it with kbuild/lld.

    vcc-kbuild.py DST [-j N] [--only PAT] [--skip PAT]

1. DST (a directory on the case-sensitive work volume) becomes a copy of the
   reference build tree (work/build-o0) unless it exists already.
2. Every kernel C TU (not scripts/) is compiled with `vpp -S` and assembled
   with clang's integrated assembler into DST, over clang's object.  The .cmd
   files are untouched, so kbuild sees the objects as up to date.
3. `make Image` in DST relinks: built-in.a archives, vmlinux, Image.
Then boot it: bench/boot-test.py --kernel DST/arch/arm64/boot/Image

TUs that vcc cannot compile keep clang's object; they are listed at the end
(and in DST/vcc-fallback.txt), so the boot test says how many objects are vcc's.
"""
import os, re, shutil, subprocess, sys, time
from multiprocessing import Pool
import importlib.util
spec = importlib.util.spec_from_file_location('vd', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'vcc-diff.py'))
vd = importlib.util.module_from_spec(spec); spec.loader.exec_module(vd)
B, CL, VPP = vd.B, vd.CL, vd.VPP
R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VDSO = 'arch/arm64/kernel/vdso/'
ASFLAGS = ('--target=', '-march=', '-mcpu=', '-Wa,', '-ffixed-', '-mbranch-protection')

def build(t):
    obj, a, dst = t
    s = f'{dst}/{obj}.vcc.s'
    p = subprocess.run([VPP, '-S', '-o', s] + a, cwd=dst, capture_output=True, text=True, timeout=120)
    if p.returncode:
        return obj, 'vpp: ' + p.stderr.strip().splitlines()[-1][:300] if p.stderr.strip() else 'vpp failed'
    tmp = f'{dst}/{obj}.vcc.o'
    q = subprocess.run([CL, '-c', '-x', 'assembler', '-w'] + [x for x in a if x.startswith(ASFLAGS)] + ['-o', tmp, s],
                       capture_output=True, text=True)
    if q.returncode:
        return obj, 'as: ' + q.stderr.strip()[:300]
    os.replace(tmp, f'{dst}/{obj}')
    return obj, None

if __name__ == '__main__':
    av = sys.argv[1:]; j = 2; only = skip = None
    if '-j' in av: k = av.index('-j'); j = int(av[k+1]); del av[k:k+2]
    if '--only' in av: k = av.index('--only'); only = av[k+1]; del av[k:k+2]
    if '--skip' in av: k = av.index('--skip'); skip = av[k+1]; del av[k:k+2]
    dst = os.path.abspath(av[0])
    if not os.path.exists(dst):
        print(f'copying {B} -> {dst}', flush=True)
        subprocess.run(['cp', '-a', B, dst], check=True)
    # the vDSO needs PIC (vcc has none): clang builds it; delete a vcc object an earlier run left there
    for o, a in vd.tus(only):
        if o.startswith(VDSO) and os.path.exists(f'{dst}/{o}.vcc.s'):
            for x in (f'{dst}/{o}', f'{dst}/{o}.vcc.s'):
                if os.path.exists(x): os.remove(x)
    ts = [(o, a, dst) for o, a in vd.tus(only) if a[-1].endswith('.c') and '-E' not in a and not o.startswith(('scripts/', VDSO))
          and not (skip and re.search(skip, o))]
    t0 = time.time()
    with Pool(j) as pool:
        res = pool.map(build, ts, chunksize=2)
    bad = [r for r in res if r[1]]
    with open(f'{dst}/vcc-fallback.txt', 'w') as f:
        for o, e in bad: f.write(f'{o}\t{e}\n')
    print(f'{len(res) - len(bad)}/{len(res)} objects from vcc ({time.time() - t0:.0f} s); fallbacks in {dst}/vcc-fallback.txt')
    for o, e in bad[:20]: print(f'  {o}: {e}')
    env = dict(os.environ)
    # like the -O0 reference build: vroom-ld stubs what only dead code refers to (listed in
    # vmlinux.unstripped.vroom-stubs; compare with the reference tree's list)
    sh = (f'source {R}/env/macos-env.sh && kmake -C {R}/work/linux O={dst} -j{j} LD={R}/ccs/vroom-ld Image')
    mt = {o: os.stat(f'{dst}/{o}').st_mtime_ns for o, e in res if not e and o != 'init/version-timestamp.o'}	# relinked every time
    r = subprocess.run(['zsh', '-c', sh], env=env)
    # kbuild may decide to rebuild a TU (a freshly copied tree): then that object is clang's again
    redone = [o for o, t in mt.items() if not os.path.exists(f'{dst}/{o}') or os.stat(f'{dst}/{o}').st_mtime_ns != t]
    if redone:
        print(f'{len(redone)} vcc objects were rebuilt by kbuild (clang), e.g. {" ".join(redone[:5])}: run again')
        sys.exit(1)
    st = f'{dst}/vmlinux.unstripped.vroom-stubs'
    ref = f'{vd.B}/vmlinux.unstripped.vroom-stubs'
    if r.returncode == 0 and os.path.exists(st):
        mine = set(open(st).read().split())
        theirs = set(open(ref).read().split()) if os.path.exists(ref) else set()
        print(f'{len(mine)} stubbed symbols (clang -O0: {len(theirs)}); not stubbed for clang: '
              + ' '.join(sorted(mine - theirs)[:30]))
    sys.exit(r.returncode)
