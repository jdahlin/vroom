#!/usr/bin/env python3
"""vcc-pic.py: GOT and TLS use per code model (-fno-pic, -fPIE, -fPIC, the hosted
default) checked against clang: for every C file in ccs/vcc/tests/pic, the
symbols with GOT or TLS relocations and the relocation types in each object must
match clang -c's (<1 s)."""
import glob, os, re, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hostenv import llvm, BUILD
R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VPP = os.path.join(os.environ.get('VCC_BIN') or BUILD, 'vpp')
CL, RE = llvm('clang'), llvm('llvm-readelf')

def relocs(o):
    out = subprocess.run([RE, '-r', o], capture_output=True, text=True, check=True).stdout
    s = set()
    for l in out.splitlines():
        m = re.match(r'\s*[0-9a-f]+\s+[0-9a-f]+\s+(R_AARCH64_\w+)\s+[0-9a-f]+\s+(\S+)', l)
        if m and ('GOT' in m.group(1) or 'TLS' in m.group(1)): s.add((m.group(1), m.group(2)))
    return s

bad = ok = 0
with tempfile.TemporaryDirectory() as t:
    for f in sorted(glob.glob(f'{R}/ccs/vcc/tests/pic/*.c')):
        for mode in ['-fno-pic', '-fPIE', '-fPIC', '-fpie', '-fpic', None]:
            fl = ['--target=aarch64-linux-gnu', '-O2', '-c', f] + ([mode] if mode else [])
            subprocess.run([CL] + fl + ['-o', f'{t}/c.o'], check=True)
            r = subprocess.run([VPP] + fl + ['-o', f'{t}/v.o'], capture_output=True, text=True)
            if r.returncode:
                print('FAIL', os.path.basename(f), mode, r.stderr[:200]); bad += 1; continue
            a, b = relocs(f'{t}/c.o'), relocs(f'{t}/v.o')
            if a != b:
                bad += 1
                print('DIFF', os.path.basename(f), mode or '(default)', 'clang only:', sorted(a - b), 'vcc only:', sorted(b - a))
            else: ok += 1
print(f'vcc-pic: {ok} pass, {bad} fail')
sys.exit(1 if bad else 0)
