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

# -E without -P: line markers byte for byte (files in the temporary directory, run there)
MARK_FILES = {
    'a.h': 'int a;\n',
    'sys/b.h': '#pragma once\nint b;\n#include "c.h"\n',
    'sys/c.h': 'int c;\n',
    'im.h': '#ifndef IMG\n#define IMG\n#include "a.h"\n#define IM 42\nint junk;\n#endif\n',
}
MARK_PROBE = ('#include "a.h"\nint x;\n' + '\n' * 10 + 'int y;\n#include <b.h>\n#include <b.h>\n'
              '#pragma clang assume_nonnull begin\n' + '\n' * 12 + '#pragma clang assume_nonnull end\n'
              '#line 100 "other.c"\nint z;\n#line 7\nint w;\n# 30 "m\\"q.c"\nint v;\n')
MARK_ROWS = [(t, f) for t in TARGETS for f in
             ['', '-undef', '-include a.h -DQ=2 -UQ', '-include sys/b.h -include ./a.h', '-std=c23',
              '-ffreestanding -std=c89', '-x assembler-with-cpp', '-mgeneral-regs-only -fshort-wchar -fPIC',
              '-fno-PIE -fno-asynchronous-unwind-tables', '-imacros im.h', '-include a.h -imacros im.h -imacros im.h',
              '-std=iso9899:199409', '-std=c2y', '-std=gnu1x']]
MARK_ROWS += [('--target=arm64-apple-macos' + v, f) for v, f in
              [('', '-mmacosx-version-min=13.0'), ('14', ''), ('15.2', ''), ('10.15', ''), ('10.9', ''),
               ('11', '-mmacosx-version-min=12.3.1')]]

def mark_rows(verbose):
    bad = 0
    with tempfile.TemporaryDirectory() as d:
        for name, text in MARK_FILES.items():
            os.makedirs(os.path.join(d, os.path.dirname(name)), exist_ok=True)
            open(os.path.join(d, name), 'w').write(text)
        open(os.path.join(d, 'm.c'), 'w').write(MARK_PROBE)
        for t, flags in MARK_ROWS:
            for mode in (['-E'], ['-E', '-dM']):
                fl = [t, '-isystem', 'sys'] + flags.split() + mode + ['m.c']
                r = [subprocess.run([x] + fl, cwd=d, capture_output=True) for x in (vd.CL, vd.VPP)]
                ok = r[0].returncode == r[1].returncode and r[0].stdout == r[1].stdout
                if not ok:
                    bad += 1
                if verbose or not ok:
                    print(f'{"ok  " if ok else "FAIL"} {" ".join(mode)} {t} {flags or "(none)"}')
                    if not ok:
                        import difflib
                        for l in list(difflib.unified_diff(r[0].stdout.decode().splitlines(), r[1].stdout.decode().splitlines(),
                                                           'clang', 'vpp', n=0, lineterm=''))[:10]:
                            print('  ' + l)
    return bad

def main():
    verbose = '-v' in sys.argv
    bad = 0
    with tempfile.TemporaryDirectory() as d:
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
    bad += mark_rows(verbose)
    n = len(ROWS) + len(ERR_ROWS) + 2 * len(MARK_ROWS)
    print(f'vcc-driver: {n - bad}/{n} ok')
    sys.exit(1 if bad else 0)

main()
