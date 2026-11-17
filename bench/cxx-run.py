#!/usr/bin/env python3
"""cxx-run.py: the C++ front end's code against clang's (doc/PLAN-cxx-sema.md S1d).

Each program of src/test/cxx/run is compiled twice for Linux arm64: by
clang++ -O2, and by cxxsema -S (the semantic tree lowered to C's AST, C's
code generator) at each -O level asked for, its assembly assembled by
clang.  Both are linked with the C suites' run time (src/test/rt) and run
under qemu-system-aarch64 with semihosting, as bench/vcc-ctests.py does;
the exit status and output must be clang's.  The functions and variables
the -O0 object defines are also clang -O0's, by name and binding (global,
local, weak).

    bench/cxx-run.py [-j N] [-O LEVELS] [-v] [PATTERN]

-O takes the levels as one argument (default 0123).  A program that
cxxsema does not lower is a failure with its reason.
"""
import importlib.util, os, re, subprocess, sys
from multiprocessing import Pool

B = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('ct', f'{B}/vcc-ctests.py')
ct = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ct)

R = os.path.dirname(B)
TESTS = f'{R}/src/test/cxx/run'
CXXSEMA = os.path.join(os.environ.get('VCC_BIN') or ct.BUILD, 'cxxsema')
OUT = f'{ct.BUILD}/cxx-run'
NM = ct.llvm('llvm-nm')
TARGET = ['--target=aarch64-linux-gnu', '-nostdinc', '-fno-PIE',
          '-fno-asynchronous-unwind-tables', '-fno-stack-protector',
          '-mno-outline-atomics']
CXX = TARGET + ['-x', 'c++', '-std=c++20', '-fno-exceptions', '-fno-rtti', '-w']


def defined(obj):
    """name -> 'global' or 'local' for each function and object defined"""
    rc, out, err = ct.sh([NM, '--defined-only', obj])
    symbols = {}
    for line in out.decode().splitlines():
        parts = line.split()
        if len(parts) != 3 or parts[1].upper() not in 'TDRBWV':
            continue
        if parts[2].startswith('$') or parts[2].startswith('.'):
            continue
        binding = 'global' if parts[1].isupper() else 'local'
        if parts[1] in 'WV':
            binding = 'weak'
        symbols[parts[2]] = binding
    return symbols


def run(objs, elf):
    rc, err = ct.link(objs, elf)
    if rc:
        return None, 'link: ' + ct.first_err(err)
    return ct.qemu(elf), None


def one(job):
    name, levels = job
    src = f'{TESTS}/{name}.cpp'
    wd = f'{OUT}/{name}'
    os.makedirs(wd, exist_ok=True)
    results = []
    rc, _, err = ct.sh([ct.CL] + CXX + ['-O2', '-c', src, '-o', f'{wd}/ref.o'])
    if rc:
        return name, [('ref', 'oracle: ' + ct.first_err(err.decode()))]
    expected, why = run([f'{wd}/ref.o'], f'{wd}/ref.elf')
    if why:
        return name, [('ref', 'oracle ' + why)]
    rc, _, err = ct.sh([ct.CL] + CXX + ['-O0', '-c', src, '-o', f'{wd}/ref0.o'])
    symbols = defined(f'{wd}/ref0.o') if not rc else None
    for level in levels:
        s, o, elf = f'{wd}/O{level}.s', f'{wd}/O{level}.o', f'{wd}/O{level}.elf'
        rc, out, err = ct.sh([CXXSEMA, f'-O{level}', '-S', src])
        if rc:
            results.append((level, 'cxxsema: ' + (err.decode().strip().splitlines() or ['exit %s' % rc])[-1]))
            continue
        open(s, 'wb').write(out)
        rc, _, err = ct.sh([ct.CL] + TARGET + ['-c', s, '-o', o])
        if rc:
            results.append((level, 'assemble: ' + ct.first_err(err.decode())))
            continue
        ours = defined(o)
        # (above -O0 an inlined static function may be gone)
        if level == '0' and symbols is not None and ours != symbols:
            missing = sorted(set(symbols) - set(ours))
            extra = sorted(set(ours) - set(symbols))
            bound = sorted(k for k in ours if k in symbols and ours[k] != symbols[k])
            results.append((level, f'symbols: missing {missing} extra {extra} binding {bound}'))
            continue
        got, why = run([o], elf)
        if why:
            results.append((level, why))
        elif got != expected:
            results.append((level, f'ran {got!r}, clang {expected!r}'))
        else:
            results.append((level, None))
    return name, results


def main():
    av = sys.argv[1:]
    jobs, levels, verbose, pattern = 6, '0123', False, ''
    while av:
        a = av.pop(0)
        if a == '-j':
            jobs = int(av.pop(0))
        elif a.startswith('-j'):
            jobs = int(a[2:])
        elif a == '-O':
            levels = av.pop(0)
        elif a == '-v':
            verbose = True
        else:
            pattern = a
    if not os.path.exists(CXXSEMA):
        sys.exit(f'cxx-run: no {CXXSEMA} (make -C src {CXXSEMA})')
    ct.build_rt()
    names = sorted(f[:-4] for f in os.listdir(TESTS)
                   if f.endswith('.cpp') and re.search(pattern, f))
    passed = failed = 0
    with Pool(jobs) as pool:
        for name, results in pool.imap(one, [(n, levels) for n in names]):
            for level, why in results:
                if why is None:
                    passed += 1
                    if verbose:
                        print(f'PASS {name} -O{level}')
                else:
                    failed += 1
                    print(f'FAIL {name} -O{level}: {why}')
    print(f'cxx-run: {passed} passed, {failed} failed ({len(names)} programs, -O{levels})')
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
