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


def return_rows(d):
    """Default errors controlled by return-type/return-mismatch, including pragmas."""
    import re
    cases = [(flags, '') for flags in [
        '', '-w', '-Wno-error', '-Wno-error=return-type', '-Wno-error=return-mismatch',
        '-Wno-return-type', '-Wno-return-mismatch', '-Werror=return-mismatch',
        '-Wno-error=return-type -Werror', '-Wno-error=return-type -Werror=return-mismatch',
        '-Wno-return-type -Wreturn-type', '-w -Wno-error=return-type',
        '-w -Werror=return-type', '-Wno-error=return-mismatch -Werror=return-type',
    ]]
    cases += [('-Werror', '#pragma clang diagnostic ' + state + ' "-Wreturn-type"\n')
              for state in ('ignored', 'warning', 'error')]
    cases += [('', '#pragma clang diagnostic push\n#pragma clang diagnostic ignored "-Wreturn-mismatch"\n'
                   '#pragma clang diagnostic pop\n')]
    rows = []
    for target in ('x86_64-linux-gnu', 'aarch64-linux-gnu'):
        for i, (flags, pragma) in enumerate(cases):
            src = os.path.join(d, f'return-{target}-{i}.c')
            open(src, 'w').write(pragma + 'void f(void) { return 3; }\nint g(void) { return; }\n')
            def check(src=src, flags=flags, target=target):
                answers = []
                for compiler in (vd.CL, vd.VPP):
                    rc, _, err = run([compiler, '--target=' + target, '-fsyntax-only',
                                      '-fno-color-diagnostics'] + flags.split(), src)
                    answers.append((rc, re.findall(r': (error|warning):', err)))
                return answers[0] == answers[1]
            rows.append((f'return policy {target} {flags or "default"} {i}', check))
    for target in ('x86_64-linux-gnu', 'aarch64-linux-gnu'):
        for flags in ('', '-ferror-limit=1', '-ferror-limit=2', '-ferror-limit=0',
                      '-Wno-error=return-type', '-Werror=return-type -ferror-limit=1',
                      '-Wno-return-type -Wreturn-type -Werror -ferror-limit=1'):
            src = os.path.join(d, 'return-emit.c')
            open(src, 'w').write('void f(void) { return 1; }\nvoid g(void) { return 2; }\nvoid h(void) { return 3; }\n')
            def check_emit(target=target, flags=flags, src=src):
                answers = []
                for i, compiler in enumerate((vd.CL, vd.VPP)):
                    output = os.path.join(d, f'return-emit-{i}.s')
                    if os.path.exists(output): os.unlink(output)
                    rc, _, err = run([compiler, '--target=' + target, '-S', '-o', output,
                                      '-fno-color-diagnostics'] + flags.split(), src)
                    answers.append((rc, os.path.exists(output),
                                    re.findall(r': (?:fatal )?(error|warning):', err),
                                    'too many errors emitted' in err))
                return answers[0] == answers[1]
            rows.append((f'return emit/limit {target} {flags or "default"}', check_emit))
    for target in ('x86_64-linux-gnu', 'aarch64-linux-gnu'):
        for limit in (0, 1, 2):
            src = os.path.join(d, 'independent-errors.c')
            open(src, 'w').write('int a = ;\nint b = ;\nint c = ;\n')
            def check_errors(target=target, limit=limit, src=src):
                answers = []
                for compiler in (vd.CL, vd.VPP):
                    rc, _, err = run([compiler, '--target=' + target, '-fsyntax-only',
                                      '-fno-color-diagnostics', f'-ferror-limit={limit}'], src)
                    answers.append((rc, re.findall(r': (?:fatal )?(error|warning):', err),
                                    'too many errors emitted' in err))
                return answers[0] == answers[1]
            rows.append((f'independent error limit {target} {limit}', check_errors))
    return rows


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
        # D4: the snippet styles; every header line stays clang's in all of them
        ('-fdiagnostics-style=vcc (default)', lambda: (lambda rc, e: rc == 1 and '| int a = countr;' in e
                                                      and 'the source line' not in e)(*v())),
        ('-fdiagnostics-style=clang', lambda: (lambda rc, e: rc == 1 and e.count('  |         ^') >= 1
                                               and 'note: ' in e)(*v('-fdiagnostics-style=clang'))),
        ('-fdiagnostics-style=narrated', lambda: (lambda rc, e: rc == 1 and 'the source line is: int a = countr;' in e
                                                  and ' | ' not in e)(*v('-fdiagnostics-style=narrated'))),
        ('-fdiagnostics-text-art-charset=unicode', lambda: '\u2502' in v('-fdiagnostics-text-art-charset=unicode')[1]),
        ('-fdiagnostics-text-art-charset=ascii', lambda: '\u2502' not in v('-fdiagnostics-text-art-charset=ascii')[1]),
        ('-fdiagnostics-style=bogus', lambda: (lambda rc, e: rc == 1 and "invalid value 'bogus'" in e)(
            *v('-fdiagnostics-style=bogus'))),
        ('same headers in every style', lambda: len({tuple(l for l in v(f'-fdiagnostics-style={s}')[1].splitlines()
                                                          if l.startswith(src)) for s in ('vcc', 'clang', 'narrated')}) == 1),
        # D2.1: a delayed bug in a TU without errors is an internal error; with errors it is silent
        ('delayed bug, no error', lambda: (lambda rc, e: rc == 1 and 'internal error' in e)(
            *v(f=ok_src, env={'VCC_TEST_DELAYED_BUG': '1'}))),
        ('delayed bug after an error', lambda: (lambda rc, e: rc == 1 and 'internal error' not in e)(
            *v(env={'VCC_TEST_DELAYED_BUG': '1'}))),
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
              '-std=iso9899:199409', '-std=c2y', '-std=gnu1x', '-fsigned-char', '-funsigned-char',
              '-fno-unsigned-char -fno-signed-char', '-ffast-math', '-ffast-math -fmath-errno -pthread',
              '-ffast-math -fno-finite-math-only', '-fno-math-errno -ffinite-math-only', '-fmath-errno']]
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
        drows = diag_rows(d) + return_rows(d)
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
    bad += mark_rows(verbose)
    n = len(ROWS) + len(ERR_ROWS) + len(drows) + 2 * len(MARK_ROWS)
    print(f'vcc-driver: {n - bad}/{n} ok')
    sys.exit(1 if bad else 0)

main()
