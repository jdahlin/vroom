#!/usr/bin/env python3
"""vas-enc.py: instruction-level encoder check of vas against llvm-mc.

  bench/vas-enc.py [S-DIR]

Runs `vas --trace` over every preprocessed .s in S-DIR (default: the
scratch reference dir given by $VAS_REF), collects each distinct
instruction statement with our encoding, assembles the same text with
`llvm-mc -show-encoding` and compares (fixup bits count as zero).
Statements llvm-mc cannot take standalone (.req aliases, local labels)
are reported as skipped.
"""
import glob, os, re, subprocess, sys
R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VAS = os.environ.get('VAS', f'{R}/build/ccs/vasm')
MC = '/opt/homebrew/opt/llvm/bin/llvm-mc'
B = f'{R}/work/build-o0' if os.path.isdir(f'{R}/work/build-o0') else '/Users/johandahlin/dev/vroom/work/build-o0'
from multiprocessing import Pool
def mc(t):
    p = subprocess.run([MC, '-triple', 'aarch64', '-mattr=+all', '-show-encoding'], input=t + '\n',
                       capture_output=True, text=True)
    m = re.findall(r'encoding: \[([^\]]*)\]', p.stdout)
    return m[0] if len(m) == 1 and not p.returncode else None

if __name__ == '__main__':
    d = sys.argv[1] if len(sys.argv) > 1 else os.environ['VAS_REF']
    ours = {}
    for f in sorted(glob.glob(f'{d}/*.s')):
        p = subprocess.run([VAS, '--trace', '-o', '/dev/null', f], cwd=B, capture_output=True, text=True)
        if p.returncode: print('FAIL', f, p.stderr.strip()[:300])
        for l in p.stdout.splitlines():
            w, fx, t = l.split(None, 2) if len(l.split(None, 2)) == 3 else (l.split()[0], l.split()[1], '')
            ours.setdefault(t.strip(), (int(w, 16), fx == '1'))
    lines = sorted(ours)
    with Pool(int(os.environ.get('J', 2))) as pool: refs = pool.map(mc, lines, chunksize=32)
    ok = skip = bad = 0
    for t, r in zip(lines, refs):
        if r is None: skip += 1; continue
        e = r.split(',')
        ref = 0; mask = 0
        for k, b in enumerate(e):
            if b.startswith('0x') and "'" not in b: ref |= int(b, 16) << 8 * k; mask |= 0xff << 8 * k
            elif b.startswith('0x'):    # 0xNN'A': some bits are fixup bits; trust the set ones
                v = int(b[2:4], 16); ref |= v << 8 * k; mask |= v << 8 * k
            elif b.startswith('0b'):
                for j, ch in enumerate(reversed(b[2:])):
                    if ch in '01': mask |= 1 << (8 * k + j); ref |= int(ch) << (8 * k + j)
        w, fx = ours[t]
        # bytes with fixup letters: llvm-mc zeroes those bits; we do too
        if (w & mask) == (ref & mask) and (fx or mask == 0xffffffff):
            ok += 1
        else:
            bad += 1
            if bad <= int(os.environ.get('SHOW', 40)): print(f'{t:50s} ours {w:08x} ref {ref:08x} mask {mask:08x}')
    print(f'{ok} ok, {bad} differ, {skip} skipped (llvm-mc cannot assemble standalone), {len(lines)} distinct')
