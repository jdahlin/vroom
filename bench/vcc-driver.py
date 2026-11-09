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

# -E with errors: clang still prints the text to stdout (status 1)
ERR_ROWS = [
    ('--target=aarch64-linux-gnu', '', 'int a;\n#error x\nint b;\n#include "nonexistent.h"\nint c;\n'),
]

# diagnostics options (PLAN-diag D1.3, D3): vcc's own, checked on what vcc prints
DIAG_SRC = 'int counter;\nint a = countr;\nint b = countr;\nint c = 1 +;\n'


def diag_rows(d):
    import json
    spec = importlib.util.spec_from_file_location('sc', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'sarif-check.py'))
    sc = importlib.util.module_from_spec(spec); spec.loader.exec_module(sc)
    src = os.path.join(d, 'diag.c')
    open(src, 'w').write(DIAG_SRC)
    sf = os.path.join(d, 'out.sarif')

    ok_src = os.path.join(d, 'ok.c')
    open(ok_src, 'w').write('int ok;\n')

    def v(*fl, f=src, env=None):
        r = subprocess.run([vd.VPP, '--target=aarch64-linux-gnu', '-fsyntax-only', '-fno-color-diagnostics'] + list(fl) + [f],
                           capture_output=True, text=True, env=dict(os.environ, **(env or {})))
        return r.returncode, r.stderr

    def json_lines(err):
        recs = [json.loads(l) for l in err.splitlines()]
        return len(recs) == 3 and all(r['id'] and 'rendered' in r for r in recs)

    def sarif_ok(err):
        return not sc.check(json.loads(err)) and len(json.loads(err)['runs'][0]['results']) == 3

    return [
        ('-fdiagnostics-format=json', lambda: (lambda rc, e: rc == 1 and json_lines(e))(*v('-fdiagnostics-format=json'))),
        ('-fdiagnostics-format=sarif', lambda: (lambda rc, e: rc == 1 and sarif_ok(e))(*v('-fdiagnostics-format=sarif'))),
        ('-fdiagnostics-add-output=sarif:file=F', lambda: (lambda rc, e: rc == 1 and 'errors generated' in e
                                                          and sarif_ok(open(sf).read()))(*v(f'-fdiagnostics-add-output=sarif:file={sf}'))),
        ('-fdiagnostics-show-id', lambda: (lambda rc, e: e.count('[err_undeclared_var_use_suggest]') == 2
                                           and '[err_expected_expression]' in e)(*v('-fdiagnostics-show-id'))),
        ('-fspell-checking-limit=1', lambda: v('-fspell-checking-limit=1')[1].count('did you mean') == 1),
        ('-fno-spell-checking', lambda: v('-fno-spell-checking')[1].count('did you mean') == 0),
        # D2.1: a delayed bug in a TU without errors is an internal error; with errors it is silent
        ('delayed bug, no error', lambda: (lambda rc, e: rc == 1 and 'internal error' in e)(
            *v(f=ok_src, env={'VCC_TEST_DELAYED_BUG': '1'}))),
        ('delayed bug after an error', lambda: (lambda rc, e: rc == 1 and 'internal error' not in e)(
            *v(env={'VCC_TEST_DELAYED_BUG': '1'}))),
    ]


def main():
    verbose = '-v' in sys.argv
    bad = 0
    with tempfile.TemporaryDirectory() as d:
        drows = diag_rows(d)
        for name, fn in drows:
            ok = fn()
            bad += not ok
            if verbose or not ok:
                print(f'{"ok  " if ok else "FAIL"} {name}')
        for i, (t, flags, probe) in enumerate(ROWS + ERR_ROWS):
            src = os.path.join(d, f'p{i}.c')
            open(src, 'w').write(probe)
            fl = [t] + flags.split()
            rc, out, err = run([vd.CL, '-E', '-P'] + fl, src)
            vrc, vout, verr = run([vd.VPP, '-E', '-P'] + fl, src)
            ok = rc == vrc and (out.split() == vout.split() or
                              (i >= len(ROWS) and ''.join(out.split()) == ''.join(vout.split())))
            if not ok:
                bad += 1
            if verbose or not ok:
                print(f'{"ok  " if ok else "FAIL"} {t} {flags or "(none)"}' +
                      ('' if ok else f'\n  clang: {" ".join(out.split())}\n  vpp:   {" ".join(vout.split())} {verr.strip()}'))
    n = len(ROWS) + len(ERR_ROWS) + len(drows)
    print(f'vcc-driver: {n - bad}/{n} ok')
    sys.exit(1 if bad else 0)

main()
