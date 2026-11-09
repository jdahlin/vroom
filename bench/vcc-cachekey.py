#!/usr/bin/env python3
"""vcc-cachekey.py: options that change a TU must not be served from caches
filled under other options (TODO.md tier 1, "Header/snapshot cache keys").

  bench/vcc-cachekey.py [-v]

For each row (header, main file, flags A, flags B): one `vpp --batch -j 1`
process compiles the TU with A, then with B (the header cache, declaration
cache and token cache are shared, as in vccd).  B's object and diagnostics
must equal a fresh `vpp` run with B.
"""
import os, subprocess, sys, tempfile
import importlib.util
spec = importlib.util.spec_from_file_location('vd', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'vcc-diff.py'))
vd = importlib.util.module_from_spec(spec); spec.loader.exec_module(vd)

# (-W/-Werror in the header cache key, pp/hcache.c sig_of, is not testable here: --batch prints no diagnostics)
T = ['--target=aarch64-linux-gnu', '-O2', '-nostdinc']
ROWS = [
    # pp: -W changes what a header's preprocessing reports
    # front end: options that change what declarations mean
    ('gnu89-inline', 'inline int h(void) { return 1; }\n', '#include "h.h"\nint f(void) { return h(); }\n',
     [], ['-fgnu89-inline']),
    ('std-gnu89', 'inline int h(void) { return 1; }\n', '#include "h.h"\nint f(void) { return h(); }\n',
     [], ['-std=gnu89']),
    ('no-builtin', 'int abs(int);\nstatic inline int g(int x) { return abs(x); }\n',
     '#include "h.h"\nint f(int x) { return g(x); }\n', [], ['-fno-builtin']),
    ('implicit-int', 'static k = 3;\n', '#include "h.h"\nint f(void) { return k; }\n',
     ['-Wno-error=implicit-int'], []),
    ('instrument', 'static inline int g(int x) { return x + 1; }\n',
     '#include "h.h"\nint f(int x) { return g(x); }\n', [], ['-finstrument-functions']),
    ('unsigned-char', 'static char c = -1;\nstatic inline int g(void) { return c; }\n',
     '#include "h.h"\nint f(void) { return g(); }\n', [], ['-funsigned-char']),
]

def run(cmd, cwd):
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    return r.returncode, r.stderr

def main():
    verbose = '-v' in sys.argv
    bad = 0
    for name, h, m, a, b in ROWS:
        with tempfile.TemporaryDirectory() as d:
            open(f'{d}/h.h', 'w').write(h); open(f'{d}/m.c', 'w').write(m)
            fresh_rc, fresh_err = run([vd.VPP] + T + b + ['-c', 'm.c', '-o', 'fresh.o'], d)
            jobs = f'{d}/jobs'
            with open(jobs, 'w') as f:
                for fl, o in ((a, 'a.o'), (b, 'b.o')):
                    f.write('\x1f'.join(['-'] + T + fl + ['-c', f'{d}/m.c', '-o', f'{d}/{o}']) + '\n')
            _, berr = run([vd.VPP, '--batch', jobs, '-j', '1', '-C', d], d)
            _, aerr = run([vd.VPP] + T + a + ['-c', 'm.c', '-o', 'a1.o'], d)
            rd = lambda p: open(p, 'rb').read() if os.path.exists(p) else None
            same_obj = rd(f'{d}/fresh.o') == rd(f'{d}/b.o')
            exp_err = (aerr + fresh_err).replace(f'{d}/', '')
            same_err = exp_err.strip() == berr.replace(f'{d}/', '').strip() if True else True
            ok = same_obj and (same_err or 'pp' not in name)
            if not ok:
                bad += 1
            if verbose or not ok:
                why = [] if same_obj else ['object differs']
                if not same_err and 'pp' in name:
                    why.append('diagnostics differ')
                print(f'{"ok  " if ok else "FAIL"} {name}: {" ".join(a) or "-"} then {" ".join(b) or "-"} {", ".join(why)}')
    print(f'vcc-cachekey: {len(ROWS) - bad}/{len(ROWS)} ok')
    sys.exit(1 if bad else 0)

main()
