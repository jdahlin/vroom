#!/usr/bin/env python3
"""Volatile accesses survive one for one: the loads and stores of each function in
src/test/volatile/*.c (-O2 -S), against the "/* ld N st N */" before it, for the
direct emitter and for VIR (VCC_VIR=bl).  --bin-dir DIR picks vpp."""
import os, re, subprocess, sys, glob

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
bind = os.environ.get('VCC_BIN', os.path.join(root, 'build/src'))
if '--bin-dir' in sys.argv:
    bind = sys.argv[sys.argv.index('--bin-dir') + 1]
vpp = os.path.join(bind, 'vpp')
LD = re.compile(r'^\t(ldr|ldur|ldrb|ldrh|ldrsb|ldrsh|ldrsw|ldurb|ldurh|ldursw|ldp)\t(.*)')
ST = re.compile(r'^\t(str|stur|strb|strh|sturb|sturh|stp)\t(.*)')
SAVE = re.compile(r'^(x(19|2\d|30)|d(8|9|1[0-5]))\b.*\[x29')   # callee-save traffic (x29-relative; locals are at sp)

def counts(src, vir):
    env = dict(os.environ)
    env.pop('VCC_VIR', None)
    if vir:
        env['VCC_VIR'] = 'bl'
    s = subprocess.run([vpp, '-O2', '-S', src, '-o', '-'], capture_output=True, text=True, env=env)
    if s.returncode:
        sys.exit(f'{src}: vpp failed\n{s.stderr}')
    out, fn = {}, None
    for line in s.stdout.split('\n'):
        m = re.match(r'^_?([A-Za-z_]\w*):', line)
        if m and not line.startswith(('.L', 'L')):
            fn = m.group(1)
            out[fn] = [0, 0]
            continue
        for k, rx in ((0, LD), (1, ST)):
            m = rx.match(line)
            if m and fn and not SAVE.match(m.group(2)) and 'x29, x30' not in m.group(2) and '@GOT' not in m.group(2) and ':got' not in m.group(2):
                out[fn][k] += 1
    return out

bad = n = 0
for src in sorted(glob.glob(os.path.join(root, 'src/test/volatile/*.c'))):
    want = {m.group(3): [int(m.group(1)), int(m.group(2))]
            for m in re.finditer(r'/\* ld (\d+) st (\d+) \*/[^(\n]*?(\w+)\(', open(src).read())}
    for vir in (False, True):
        got = counts(src, vir)
        for f, w in want.items():
            n += 1
            if got.get(f) != w:
                bad += 1
                print(f'FAIL {os.path.basename(src)} {f} ({"VIR" if vir else "direct"}): ld/st {got.get(f)} want {w}')
print(f'volatile: {n - bad}/{n} pass')
sys.exit(1 if bad else 0)
