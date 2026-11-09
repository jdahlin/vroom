#!/usr/bin/env python3
"""vcc-driver.py: command lines -> what vpp must do, against clang (TODO.md T11).

  bench/vcc-driver.py [-v]

A row is (flags, probe).  The probe is preprocessed (-E -P) by clang and by
vpp with the same flags and the token streams must be equal.  Starts with
the predefines that follow from -O and from -march/-mcpu (LSE); add rows as driver items are fixed.
"""
import os, subprocess, sys, tempfile
import importlib.util
spec = importlib.util.spec_from_file_location('vd', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'vcc-diff.py'))
vd = importlib.util.module_from_spec(spec); spec.loader.exec_module(vd)

OPT_PROBE = '''
#ifdef __OPTIMIZE__
optimize __OPTIMIZE__
#endif
#ifdef __OPTIMIZE_SIZE__
optimize_size __OPTIMIZE_SIZE__
#endif
#ifdef __NO_INLINE__
no_inline __NO_INLINE__
#endif
'''
LSE_PROBE = '''
#ifdef __ARM_FEATURE_ATOMICS
lse __ARM_FEATURE_ATOMICS
#endif
'''
TARGETS = ['--target=aarch64-linux-gnu', '--target=arm64-apple-darwin']
ROWS = [(t, f, OPT_PROBE) for t in TARGETS for f in
        ['', '-O0', '-O1', '-O2', '-O3', '-O', '-Os', '-Oz', '-Os -O2', '-O2 -O0', '-O0 -Oz', '-Og']]
ROWS += [(TARGETS[0], f, LSE_PROBE) for f in
         ['', '-march=armv8-a', '-march=armv8.1-a', '-march=armv8.5-a', '-march=armv9-a', '-march=armv8-a+lse',
          '-march=armv8.5-a+nolse', '-march=armv8.2-a+crypto+nolse+lse', '-mcpu=cortex-a53', '-mcpu=cortex-a76',
          '-mcpu=neoverse-n1', '-mcpu=cortex-a72+lse', '-mcpu=cortex-a76 -march=armv8-a',
          '-march=armv8-a -mcpu=cortex-a76', '-mcpu=apple-m1']]
ROWS += [(TARGETS[1], '', LSE_PROBE)]

def run(cmd, src):
    r = subprocess.run(cmd + [src], capture_output=True, text=True)
    return r.returncode, r.stdout, r.stderr

def main():
    verbose = '-v' in sys.argv
    bad = 0
    with tempfile.TemporaryDirectory() as d:
        for i, (t, flags, probe) in enumerate(ROWS):
            src = os.path.join(d, f'p{i}.c')
            open(src, 'w').write(probe)
            fl = [t] + flags.split()
            rc, out, err = run([vd.CL, '-E', '-P'] + fl, src)
            vrc, vout, verr = run([vd.VPP, '-E', '-P'] + fl, src)
            ok = rc == vrc and out.split() == vout.split()
            if not ok:
                bad += 1
            if verbose or not ok:
                print(f'{"ok  " if ok else "FAIL"} {t} {flags or "(none)"}' +
                      ('' if ok else f'\n  clang: {" ".join(out.split())}\n  vpp:   {" ".join(vout.split())} {verr.strip()}'))
    print(f'vcc-driver: {len(ROWS) - bad}/{len(ROWS)} ok')
    sys.exit(1 if bad else 0)

main()
