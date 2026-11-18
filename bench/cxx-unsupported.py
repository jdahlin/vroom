#!/usr/bin/env python3
"""cxx-unsupported.py: what the C++ front end reports for code it has no
rules for yet (src/test/cxx/unsupported).

Each X.cpp is valid C++ (clang++ -std=c++20 -fsyntax-only accepts it) that
vcc does not support.  vpp -fsyntax-only and vpp -c must both fail with
exactly X.cpp.stderr: the error at the construct, with the subclause, and
the note naming the check in vcc that refused it.  The note's line number
in vcc's source is compared as N (it moves with every edit there).
/* diag-vpp-flags: F */ in a test adds flags (-fcxx-speculation-limit=N).

    bench/cxx-unsupported.py [-v] [--bless] [--bin-dir DIR] [PATTERN]
"""
import os, re, subprocess, sys

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
D = f'{R}/src/test/cxx/unsupported'
BIN = os.environ.get('VCC_BIN', f'{R}/build/src')
if '--bin-dir' in sys.argv:
    k = sys.argv.index('--bin-dir'); BIN = os.path.abspath(sys.argv[k + 1]); del sys.argv[k:k + 2]
VPP = f'{BIN}/vpp'
FLAGS = ['--target=aarch64-linux-gnu', '-nostdinc', '-x', 'c++', '-std=c++20']


def normal(text):
    return re.sub(r"(refused by vcc's [^ :]+):\d+ ", r'\1:N ', text)


def main():
    verbose = '-v' in sys.argv
    bless = '--bless' in sys.argv
    pat = [a for a in sys.argv[1:] if not a.startswith('-')]
    names = sorted(f for f in os.listdir(D) if f.endswith('.cpp') and (not pat or re.search(pat[0], f)))
    bad = 0
    for name in names:
        outs = []
        m = re.search(r'/\*\s*diag-vpp-flags:\s*(.*?)\s*\*/', open(f'{D}/{name}').read())
        extra = m.group(1).split() if m else []
        for mode in (['-fsyntax-only'], ['-c', '-o', '/dev/null']):
            r = subprocess.run([VPP] + FLAGS + extra + mode + [name], cwd=D, capture_output=True, text=True)
            outs.append((mode[0], r.returncode, normal(r.stderr)))
        snap = f'{D}/{name}.stderr'
        if bless:
            open(snap, 'w').write(outs[0][2])
            continue
        want = open(snap).read() if os.path.exists(snap) else None
        why = [f'{m}: status {rc}' for m, rc, _ in outs if rc != 1]
        why += [f'{m}: stderr differs' for m, _, err in outs if err != want]
        if why:
            bad += 1
            print(f'FAIL {name}: ' + ', '.join(why))
            if verbose:
                print(f'  expected:\n{want}  got:\n{outs[0][2]}')
        elif verbose:
            print(f'ok   {name}')
    if not bless:
        print(f'cxx-unsupported: {len(names) - bad}/{len(names)} ok')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
