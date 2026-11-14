#!/usr/bin/env python3
"""vlink-dyn.py: vlink's x86-64 dynamic executables against glibc (dyn.inc, dso.inc).

  bench/vlink-dyn.py [-v] [PAT]

Every C file in ccs/vcc/tests/vlink-dyn is compiled by clang and by vpp, in
three modes: -fPIE linked -pie, -fno-pic linked -no-pie (copy relocations,
canonical PLT entries), -fPIE linked -pie -z now.  Each object is linked by
ld.lld and by vlink with the link line clang's driver gives (crt files, libc.so
as a linker script, --as-needed libgcc_s), both programs run, and their exit
status and output must be the same; vlink's must have no DT_TEXTREL and name
libc.so.6 in DT_NEEDED.  Linux x86-64 only (<5 s).
"""
import glob, os, platform, re, shlex, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hostenv import llvm, BUILD
R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.environ.get('VCC_BIN') or BUILD
VPP, VLINK = os.path.join(BIN, 'vpp'), os.path.join(BIN, 'vlink')
CL, LLD, RE = llvm('clang'), llvm('ld.lld'), llvm('llvm-readelf')
MODES = {
    'pie':   (['-fPIE'], ['-pie'], []),
    'nopie': (['-fno-pic'], ['-no-pie'], []),
    'now':   (['-fPIE'], ['-pie'], ['-z', 'now']),
}


def ld_line(cflags, lflags, obj, exe):
    """the linker's arguments clang's driver would pass (without the linker itself)"""
    out = subprocess.run([CL] + lflags + ['-###', obj, '-o', exe], capture_output=True, text=True).stderr
    last = [l for l in out.splitlines() if l.startswith(' "')][-1]
    return shlex.split(last)[1:]


def run(exe):
    p = subprocess.run([exe], capture_output=True, timeout=20)
    return p.returncode, p.stdout


def main():
    args = sys.argv[1:]
    verbose = '-v' in args
    args = [a for a in args if a != '-v']
    pat = args[0] if args else None
    if platform.system() != 'Linux' or platform.machine() != 'x86_64':
        print('vlink-dyn: skipped (needs Linux x86-64)')
        return 0
    ok = bad = 0
    with tempfile.TemporaryDirectory() as t:
        for f in sorted(glob.glob(f'{R}/ccs/vcc/tests/vlink-dyn/*.c')):
            name = os.path.basename(f)[:-2]
            if pat and pat not in name:
                continue
            for mode, (cf, lf, extra) in MODES.items():
                for cc in ('clang', 'vpp'):
                    tag = f'{name}/{mode}/{cc}'
                    o = f'{t}/{name}-{mode}-{cc}.o'
                    comp = CL if cc == 'clang' else VPP
                    r = subprocess.run([comp, '-O1', '-c'] + cf + [f, '-o', o], capture_output=True, text=True)
                    if r.returncode:
                        print(f'FAIL {tag}: compile: {r.stderr.strip()[:200]}')
                        bad += 1
                        continue
                    res = {}
                    for ld, prog in (('lld', LLD), ('vlink', VLINK)):
                        exe = f'{t}/{name}-{mode}-{cc}.{ld}'
                        r = subprocess.run([prog] + ld_line(cf, lf, o, exe) + extra, capture_output=True, text=True)
                        if r.returncode:
                            res[ld] = ('link', r.stderr.strip()[:300])
                            continue
                        res[ld] = run(exe)
                        if ld == 'vlink':
                            d = subprocess.run([RE, '-dW', exe], capture_output=True, text=True).stdout
                            if 'TEXTREL' in d or 'libc.so.6' not in d:
                                res[ld] = ('dynamic', d[:300])
                    if res['lld'] and res['lld'][0] == 'link':
                        print(f'FAIL {tag}: lld: {res["lld"][1]}')
                        bad += 1
                    elif res['vlink'] != res['lld']:
                        print(f'FAIL {tag}: vlink {res["vlink"]!r}\n     lld   {res["lld"]!r}')
                        bad += 1
                    else:
                        ok += 1
                        if verbose:
                            print(f'ok   {tag}: {res["vlink"][1].decode(errors="replace").strip()!r}')
    print(f'vlink-dyn: {ok}/{ok + bad} ok')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
