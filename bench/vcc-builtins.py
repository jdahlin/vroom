#!/usr/bin/env python3
"""vcc-builtins.py: clang's builtins that vcc does not know (TODO.md tier 3, builtins).

    bench/vcc-builtins.py [-v] [--target T]

The names come from LLVM's generated tables (Builtins.inc, BuiltinsAArch64.inc
under Homebrew LLVM's include/clang/Basic); those clang's __has_builtin
accepts for the target in C are the list.  For each:
  - unknown: vcc -fsyntax-only on a call to it says "undeclared function"
    (a wrong-argument error means vcc knows the name);
  - has_builtin: vcc's __has_builtin answer differs from clang's.
Prints the counts and, with -v, the names.  Exit status 1 if vcc's
__has_builtin says 1 where clang says 0 (a program would take a path that
clang's build does not); the rest is a report.
"""
import os, re, subprocess, sys, tempfile

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VPP = os.environ.get('VCC_BIN', f'{R}/build/src') + '/vpp'
CLANG = '/opt/homebrew/opt/llvm/bin/clang'
INC = '/opt/homebrew/opt/llvm/include/clang/Basic'


def names():
    out = set()
    for f in ('Builtins.inc', 'BuiltinsAArch64.inc'):
        p = f'{INC}/{f}'
        if os.path.exists(p):
            out |= set(re.findall(r'\bBI(__builtin_\w+|__sync_\w+|__atomic_\w+|__c11_atomic_\w+)\b', open(p).read()))
    return sorted(out)


def has_builtin(cc, target, ns, wd):
    src = ''.join(f'{n} __has_builtin({n})\n' for n in ns)
    open(f'{wd}/h.c', 'w').write(src)
    p = subprocess.run([cc, f'--target={target}', '-E', '-P', '-x', 'c', f'{wd}/h.c'], capture_output=True, text=True)
    ans = {}
    for line in p.stdout.splitlines():
        t = line.split()
        if len(t) == 2:
            ans[t[0]] = t[1] == '1'
    return ans


def main(av):
    verbose = '-v' in av
    target = av[av.index('--target') + 1] if '--target' in av else 'aarch64-linux-gnu'
    ns = names()
    with tempfile.TemporaryDirectory() as wd:
        cl = has_builtin(CLANG, target, ns, wd)
        us = has_builtin(VPP, target, ns, wd)
        known = [n for n in ns if cl.get(n)]
        unknown = []
        for i in range(0, len(known), 200):
            chunk = known[i:i + 200]
            src = ''.join(f'void f{j}(void) {{ {n}(0); }}\n' for j, n in enumerate(chunk))
            open(f'{wd}/u.c', 'w').write(src)
            p = subprocess.run([VPP, f'--target={target}', '-fsyntax-only', '-ferror-limit=0', '-w', f'{wd}/u.c'],
                               capture_output=True, text=True)
            unknown += [n for n in chunk if f"undeclared function '{n}'" in p.stderr or f"undeclared identifier '{n}'" in p.stderr]
    hb = [n for n in known if not us.get(n)]
    extra = [n for n in ns if us.get(n) and not cl.get(n)]
    print(f'builtins: {len(known)} accepted by clang for {target}; {len(unknown)} unknown to vcc; '
          f'{len(hb)} with __has_builtin 0 in vcc; {len(extra)} 1 in vcc, 0 in clang')
    if verbose:
        print('unknown:', ' '.join(unknown))
        print('has_builtin 0:', ' '.join(hb))
        print('has_builtin 1, clang 0:', ' '.join(extra))
    return 1 if extra else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
