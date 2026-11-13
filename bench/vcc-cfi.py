#!/usr/bin/env python3
"""vcc-cfi.py: unwind tables (TODO.md CFI / .eh_frame).

  bench/vcc-cfi.py [-j N] [-v] [PAT]

For every C file in src/tests/vcc and tests/gcc-torture/execute (matching
PAT), compiled for aarch64 Linux with -fasynchronous-unwind-tables:
  - llvm-dwarfdump --eh-frame parses it, with one FDE per function symbol,
    each covering exactly that function;
  - the unwind tables do not change the code: .text is byte-identical
    without them;
  - a function with a frame ends at CFA = W29 + 16, W30 at CFA-8, W29 at
    CFA-16 (the frame record), its callee-saved registers below.
"""
import argparse, glob, os, re, subprocess, sys, tempfile
from multiprocessing import Pool
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hostenv import llvm, BUILD
R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VPP = os.path.join(os.environ.get('VCC_BIN') or BUILD, 'vpp')
DD, OC, NM = llvm('llvm-dwarfdump'), llvm('llvm-objcopy'), llvm('llvm-nm')
RT = f'{R}/src/tests/rt/include'
FL = ['--target=aarch64-linux-gnu', '-w', '-ffreestanding', '-I', RT]

def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, **kw)

def text(o, t):
    run([OC, '-O', 'binary', '--only-section=.text', o, t])
    return open(t, 'rb').read()

FDE = re.compile(r'^[0-9a-f]+ [0-9a-f]+ [0-9a-f]+ FDE cie=[0-9a-f]+ pc=([0-9a-f]+)\.\.\.([0-9a-f]+)')

def one(f):
    with tempfile.TemporaryDirectory() as t:
        d, b = os.path.dirname(f), os.path.basename(f)
        if run([VPP] + FL + ['-c', b, '-o', f'{t}/n.o'], cwd=d).returncode:
            return ('skip', f, '')
        p = run([VPP] + FL + ['-fasynchronous-unwind-tables', '-c', b, '-o', f'{t}/u.o'], cwd=d, text=True)
        if p.returncode:
            return ('fail', f, 'vpp -c: ' + p.stderr[:200])
        if text(f'{t}/n.o', f'{t}/n.t') != text(f'{t}/u.o', f'{t}/u.t'):
            return ('fail', f, 'unwind tables change .text')
        funcs = {}
        for l in run([NM, '-S', '--defined-only', f'{t}/u.o'], text=True).stdout.splitlines():
            x = l.split()
            if len(x) == 4 and x[2] in 'tTwW' and int(x[1], 16):
                funcs[int(x[0], 16)] = int(x[1], 16)
        out = run([DD, '--eh-frame', f'{t}/u.o'], text=True)
        if out.returncode:
            return ('fail', f, 'dwarfdump: ' + out.stderr[:200])
        fdes, cur = {}, None
        for l in out.stdout.splitlines():
            m = FDE.match(l)
            if m:
                cur = int(m.group(1), 16)
                fdes[cur] = [int(m.group(2), 16) - cur, []]
            elif cur is not None and re.match(r'\s+0x[0-9a-f]+: CFA=', l):
                fdes[cur][1].append(l.split(': ', 1)[1])
        if set(fdes) != set(funcs):
            return ('fail', f, f'{len(fdes)} FDEs for {len(funcs)} functions')
        for a, (n, rows) in fdes.items():
            if n != funcs[a]:
                return ('fail', f, f'FDE at {a:#x} covers {n:#x}, the function {funcs[a]:#x}')
            last = rows[-1] if rows else ''
            if len(rows) > 1 and not (last.startswith('CFA=W29+16') and 'W29=[CFA-16]' in last and 'W30=[CFA-8]' in last):
                return ('fail', f, f'FDE at {a:#x} ends at {last}')
    return ('ok', f, '')

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-j', type=int, default=6)
    ap.add_argument('-v', action='store_true')
    ap.add_argument('pat', nargs='?')
    a = ap.parse_args()
    files = sorted(glob.glob(f'{R}/src/tests/vcc/*.c') + glob.glob(f'{R}/src/tests/gcc-torture/execute/*.c'))
    if a.pat: files = [f for f in files if a.pat in f]
    with Pool(a.j) as p: res = p.map(one, files, chunksize=4)
    n = {'ok': 0, 'fail': 0, 'skip': 0}
    for k, f, m in res:
        n[k] += 1
        if k == 'fail': print('FAIL', os.path.relpath(f, R), m)
    print(f"vcc-cfi: {n['ok']}/{n['ok'] + n['fail']} ok, {n['skip']} skipped")
    sys.exit(1 if n['fail'] else 0)

if __name__ == '__main__': main()
