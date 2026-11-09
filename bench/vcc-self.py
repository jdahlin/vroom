#!/usr/bin/env python3
"""vcc-self.py: vcc compiled by itself to WebAssembly (PLAN-wasm.md W6).

  bench/vcc-self.py [-j N] [--self DIR] [--ref DIR] [--target T]... [PAT]

ccs/wasm/build.sh builds vcc.wasm twice: by emcc (--ref, default
build/wasm-emcc) and by vcc itself (VCC=vpp, --self, default
build/wasm-self).  Every test of vcc-ctests.py is preprocessed by the native
vpp for each target (default aarch64-linux-gnu and
wasm64-unknown-emscripten), and both vcc.wasm run `-O2 -S` on it under node
(ccs/wasm/selfrun.mjs).  Passes when the outputs (assembly, or the
diagnostics and exit status) are byte-identical.
"""
import hashlib, importlib.util, json, os, subprocess, sys, time
from multiprocessing import Pool
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hostenv import BUILD

_spec = importlib.util.spec_from_file_location('ctests', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'vcc-ctests.py'))
ct = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(ct)
R = ct.R
NODE = 'node'
OUT = f'{BUILD}/selftests'
RUN = f'{R}/ccs/wasm/selfrun.mjs'
EMSR = None

def pre(a):
    """the native vpp's -E for one test and target (the runtime headers, clang's resource dir)"""
    suite, name, path, target = a
    o = f'{OUT}/{target.split("-")[0]}/{suite}-{name}.i'
    rc = subprocess.run([ct.VPP, f'--target={target}', '-nostdinc', '-I', f'{ct.RT}/include', '-isystem', ct.RES,
                         '-O2', '-w', '-E', path, '-o', o], capture_output=True).returncode
    return (target, o) if rc == 0 else None

def run(a):
    d, lst = a
    p = subprocess.run([NODE, RUN, d, lst], capture_output=True, text=True)
    if p.returncode:
        raise RuntimeError(f'{RUN} {d}: {p.stderr[-2000:]}')
    return {(r['target'], r['i']): (r['rc'], r['out']) for r in map(json.loads, p.stdout.splitlines())}

def main():
    args = sys.argv[1:]
    jobs, ref, self_, targets, pat = 6, f'{R}/build/wasm-emcc', f'{R}/build/wasm-self', [], None
    while args:
        a = args.pop(0)
        if a == '-j': jobs = int(args.pop(0))
        elif a.startswith('-j'): jobs = int(a[2:])
        elif a == '--ref': ref = os.path.abspath(args.pop(0))
        elif a == '--self': self_ = os.path.abspath(args.pop(0))
        elif a == '--target': targets.append(args.pop(0))
        else: pat = a
    targets = targets or ['aarch64-linux-gnu', 'wasm64-unknown-emscripten']
    for t in targets:
        os.makedirs(f'{OUT}/{t.split("-")[0]}', exist_ok=True)
    t0 = time.time()
    work = [(s, n, p, t) for s, n, p in ct.tests(None, pat) for t in targets]
    with Pool(jobs) as pool:
        ins = [x for x in pool.map(pre, work, chunksize=8) if x]
        # chunks per node process: half the jobs for each vcc.wasm
        k = max(1, jobs // 2)
        lists = []
        for i in range(k):
            lst = f'{OUT}/list{i}'
            with open(lst, 'w') as f:
                f.writelines(f'{t}\t{o}\n' for t, o in ins[i::k])
            lists.append(lst)
        res = pool.map(run, [(d, l) for d in (ref, self_) for l in lists], chunksize=1)
    a, b = {}, {}
    for r in res[:k]: a.update(r)
    for r in res[k:]: b.update(r)
    bad = [key for key in a if a[key] != b.get(key)]
    ok = sum(1 for key in a if a[key][0] == 0)
    for t, i in bad[:20]:
        print(f'DIFF {t} {os.path.basename(i)}  ref rc {a[(t, i)][0]} self rc {b.get((t, i), ("missing",))[0]}')
    print(f'{len(a) - len(bad)}/{len(a)} outputs identical ({ok} compiled, {len(work) - len(ins)} not preprocessed); '
          f'{time.time() - t0:.1f} s')
    sys.exit(1 if bad else 0)

if __name__ == '__main__':
    main()
